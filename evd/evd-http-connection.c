/*
 * evd-http-connection.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2013, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#include <string.h>

#include "evd-http-connection.h"

#include "evd-error.h"
#include "evd-buffered-input-stream.h"
#include "evd-http-chunked-decoder.h"

G_DEFINE_TYPE (EvdHttpConnection, evd_http_connection, EVD_TYPE_CONNECTION)

#define EVD_HTTP_CONNECTION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                              EVD_TYPE_HTTP_CONNECTION, \
                                              EvdHttpConnectionPrivate))

#define HEADER_BLOCK_SIZE       256
#define MAX_HEADERS_SIZE  16 * 1024
#define CONTENT_BLOCK_SIZE     4096

/* private data */
struct _EvdHttpConnectionPrivate
{
  GSimpleAsyncResult *async_result;

  GString *buf;
  gchar *last_buf_block;

  gint priority;

  gint last_headers_pos;

  SoupHTTPVersion http_ver;

  goffset content_len;
  SoupEncoding encoding;
  gsize content_read;

  EvdHttpRequest *current_request;

  gboolean keepalive;

  GConverter *chunked_decoder;
};

/* properties */
enum
{
  PROP_0
};

struct EvdHttpConnectionResponseHeaders
{
  SoupMessageHeaders *headers;
  SoupHTTPVersion version;
  guint status_code;
  gchar *reason_phrase;
};

struct ContentReadData
{
  gssize size;
  gboolean more;
};

static void     evd_http_connection_class_init         (EvdHttpConnectionClass *class);
static void     evd_http_connection_init               (EvdHttpConnection *self);

static void     evd_http_connection_finalize           (GObject *obj);
static void     evd_http_connection_dispose            (GObject *obj);

static gboolean evd_http_connection_close              (GIOStream     *stream,
                                                        GCancellable  *cancellable,
                                                        GError       **error);

static void     evd_http_connection_read_headers_block (EvdHttpConnection *self);

static void     evd_http_connection_read_content_block (EvdHttpConnection *self,
                                                        void              *buf,
                                                        gsize              size);

static void
evd_http_connection_class_init (EvdHttpConnectionClass *class)
{
  GObjectClass *obj_class;
  GIOStreamClass *io_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_http_connection_dispose;
  obj_class->finalize = evd_http_connection_finalize;

  io_stream_class = G_IO_STREAM_CLASS (class);
  io_stream_class->close_fn = evd_http_connection_close;

  g_type_class_add_private (obj_class, sizeof (EvdHttpConnectionPrivate));
}

static void
evd_http_connection_init (EvdHttpConnection *self)
{
  EvdHttpConnectionPrivate *priv;

  priv = EVD_HTTP_CONNECTION_GET_PRIVATE (self);
  self->priv = priv;

  priv->async_result = NULL;

  priv->buf = g_string_new ("");

  priv->last_headers_pos = 0;

  priv->http_ver = SOUP_HTTP_1_1;

  priv->encoding = SOUP_ENCODING_UNRECOGNIZED;
  priv->content_len = 0;

  priv->keepalive = FALSE;

  priv->chunked_decoder = G_CONVERTER (evd_http_chunked_decoder_new ());

  priv->last_buf_block = NULL;
}

static void
evd_http_connection_dispose (GObject *obj)
{
  EvdHttpConnection *self = EVD_HTTP_CONNECTION (obj);

  if (self->priv->current_request != NULL)
    {
      g_object_unref (self->priv->current_request);
      self->priv->current_request = NULL;
    }

  if (self->priv->async_result != NULL)
    {
      g_simple_async_result_set_error (self->priv->async_result,
                                       G_IO_ERROR,
                                       G_IO_ERROR_FAILED,
                                       "HTTP connection destroyed while an operation was pending");
      g_simple_async_result_complete (self->priv->async_result);
      g_object_unref (self->priv->async_result);
      self->priv->async_result = NULL;
    }

  G_OBJECT_CLASS (evd_http_connection_parent_class)->dispose (obj);
}

static void
evd_http_connection_finalize (GObject *obj)
{
  EvdHttpConnection *self = EVD_HTTP_CONNECTION (obj);

  g_string_free (self->priv->buf, TRUE);

  g_object_unref (self->priv->chunked_decoder);

  if (self->priv->last_buf_block != NULL)
    g_slice_free1 (CONTENT_BLOCK_SIZE, self->priv->last_buf_block);

  G_OBJECT_CLASS (evd_http_connection_parent_class)->finalize (obj);
}

