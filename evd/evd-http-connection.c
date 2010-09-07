/*
 * evd-http-connection.c
 *
 * EventDance - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009/2010, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 *
 */

#include <string.h>

#include "evd-http-connection.h"

#include "evd-error.h"
#include "evd-buffered-input-stream.h"

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
  GString *block;

  gint priority;

  gint last_headers_pos;

  SoupHTTPVersion http_ver;

  goffset content_len;
  SoupEncoding encoding;
  gsize content_read;
};

/* properties */
enum
{
  PROP_0
};

struct EvdHttpConnectionRequestHeaders
{
  SoupMessageHeaders *headers;
  SoupHTTPVersion version;
  gchar *method;
  gchar *path;
};

struct EvdHttpConnectionResponseHeaders
{
  SoupMessageHeaders *headers;
  SoupHTTPVersion version;
  guint status_code;
  gchar *reason_phrase;
};

static void     evd_http_connection_class_init         (EvdHttpConnectionClass *class);
static void     evd_http_connection_init               (EvdHttpConnection *self);

static void     evd_http_connection_finalize           (GObject *obj);
static void     evd_http_connection_dispose            (GObject *obj);

static void     evd_http_connection_set_property       (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void     evd_http_connection_get_property       (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static gboolean evd_http_connection_close              (GIOStream     *stream,
                                                        GCancellable  *cancellable,
                                                        GError       **error);

static void     evd_http_connection_read_headers_block (EvdHttpConnection *self);

static void     evd_http_connection_read_content_block (EvdHttpConnection *self);


static void
evd_http_connection_class_init (EvdHttpConnectionClass *class)
{
  GObjectClass *obj_class;
  GIOStreamClass *io_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_http_connection_dispose;
  obj_class->finalize = evd_http_connection_finalize;
  obj_class->get_property = evd_http_connection_get_property;
  obj_class->set_property = evd_http_connection_set_property;

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
}

static void
evd_http_connection_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_http_connection_parent_class)->dispose (obj);
}

static void
evd_http_connection_finalize (GObject *obj)
{
  EvdHttpConnection *self = EVD_HTTP_CONNECTION (obj);

  g_string_free (self->priv->buf, TRUE);

  G_OBJECT_CLASS (evd_http_connection_parent_class)->finalize (obj);
}

static void
evd_http_connection_set_property (GObject      *obj,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EvdHttpConnection *self;

  self = EVD_HTTP_CONNECTION (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_http_connection_get_property (GObject    *obj,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EvdHttpConnection *self;

  self = EVD_HTTP_CONNECTION (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
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
                                       EVD_ERROR,
                                       EVD_ERROR_CLOSED,
                                       "Connection closed during async operation");
      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  return result;
}

static void
evd_http_connection_request_headers_destroy (gpointer data)
{
  struct EvdHttpConnectionRequestHeaders *request;

  request = (struct EvdHttpConnectionRequestHeaders *) data;

  if (request->headers != NULL)
    soup_message_headers_free (request->headers);
  if (request->method != NULL)
    g_free (request->method);
  if (request->path != NULL)
    g_free (request->path);

  g_free (request);
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

  if (source_tag == evd_http_connection_read_request_headers_async)
    {
      struct EvdHttpConnectionRequestHeaders *response;

      response = g_new0 (struct EvdHttpConnectionRequestHeaders, 1);
      response->headers =
        soup_message_headers_new (SOUP_MESSAGE_HEADERS_REQUEST);

      if (soup_headers_parse_request (buf->str,
                                      buf->len - 2,
                                      response->headers,
                                      &response->method,
                                      &response->path,
                                      &response->version))
        {
          g_simple_async_result_set_op_res_gpointer (res,
                                  response,
                                  evd_http_connection_request_headers_destroy);

          self->priv->encoding =
            soup_message_headers_get_encoding (response->headers);
          self->priv->content_len =
            soup_message_headers_get_content_length (response->headers);
        }
      else
        {
          g_simple_async_result_set_error (res,
                                           EVD_ERROR,
                                           EVD_ERROR_INVALID_DATA,
                                           "Failed to parse HTTP request headers");
        }
    }
  else if (source_tag == evd_http_connection_read_response_headers_async)
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
                                           EVD_ERROR,
                                           EVD_ERROR_INVALID_DATA,
                                           "Failed to parse HTTP response headers");
        }
    }

  g_simple_async_result_complete_in_idle (res);
  g_object_unref (res);
}