static gboolean
evd_http_connection_close (GIOStream     *stream,
                           GCancellable  *cancellable,
                           GError       **error)
{
  EvdHttpConnection *self = EVD_HTTP_CONNECTION (stream);
  gboolean result;

  result =
    G_IO_STREAM_CLASS (evd_http_connection_parent_class)->close_fn (stream,
                                                                    cancellable,
                                                                    error);

  if (self->priv->async_result != NULL)
    {
      GSimpleAsyncResult *res;

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_CLOSED,
                                       "Connection closed during async operation");
      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  return result;
}

static void
evd_http_connection_response_headers_destroy (gpointer data)
{
  struct EvdHttpConnectionResponseHeaders *response;

  response = (struct EvdHttpConnectionResponseHeaders *) data;

  if (response->headers != NULL)
    soup_message_headers_free (response->headers);
  if (response->reason_phrase != NULL)
    g_free (response->reason_phrase);

  g_free (response);
}

static SoupURI *
evd_http_connection_build_uri (EvdHttpConnection  *self,
                               const gchar        *path,
                               SoupMessageHeaders *headers)
{
  gchar *scheme;
  const gchar *host;
  gchar *uri_str;
  SoupURI *uri;

  if (evd_connection_get_tls_active (EVD_CONNECTION (self)))
    scheme = g_strdup ("https");
  else
    scheme = g_strdup ("http");

  host = soup_message_headers_get_one (headers, "host");

  uri_str = g_strconcat (scheme, "://", host, path, NULL);

  uri = soup_uri_new (uri_str);

  g_free (uri_str);
  g_free (scheme);

  return uri;
}

static void
evd_http_connection_on_read_headers (EvdHttpConnection *self,
                                     GString           *buf)
{
  GSimpleAsyncResult *res;
  gpointer source_tag;

  if (self->priv->async_result == NULL)
    return;

  g_io_stream_clear_pending (G_IO_STREAM (self));

  res = self->priv->async_result;
  self->priv->async_result = NULL;
  source_tag = g_simple_async_result_get_source_tag (res);

  if (source_tag == evd_http_connection_read_request_headers)
    {
      SoupMessageHeaders *headers;
      gchar *method = NULL;
      gchar *path = NULL;
      SoupHTTPVersion version;

      headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_REQUEST);

      if (soup_headers_parse_request (buf->str,
                                      buf->len - 2,
                                      headers,
                                      &method,
                                      &path,
                                      &version)
          && method != NULL
          && version <= SOUP_HTTP_1_1)
        {
          EvdHttpRequest *request;
          SoupURI *uri;
          const gchar *conn_header;

          uri = evd_http_connection_build_uri (self, path, headers);

          request = g_object_new (EVD_TYPE_HTTP_REQUEST,
                                  "version", version,
                                  "headers", headers,
                                  "method", method,
                                  "uri", uri,
                                  NULL);

          soup_uri_free (uri);

          evd_http_connection_set_current_request (self, request);

          g_simple_async_result_set_op_res_gpointer (res, request, g_object_unref);

          self->priv->encoding =
            soup_message_headers_get_encoding (headers);
          self->priv->content_len =
            soup_message_headers_get_content_length (headers);

          /* detect if is keep-alive */
          conn_header = soup_message_headers_get_one (headers, "Connection");

          self->priv->keepalive =
            (version == SOUP_HTTP_1_0 && conn_header != NULL &&
             g_strstr_len (conn_header, -1, "keep-alive") != NULL) ||
            (version == SOUP_HTTP_1_1 && conn_header != NULL &&
             g_strstr_len (conn_header, -1, "close") == NULL);
        }
      else
        {
          soup_message_headers_free (headers);

          g_simple_async_result_set_error (res,
                                           G_IO_ERROR,
                                           G_IO_ERROR_INVALID_DATA,
                                           "Failed to parse HTTP request headers");
        }

      g_free (method);
      g_free (path);
    }
  else if (source_tag == evd_http_connection_read_response_headers)
    {
      struct EvdHttpConnectionResponseHeaders *response;

      response = g_new0 (struct EvdHttpConnectionResponseHeaders, 1);
      response->headers =
        soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

      if (soup_headers_parse_response (buf->str,
                                       buf->len - 2,
                                       response->headers,
                                       &response->version,
                                       &response->status_code,
                                       &response->reason_phrase))
        {
          g_simple_async_result_set_op_res_gpointer (res,
                                  response,
                                  evd_http_connection_response_headers_destroy);

          self->priv->encoding =
            soup_message_headers_get_encoding (response->headers);
          self->priv->content_len =
            soup_message_headers_get_content_length (response->headers);
        }
      else
        {
          g_simple_async_result_set_error (res,
                                           G_IO_ERROR,
                                           G_IO_ERROR_INVALID_DATA,
                                           "Failed to parse HTTP response headers");
        }
    }

  g_simple_async_result_complete_in_idle (res);
  g_object_unref (res);
}

static gint
evd_http_connection_find_end_headers_mark (const GString *buf,
                                           gint           last_pos)
{
  gint i;

  i = last_pos;
  while (i < buf->len)
    {
      if (buf->str[i] != '\r' && buf->str[i] != '\n')
        {
          i = i + 4;
        }
      else if (buf->str[i+1] != '\r' && buf->str[i+1] != '\n')
        {
          i = i - 3;
        }
      else if (buf->str[i+2] == '\r' && buf->str[i+3] == '\n')
        {
          if (buf->str[i] == '\r' && buf->str[i+1] == '\n')
            return i + 4;
          else
            i = i + 4;
        }
      else
        {
          i = i - 1;
        }
    }

  return -1;
}

static void
evd_http_connection_on_read_headers_block (GObject      *obj,
                                           GAsyncResult *res,
                                           gpointer      user_data)
{
  EvdHttpConnection *self = EVD_HTTP_CONNECTION (user_data);
  GError *error = NULL;
  gssize size;

  if ( (size = g_input_stream_read_finish (G_INPUT_STREAM (obj),
                                           res,
                                           &error)) >= 0)
    {
      gint pos;
      gint extra;

      extra = HEADER_BLOCK_SIZE - size;

      if (extra > 0)
        g_string_truncate (self->priv->buf, self->priv->buf->len - extra);

      if ( (pos =
            evd_http_connection_find_end_headers_mark (self->priv->buf,
                                             self->priv->last_headers_pos)) > 0)
        {
          void *unread_buf;
          gsize unread_size;

          unread_size = self->priv->buf->len - pos;
          if (unread_size > 0)
            {
              /* unread data beyond HTTP headers, back to the stream */
              unread_buf = self->priv->buf->str + pos;

              if (evd_buffered_input_stream_unread (EVD_BUFFERED_INPUT_STREAM (obj),
                                                    unread_buf,
                                                    unread_size,
                                                    NULL,
                                                    &error) >= 0)
                {
                  g_string_set_size (self->priv->buf, pos);
                }
            }

          evd_http_connection_on_read_headers (self, self->priv->buf);

          self->priv->last_headers_pos = 0;
          g_string_set_size (self->priv->buf, 0);
        }
      else if (self->priv->buf->len < MAX_HEADERS_SIZE)
        {
          self->priv->last_headers_pos = self->priv->buf->len - 3;
          evd_http_connection_read_headers_block (self);
        }
      else
        {
          g_set_error_literal (&error,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "HTTP headers are too long");
        }
    }

  if (error != NULL)
    {
      if (self->priv->async_result != NULL)
        {
          GSimpleAsyncResult *res;

          res = self->priv->async_result;
          self->priv->async_result = NULL;
          g_simple_async_result_set_from_error (res, error);
          g_simple_async_result_complete_in_idle (res);
          g_object_unref (res);
        }

      g_error_free (error);
    }

  g_object_unref (self);
}

static void
evd_http_connection_read_headers_block (EvdHttpConnection *self)
{
  GInputStream *stream;
  void *buf;
  gsize new_block_size;

  new_block_size = MIN (MAX_HEADERS_SIZE, self->priv->buf->len + HEADER_BLOCK_SIZE)
    - self->priv->buf->len;

  if (new_block_size <= 0)
    {
      /* @TODO: handle error, max header size reached */
      g_warning ("Unhandled error: max header size reached");
      return;
    }

  g_string_set_size (self->priv->buf,
                     self->priv->buf->len + new_block_size);

  buf = self->priv->buf->str + self->priv->buf->len - new_block_size;

  stream = g_io_stream_get_input_stream (G_IO_STREAM (self));

  g_object_ref (self);
  g_input_stream_read_async (stream,
                             buf,
                             new_block_size,
                             evd_connection_get_priority (EVD_CONNECTION (self)),
                             NULL,
                             evd_http_connection_on_read_headers_block,
                             self);
}