static gint
evd_http_connection_find_end_headers_mark (const GString *buf,
                                           gint          last_pos)
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

          /* unread data beyond HTTP headers, back to the stream */
          unread_buf = (void *) ( ((guintptr) self->priv->buf->str) + pos);
          unread_size = self->priv->buf->len - pos;

          if (evd_buffered_input_stream_unread (EVD_BUFFERED_INPUT_STREAM (obj),
                                                unread_buf,
                                                unread_size,
                                                NULL,
                                                &error) >= 0)
            {
              g_string_set_size (self->priv->buf, pos);

              evd_http_connection_on_read_headers (self, self->priv->buf);
            }

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
                               EVD_ERROR,
                               EVD_ERROR_TOO_LONG,
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

  /* enlarge buffer */
  g_string_set_size (self->priv->buf,
                     self->priv->buf->len + HEADER_BLOCK_SIZE);

  buf = (void *) ( ((guintptr) self->priv->buf->str)
                   + self->priv->buf->len - new_block_size);

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
                                        GAsyncReadyCallback callback,
                                        gpointer            user_data,
                                        gpointer            source_tag)
{
  GError *error = NULL;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (self));

  if (! g_io_stream_set_pending (G_IO_STREAM (self), &error))
    {
      GSimpleAsyncResult *res;

      res = g_simple_async_result_new (G_OBJECT (self),
                                       callback,
                                       user_data,
                                       source_tag);
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  self->priv->async_result =
    g_simple_async_result_new (G_OBJECT (self),
                               callback,
                               user_data,
                               source_tag);

  self->priv->last_headers_pos = 12;
  evd_http_connection_read_headers_block (self);
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

  if ( (size = g_input_stream_read_finish (G_INPUT_STREAM (obj),
                                           res,
                                           &error)) > 0)
    {
      self->priv->content_read += size;

      /* continue reading? */
      if ( evd_connection_is_connected (EVD_CONNECTION (self)) &&

           ( (self->priv->encoding == SOUP_ENCODING_CONTENT_LENGTH &&
              self->priv->content_read < self->priv->content_len) ||

             (self->priv->encoding == SOUP_ENCODING_EOF) ) )
        {
          evd_http_connection_read_content_block (self);
        }
      else
        {
          done = TRUE;
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

  if (done)
    {
      GSimpleAsyncResult *res;

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      g_io_stream_clear_pending (G_IO_STREAM (self));

      g_string_set_size (self->priv->buf, self->priv->content_read);

      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  g_object_unref (self);
}

static void
evd_http_connection_read_content_block (EvdHttpConnection *self)
{
  GInputStream *stream;
  void *buf;
  gsize new_block_size;

  if (self->priv->encoding == SOUP_ENCODING_CONTENT_LENGTH)
    new_block_size = MIN (self->priv->content_len - self->priv->content_read,
                          CONTENT_BLOCK_SIZE);
  else
    new_block_size = CONTENT_BLOCK_SIZE;

  /* enlarge buffer, if necessary */
  if (self->priv->buf->len < self->priv->content_read + new_block_size)
    g_string_set_size (self->priv->buf,
                       self->priv->content_read + new_block_size);

  buf = (void *) ( ((guintptr) self->priv->buf->str)
                   + self->priv->buf->len - new_block_size);

  stream = g_io_stream_get_input_stream (G_IO_STREAM (self));

  g_object_ref (self);
  g_input_stream_read_async (stream,
                             buf,
                             new_block_size,
                             evd_connection_get_priority (EVD_CONNECTION (self)),
                             NULL,
                             evd_http_connection_on_read_content_block,
                             self);
}

static void
evd_http_connection_shutdown_on_flush (GObject      *obj,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  EvdConnection *conn = EVD_CONNECTION (user_data);

  g_output_stream_flush_finish (G_OUTPUT_STREAM (obj), res, NULL);

  evd_socket_shutdown (evd_connection_get_socket (conn),
                       TRUE,
                       TRUE,
                       NULL);

  g_object_unref (conn);
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

void
evd_http_connection_read_response_headers_async (EvdHttpConnection   *self,
                                                 GCancellable        *cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer            user_data)
{
  evd_http_connection_read_headers_async (self,
                               cancellable,
                               callback,
                               user_data,
                               evd_http_connection_read_response_headers_async);
}

/**
 * evd_http_connection_read_response_headers_finish:
 * @result: The #GAsyncResult object passed to the callback.
 * @version: (out):
 * @status_code: (out):
 * @reason_phrase: (out):
 * @error:
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
                               evd_http_connection_read_response_headers_async),
                        FALSE);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      struct EvdHttpConnectionResponseHeaders *response;
      SoupMessageHeaders *headers = NULL;

      response =
        g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

      headers = response->headers;

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

void
evd_http_connection_read_request_headers_async (EvdHttpConnection   *self,
                                                GCancellable        *cancellable,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data)
{
  evd_http_connection_read_headers_async (self,
                               cancellable,
                               callback,
                               user_data,
                               evd_http_connection_read_request_headers_async);
}

/**
 * evd_http_connection_read_request_headers_finish:
 * @result: The #GAsyncResult object passed to the callback.
 * @version: (out):
 * @method: (out):
 * @path: (out):
 * @error:
 *
 * Returns: (transfer full) (type Soup.MessageHeaders):
 **/
SoupMessageHeaders *
evd_http_connection_read_request_headers_finish (EvdHttpConnection   *self,
                                                 GAsyncResult        *result,
                                                 SoupHTTPVersion     *version,
                                                 gchar              **method,
                                                 gchar              **path,
                                                 GError             **error)
{
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                               G_OBJECT (self),
                               evd_http_connection_read_request_headers_async),
                        NULL);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      struct EvdHttpConnectionRequestHeaders *response;
      SoupMessageHeaders *headers = NULL;

      response =
        g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

      if (response->headers != NULL)
        {
          headers = response->headers;
          response->headers = NULL;
        }

      if (version != NULL)
        *version = response->version;

      if (method != NULL)
        {
          *method = response->method;
          response->method = NULL;
        }

      if (path != NULL)
        {
          *path = response->path;
          response->path = NULL;
        }

      return headers;
    }
  else
    {
      return NULL;
    }
}

/**
 * evd_http_connection_write_response_headers:
 * @headers: (type Soup.MessageHeaders) (allow-none):
 *
 **/
gboolean
evd_http_connection_write_response_headers (EvdHttpConnection   *self,
                                            SoupHTTPVersion      version,
                                            guint                status_code,
                                            gchar               *reason_phrase,
                                            SoupMessageHeaders  *headers,
                                            GCancellable        *cancellable,
                                            GError             **error)
{
  GOutputStream *stream;
  gboolean result = TRUE;

  gchar *st;
  GString *buf;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);

  buf = g_string_new ("");

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
                                   GCancellable       *cancellable,
                                   GError            **error)
{
  GOutputStream *stream;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);

  if (size == 0)
    return TRUE;

  /* @TODO: here we apply necessary transformation to buffer, depending
     on current state of connection (e.g chunked transfer, gzipped, etc) */

  stream = g_io_stream_get_output_stream (G_IO_STREAM (self));

  return g_output_stream_write (stream, buffer, size, cancellable, error);
}