static void
evd_http_connection_read_headers_async (EvdHttpConnection   *self,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data,
                                        gpointer             source_tag)
{
  GError *error = NULL;
  GSimpleAsyncResult *res;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (self));

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   source_tag);

  if (! g_io_stream_set_pending (G_IO_STREAM (self), &error))
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  self->priv->keepalive = FALSE;

  self->priv->async_result = res;

  g_string_set_size (self->priv->buf, 0);

  self->priv->last_headers_pos = 12;
  evd_http_connection_read_headers_block (self);
}

static void
evd_http_connection_read_next_content_block (EvdHttpConnection *self)
{
  gsize new_block_size;
  gchar *buf;

  new_block_size = CONTENT_BLOCK_SIZE;

  if (self->priv->encoding == SOUP_ENCODING_CHUNKED)
    {
      if (self->priv->last_buf_block == NULL)
        self->priv->last_buf_block = g_slice_alloc (CONTENT_BLOCK_SIZE);

      buf = self->priv->last_buf_block;
    }
  else
    {
      if (self->priv->encoding == SOUP_ENCODING_CONTENT_LENGTH)
        new_block_size = MIN (self->priv->content_len - self->priv->content_read,
                              CONTENT_BLOCK_SIZE);

      /* enlarge buffer, if necessary */
      if (self->priv->buf->len < self->priv->content_read + new_block_size)
        g_string_set_size (self->priv->buf,
                           self->priv->content_read + new_block_size);

      buf = self->priv->buf->str + self->priv->buf->len - new_block_size;
    }

  evd_http_connection_read_content_block (self,
                                          buf,
                                          new_block_size);
}

static gboolean
evd_http_connection_process_read_content (EvdHttpConnection  *self,
                                          gsize               size,
                                          gboolean           *done,
                                          GError            **error)
{
  gboolean result = TRUE;

  if (self->priv->encoding == SOUP_ENCODING_CHUNKED)
    {
      gchar outbuf[1024 + 1] = { 0, };
      GConverterResult result;
      gsize total_bytes_read = 0;
      gsize bytes_read = 0;
      gsize bytes_written = 0;

      do
        {
          result =
            g_converter_convert (self->priv->chunked_decoder,
                                 self->priv->last_buf_block + total_bytes_read,
                                 size - total_bytes_read,
                                 outbuf,
                                 1024,
                                 G_CONVERTER_NO_FLAGS,
                                 &bytes_read,
                                 &bytes_written,
                                 error);

          total_bytes_read += bytes_read;

          g_string_append_len (self->priv->buf, outbuf, bytes_written);
          self->priv->content_read += bytes_written;
        }
      while (result != G_CONVERTER_ERROR &&
             result != G_CONVERTER_FINISHED &&
             total_bytes_read < size);

      if (result == G_CONVERTER_FINISHED)
        {
          g_converter_reset (self->priv->chunked_decoder);
          *done = TRUE;
        }
      else if (result == G_CONVERTER_ERROR)
        {
          result = FALSE;

          g_converter_reset (self->priv->chunked_decoder);
          *done = TRUE;
        }
    }
  else
    {
      self->priv->content_read += size;

      /* are we done reading? */
      if (! evd_connection_is_connected (EVD_CONNECTION (self))

          || ( (self->priv->encoding == SOUP_ENCODING_CONTENT_LENGTH
                && self->priv->content_read >= self->priv->content_len)

               && (self->priv->encoding != SOUP_ENCODING_EOF)
               && (self->priv->encoding != SOUP_ENCODING_UNRECOGNIZED)) )
        {
          *done = TRUE;
        }
    }

  return result;
}

static void
evd_http_connection_on_read_content_block (GObject      *obj,
                                           GAsyncResult *res,
                                           gpointer      user_data)
{
  EvdHttpConnection *self = EVD_HTTP_CONNECTION (user_data);
  GError *error = NULL;
  gssize size;
  gboolean done = FALSE;
  gpointer source_tag;

  if ( (size = g_input_stream_read_finish (G_INPUT_STREAM (obj),
                                           res,
                                           &error)) > 0)
    {
      if (! evd_http_connection_process_read_content (self,
                                                      size,
                                                      &done,
                                                      &error))
        {
          g_simple_async_result_set_from_error (self->priv->async_result, error);
          g_error_free (error);
        }
    }
  else if (size == 0)
    {
      done = TRUE;
    }
  else
    {
      g_simple_async_result_set_from_error (self->priv->async_result, error);
      g_error_free (error);

      done = TRUE;
    }

  source_tag =
    g_simple_async_result_get_source_tag (self->priv->async_result);

  if (source_tag == evd_http_connection_read_all_content)
    {
      if (done)
        g_string_set_size (self->priv->buf, self->priv->content_read);
      else
        evd_http_connection_read_next_content_block (self);
    }
  else if (source_tag == evd_http_connection_read_content)
    {
      if (size >= 0)
        {
          struct ContentReadData *data;

          data = g_new0 (struct ContentReadData, 1);
          data->size = size;
          data->more = ! done;

          g_simple_async_result_set_op_res_gpointer (self->priv->async_result,
                                                     data,
                                                     g_free);
        }

      done = TRUE;
    }

  if (done)
    {
      GSimpleAsyncResult *res;

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      g_io_stream_clear_pending (G_IO_STREAM (self));

      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  g_object_unref (self);
}

static void
evd_http_connection_read_content_block (EvdHttpConnection *self,
                                        void              *buf,
                                        gsize              size)
{
  GInputStream *stream;

  stream = g_io_stream_get_input_stream (G_IO_STREAM (self));

  g_object_ref (self);
  g_input_stream_read_async (stream,
                             buf,
                             size,
                             evd_connection_get_priority (EVD_CONNECTION (self)),
                             NULL,
                             evd_http_connection_on_read_content_block,
                             self);
}

static void
evd_http_connection_on_write_request_headers (GObject      *obj,
                                              GAsyncResult *res,
                                              gpointer      user_data)
{
  gssize size;
  GError *error = NULL;
  EvdHttpConnection *self = EVD_HTTP_CONNECTION (user_data);
  GSimpleAsyncResult *_res;

  g_assert (self->priv->async_result != NULL);

  _res = self->priv->async_result;
  self->priv->async_result = NULL;
  g_io_stream_clear_pending (G_IO_STREAM (self));

  size = g_output_stream_write_finish (G_OUTPUT_STREAM (obj),
                                           res,
                                           &error);

  if (size < 0)
    {
      g_simple_async_result_set_from_error (_res, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (_res);
  g_object_unref (_res);
}

static gboolean
evd_http_connection_write_chunk (EvdHttpConnection   *self,
                                 const gchar         *buffer,
                                 gsize                size,
                                 GError            **error)
{
  gchar *chunk_hdr;
  GError *_error = NULL;
  gboolean result = TRUE;

  self->priv->encoding = SOUP_ENCODING_EOF;

  chunk_hdr = g_strdup_printf ("%x\r\n", (guint) size);
  result = evd_http_connection_write_content (self,
                                              chunk_hdr,
                                              strlen (chunk_hdr),
                                              TRUE,
                                              &_error);

  if (result && size > 0)
    result = evd_http_connection_write_content (self,
                                                buffer,
                                                size,
                                                TRUE,
                                                _error == NULL ? &_error : NULL);

  if (result)
    result = evd_http_connection_write_content (self, "\r\n", 2, TRUE,
                                                _error == NULL ? &_error : NULL);

  self->priv->encoding = SOUP_ENCODING_CHUNKED;

  g_free (chunk_hdr);
  if (_error != NULL)
    g_propagate_error (error, _error);

  return result;
}

/* public methods */

EvdHttpConnection *
evd_http_connection_new (EvdSocket *socket)
{
  EvdHttpConnection *self;

  g_return_val_if_fail (EVD_IS_SOCKET (socket), NULL);

  self = g_object_new (EVD_TYPE_HTTP_CONNECTION,
                       "socket", socket,
                       NULL);

  return self;
}

/**
 * evd_http_connection_read_response_headers:
 * @cancellable: (allow-none):
 * @callback: (allow-none):
 * @user_data: (allow-none):
 *
 **/
void
evd_http_connection_read_response_headers (EvdHttpConnection   *self,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
  evd_http_connection_read_headers_async (self,
                               cancellable,
                               callback,
                               user_data,
                               evd_http_connection_read_response_headers);
}

/**
 * evd_http_connection_read_response_headers_finish:
 * @result: The #GAsyncResult object passed to the callback.
 * @version: (out) (allow-none):
 * @status_code: (out) (allow-none):
 * @reason_phrase: (out) (allow-none):
 * @error: (out) (allow-none):
 *
 * Returns: (transfer full) (type Soup.MessageHeaders):
 **/
SoupMessageHeaders *
evd_http_connection_read_response_headers_finish (EvdHttpConnection   *self,
                                                  GAsyncResult        *result,
                                                  SoupHTTPVersion     *version,
                                                  guint               *status_code,
                                                  gchar              **reason_phrase,
                                                  GError             **error)
{
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                               G_OBJECT (self),
                               evd_http_connection_read_response_headers),
                        FALSE);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      struct EvdHttpConnectionResponseHeaders *response;
      SoupMessageHeaders *headers = NULL;

      response =
        g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

      headers = response->headers;
      response->headers = NULL;

      if (version != NULL)
        *version = response->version;

      if (status_code != NULL)
        *status_code = response->status_code;

      if (reason_phrase != NULL)
        {
          *reason_phrase = response->reason_phrase;
          response->reason_phrase = NULL;
        }

      return headers;
    }
  else
    {
      return NULL;
    }
}