void
evd_http_connection_read_all_content_async (EvdHttpConnection   *self,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
  GError *error = NULL;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (self));

  if (! g_io_stream_set_pending (G_IO_STREAM (self), &error))
    {
      GSimpleAsyncResult *res;

      res = g_simple_async_result_new (G_OBJECT (self),
                                    callback,
                                    user_data,
                                    evd_http_connection_read_all_content_async);
      g_simple_async_result_set_from_error (res, error);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  if (self->priv->encoding == SOUP_ENCODING_NONE ||
      (self->priv->encoding == SOUP_ENCODING_CONTENT_LENGTH &&
       self->priv->content_len == 0) )
    {
      GSimpleAsyncResult *res;

      res =
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_http_connection_read_all_content_async);
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  /* @TODO: consider chunked encoding */
  if (self->priv->encoding == SOUP_ENCODING_UNRECOGNIZED ||
      self->priv->encoding == SOUP_ENCODING_CHUNKED)
    self->priv->encoding = SOUP_ENCODING_EOF;

  self->priv->content_read = 0;
  g_string_set_size (self->priv->buf, 0);

  self->priv->async_result =
    g_simple_async_result_new (G_OBJECT (self),
                               callback,
                               user_data,
                               evd_http_connection_read_all_content_async);

  evd_http_connection_read_content_block (self);
}