/**
 * evd_http_connection_read_request_headers:
 * @cancellable: (allow-none):
 * @callback: (allow-none):
 * @user_data: (allow-none):
 *
 **/
void
evd_http_connection_read_request_headers (EvdHttpConnection   *self,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
  evd_http_connection_read_headers_async (self,
                               cancellable,
                               callback,
                               user_data,
                               evd_http_connection_read_request_headers);
}

/**
 * evd_http_connection_read_request_headers_finish:
 * @result: The #GAsyncResult object passed to the callback.
 * @error: (out) (allow-none):
 *
 * Returns: (transfer full):
 **/
EvdHttpRequest *
evd_http_connection_read_request_headers_finish (EvdHttpConnection  *self,
                                                 GAsyncResult       *result,
                                                 GError            **error)
{
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                               G_OBJECT (self),
                               evd_http_connection_read_request_headers),
                        NULL);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
    }
  else
    {
      return NULL;
    }
}

/**
 * evd_http_connection_write_response_headers:
 * @headers: (type Soup.MessageHeaders) (allow-none):
 * @error: (out) (allow-none):
 *
 **/
gboolean
evd_http_connection_write_response_headers (EvdHttpConnection   *self,
                                            SoupHTTPVersion      version,
                                            guint                status_code,
                                            const gchar         *reason_phrase,
                                            SoupMessageHeaders  *headers,
                                            GError             **error)
{
  GOutputStream *stream;
  gboolean result = TRUE;

  gchar *st;
  GString *buf;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);

  buf = g_string_new ("");

  if (reason_phrase == NULL)
    reason_phrase = soup_status_get_phrase (status_code);

  /* send status line */
  st = g_strdup_printf ("HTTP/1.%d %d %s\r\n",
                        version,
                        status_code,
                        reason_phrase);
  g_string_append_len (buf, st, strlen (st));
  g_free (st);

  /* send headers, if any */
  if (headers != NULL)
    {
      SoupMessageHeadersIter iter;
      const gchar *name;
      const gchar *value;

      soup_message_headers_iter_init (&iter, headers);
      while (soup_message_headers_iter_next (&iter, &name, &value))
        {
          st = g_strdup_printf ("%s: %s\r\n", name, value);
          g_string_append_len (buf, st, strlen (st));
          g_free (st);
        }

      self->priv->encoding = soup_message_headers_get_encoding (headers);
    }
  else
    {
      self->priv->encoding = SOUP_ENCODING_EOF;
    }

  g_string_append_len (buf, "\r\n", 2);

  stream = g_io_stream_get_output_stream (G_IO_STREAM (self));
  if (g_output_stream_write (stream, buf->str, buf->len, NULL, error) < 0)
    result = FALSE;

  g_string_free (buf, TRUE);

  return result;
}

gboolean
evd_http_connection_write_content (EvdHttpConnection  *self,
                                   const gchar        *buffer,
                                   gsize               size,
                                   gboolean            more,
                                   GError            **error)
{
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);

  if (self->priv->encoding == SOUP_ENCODING_CHUNKED)
    {
       if (size == 0 || evd_http_connection_write_chunk (self,
                                                         buffer,
                                                         size,
                                                         error))
        {
          if (! more)
            return evd_http_connection_write_chunk (self,
                                                    NULL,
                                                    0,
                                                    error);
          else
            return TRUE;
        }
      else
        {
          return FALSE;
        }
    }
  else
    {
      GOutputStream *stream;
      gssize size_written;

      stream = g_io_stream_get_output_stream (G_IO_STREAM (self));

      size_written = g_output_stream_write (stream, buffer, size, NULL, error);
      if (size_written < 0)
        {
          return FALSE;
        }
      else if (size_written < size)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_AGAIN,
                       "Resource temporarily unavailable, output buffer full");
          return FALSE;
        }
      else
        {
          return TRUE;
        }
    }
}

/**
 * evd_http_connection_read_content:
 * @cancellable: (allow-none):
 * @callback: (allow-none):
 * @user_data: (allow-none):
 *
 **/
void
evd_http_connection_read_content (EvdHttpConnection   *self,
                                  gchar               *buffer,
                                  gsize                size,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  GError *error = NULL;
  GSimpleAsyncResult *res;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (self));

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_http_connection_read_content);

  if (! g_io_stream_set_pending (G_IO_STREAM (self), &error))
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  if (self->priv->encoding == SOUP_ENCODING_CONTENT_LENGTH &&
      self->priv->content_len == 0)
    {
      g_simple_async_result_set_op_res_gssize (res, 0);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  self->priv->async_result = res;
  evd_http_connection_read_content_block (self, buffer, size);
}

/**
 * evd_http_connection_read_content_finish:
 * @more: (out) (allow-none):
 *
 * Returns:
 **/
gssize
evd_http_connection_read_content_finish (EvdHttpConnection  *self,
                                         GAsyncResult       *result,
                                         gboolean           *more,
                                         GError            **error)
{
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), -1);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                        G_OBJECT (self),
                                        evd_http_connection_read_content),
                        -1);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      struct ContentReadData *data;
      gssize size = 0;
      gboolean _more = FALSE;

      data =
        g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

      if (data != NULL)
        {
          size = data->size;
          _more = data->more;
        }

      if (more != NULL)
        *more = _more;

      return size;
    }
  else
    {
      return -1;
    }
}

/**
 * evd_http_connection_read_all_content:
 * @cancellable: (allow-none):
 * @callback: (allow-none):
 * @user_data: (allow-none):
 *
 **/
void
evd_http_connection_read_all_content (EvdHttpConnection   *self,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  GError *error = NULL;
  GSimpleAsyncResult *res;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (self));

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_http_connection_read_all_content);

  if (! g_io_stream_set_pending (G_IO_STREAM (self), &error))
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  if (self->priv->encoding == SOUP_ENCODING_NONE ||
      (self->priv->encoding == SOUP_ENCODING_CONTENT_LENGTH &&
       self->priv->content_len == 0) )
    {
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  self->priv->content_read = 0;
  g_string_set_size (self->priv->buf, 0);

  self->priv->async_result = res;
  evd_http_connection_read_next_content_block (self);
}

/**
 * evd_http_connection_read_all_content_finish:
 * @size: (out) (allow-none):
 *
 * Returns: (transfer full):
 **/
gchar *
evd_http_connection_read_all_content_finish (EvdHttpConnection  *self,
                                             GAsyncResult       *result,
                                             gssize             *size,
                                             GError            **error)
{
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                               G_OBJECT (self),
                               evd_http_connection_read_all_content),
                        NULL);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      gchar *str = NULL;

      str = self->priv->buf->str;
      if (size != NULL)
        *size = self->priv->content_read;

      g_string_free (self->priv->buf, FALSE);
      self->priv->buf = g_string_new ("");

      return str;
    }
  else
    {
      return NULL;
    }
}

gboolean
evd_http_connection_unread_request_headers (EvdHttpConnection   *self,
                                            EvdHttpRequest      *request,
                                            GError             **error)
{
  GInputStream *stream;
  gboolean result = TRUE;
  gchar *buf;
  gsize size;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (request), FALSE);

  buf = evd_http_request_to_string (request, &size);

  stream = g_io_stream_get_input_stream (G_IO_STREAM (self));
  if (evd_buffered_input_stream_unread (EVD_BUFFERED_INPUT_STREAM (stream),
                                        buf,
                                        size,
                                        NULL,
                                        error) < 0)
    result = FALSE;

  g_free (buf);

  return result;
}

/**
 * evd_http_connection_respond:
 * @reason_phrase: (allow-none):
 * @headers: (allow-none):
 * @content: (allow-none):
 *
 **/
gboolean
evd_http_connection_respond (EvdHttpConnection   *self,
                             SoupHTTPVersion      ver,
                             guint                status_code,
                             const gchar         *reason_phrase,
                             SoupMessageHeaders  *headers,
                             const gchar         *content,
                             gsize                size,
                             gboolean             close_after,
                             GError             **error)
{
  SoupMessageHeaders *_headers;
  gboolean result = FALSE;

  if (headers == NULL)
    _headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
  else
    _headers = headers;

  if (close_after || ! self->priv->keepalive)
    soup_message_headers_replace (_headers, "Connection", "close");
  else
    soup_message_headers_replace (_headers, "Connection", "keep-alive");

  soup_message_headers_set_content_length (_headers, size);

  if (evd_http_connection_write_response_headers (self,
                                                  ver,
                                                  status_code,
                                                  reason_phrase,
                                                  _headers,
                                                  error))
    {
      if (content == NULL ||
          evd_http_connection_write_content (self,
                                             content,
                                             size,
                                             FALSE,
                                             error))
        {
          result = TRUE;
        }
    }

  if (headers == NULL)
    soup_message_headers_free (_headers);

  return result;
}