/**
 * evd_http_connection_read_all_content_finish:
 * @size: (out):
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
                               evd_http_connection_read_all_content_async),
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
                                            SoupHTTPVersion      version,
                                            const gchar         *method,
                                            const gchar         *path,
                                            SoupMessageHeaders  *headers,
                                            GCancellable        *cancellable,
                                            GError             **error)
{
  GInputStream *stream;
  gboolean result = TRUE;

  gchar *st;
  GString *buf;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (self), FALSE);

  buf = g_string_new ("");

  /* send status line */
  st = g_strdup_printf ("%s %s HTTP/1.%d\r\n",
                        method,
                        path,
                        version);
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
    }

  g_string_append_len (buf, "\r\n", 2);

  stream = g_io_stream_get_input_stream (G_IO_STREAM (self));
  if (evd_buffered_input_stream_unread (EVD_BUFFERED_INPUT_STREAM (stream),
                                        buf->str,
                                        buf->len,
                                        NULL,
                                        error) < 0)
    result = FALSE;

  g_string_free (buf, TRUE);

  return result;
}

gboolean
evd_http_connection_respond (EvdHttpConnection   *self,
                             SoupHTTPVersion      ver,
                             guint                status_code,
                             gchar               *reason_phrase,
                             SoupMessageHeaders  *headers,
                             const gchar         *content,
                             gsize                size,
                             gboolean             close_after,
                             GCancellable        *cancellable,
                             GError             **error)
{
  SoupMessageHeaders *_headers;
  gboolean result = FALSE;

  if (headers == NULL)
    _headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
  else
    _headers = headers;

  if (close_after)
    soup_message_headers_replace (_headers, "Connection", "close");

  if (content != NULL)
    soup_message_headers_set_content_length (_headers, size);

  if (evd_http_connection_write_response_headers (self,
                                                  ver,
                                                  status_code,
                                                  reason_phrase,
                                                  _headers,
                                                  cancellable,
                                                  error))
    {
      if (content == NULL ||
          evd_http_connection_write_content (self,
                                             content,
                                             size,
                                             cancellable,
                                             error))
        {
          GOutputStream *stream;

          stream = g_io_stream_get_output_stream (G_IO_STREAM (self));

          g_object_ref (self);
          g_output_stream_flush_async (stream,
                            evd_connection_get_priority (EVD_CONNECTION (self)),
                            cancellable,
                            evd_http_connection_shutdown_on_flush,
                            self);

          result = TRUE;
        }
    }

  if (headers == NULL)
    soup_message_headers_free (_headers);

  return result;
}