/**
 * evd_http_connection_respond_simple:
 * @content: (allow-none):
 *
 **/
gboolean
evd_http_connection_respond_simple (EvdHttpConnection   *self,
                                    guint                status_code,
                                    const gchar         *content,
                                    gsize                size)
{
  return evd_http_connection_respond (self,
                                      SOUP_HTTP_1_0,
                                      status_code,
                                      NULL,
                                      NULL,
                                      content,
                                      size,
                                      TRUE,
                                      NULL);
}

/**
 * evd_http_connection_set_current_request:
 * @request: (allow-none):
 *
 **/
void
evd_http_connection_set_current_request (EvdHttpConnection *self,
                                         EvdHttpRequest    *request)
{
  g_return_if_fail (EVD_IS_HTTP_CONNECTION (self));

  if (request == self->priv->current_request)
    return;

  if (self->priv->current_request != NULL)
    g_object_unref (self->priv->current_request);

  self->priv->current_request = request;

  if (self->priv->current_request != NULL)
    g_object_ref (self->priv->current_request);
}

/**
 * evd_http_connection_get_current_request:
 *
 * Returns: (transfer none):
 **/
EvdHttpRequest *
evd_http_connection_get_current_request (EvdHttpConnection *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), NULL);

  return self->priv->current_request;
}

gboolean
evd_http_connection_redirect (EvdHttpConnection  *self,
                              const gchar        *url,
                              gboolean            permanently,
                              GError            **error)
{
  SoupMessageHeaders *headers;
  gboolean result;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);
  g_return_val_if_fail (url != NULL, FALSE);

  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
  soup_message_headers_replace (headers, "Location", url);

  result = evd_http_connection_respond (self,
                                        SOUP_HTTP_1_1,
                                        permanently ?
                                        SOUP_STATUS_MOVED_PERMANENTLY :
                                        SOUP_STATUS_MOVED_TEMPORARILY,
                                        NULL,
                                        headers,
                                        NULL,
                                        0,
                                        TRUE,
                                        error);

  soup_message_headers_free (headers);

  return result;
}

/**
 * evd_http_connection_set_keepalive:
 * @self: The #EvdHttpConnection
 * @keepalive: %TRUE or %FALSE
 *
 * Manually sets the keepalive flag, overriding the internal state obtained from
 * HTTP headers.
 **/
void
evd_http_connection_set_keepalive (EvdHttpConnection *self, gboolean keepalive)
{
  g_return_if_fail (EVD_IS_HTTP_CONNECTION (self));

  self->priv->keepalive = keepalive;
}

gboolean
evd_http_connection_get_keepalive (EvdHttpConnection *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);

  return self->priv->keepalive;
}

/**
 * evd_http_connection_write_request_headers:
 * @cancellable: (allow-none):
 * @callback: (allow-none):
 * @user_data: (allow-none):
 *
 **/
void
evd_http_connection_write_request_headers (EvdHttpConnection   *self,
                                           EvdHttpRequest      *request,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
  GSimpleAsyncResult *res;
  GOutputStream *stream;
  gchar *st;
  gsize size;
  GError *error = NULL;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (self));
  g_return_if_fail (EVD_IS_HTTP_REQUEST (request));

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_http_connection_write_request_headers);

  if (! g_io_stream_set_pending (G_IO_STREAM (self), &error))
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  self->priv->async_result = res;

  st = evd_http_request_to_string (request, &size);

  stream = g_io_stream_get_output_stream (G_IO_STREAM (self));
  g_output_stream_write_async (stream,
                               st,
                               size,
                               G_PRIORITY_DEFAULT,
                               cancellable,
                               evd_http_connection_on_write_request_headers,
                               self);

  g_free (st);
}

gboolean
evd_http_connection_write_request_headers_finish (EvdHttpConnection  *self,
                                                  GAsyncResult       *result,
                                                  GError            **error)
{
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                     G_OBJECT (self),
                                     evd_http_connection_write_request_headers),
                        FALSE);

  return
    ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                             error);
}
