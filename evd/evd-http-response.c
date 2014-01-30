/*
 * evd-http-response.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2013-2014, Igalia S.L.
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
 */

#include <string.h>
#include <libsoup/soup-method.h>

#include "evd-http-response.h"

#include "evd-output-stream.h"

#define EVD_HTTP_RESPONSE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                            EVD_TYPE_HTTP_RESPONSE, \
                                            EvdHttpResponsePrivate))

/* private data */
struct _EvdHttpResponsePrivate
{
  guint status_code;
  gchar *reason_phrase;

  EvdHttpRequest *request;
  SoupHTTPVersion http_version;

  SoupEncoding encoding;

  gboolean headers_sent;
  gboolean done;
};

/* properties */
enum
{
  PROP_0,
  PROP_STATUS_CODE,
  PROP_REASON_PHRASE,
  PROP_REQUEST
};

static void     evd_http_response_class_init           (EvdHttpResponseClass *class);
static void     evd_http_response_init                 (EvdHttpResponse *self);

static void     evd_http_response_finalize             (GObject *obj);
static void     evd_http_response_dispose              (GObject *obj);
static void     evd_http_response_constructed          (GObject *obj);

static void     evd_http_response_set_property         (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void     evd_http_response_get_property         (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static void     output_stream_iface_init               (EvdOutputStreamInterface *iface);

static gssize   output_stream_write                    (EvdOutputStream  *stream,
                                                        const void       *buffer,
                                                        gsize             size,
                                                        GError          **error);

G_DEFINE_TYPE_WITH_CODE (EvdHttpResponse, evd_http_response, EVD_TYPE_HTTP_MESSAGE,
                         G_IMPLEMENT_INTERFACE (EVD_TYPE_OUTPUT_STREAM,
                                                output_stream_iface_init));

static void
evd_http_response_class_init (EvdHttpResponseClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdHttpMessageClass *http_message_class = EVD_HTTP_MESSAGE_CLASS (class);

  obj_class->dispose = evd_http_response_dispose;
  obj_class->finalize = evd_http_response_finalize;
  obj_class->get_property = evd_http_response_get_property;
  obj_class->set_property = evd_http_response_set_property;
  obj_class->constructed = evd_http_response_constructed;

  http_message_class->type = SOUP_MESSAGE_HEADERS_RESPONSE;

  g_object_class_install_property (obj_class, PROP_STATUS_CODE,
                                   g_param_spec_uint ("status-code",
                                                      "Status code",
                                                      "The status code of the HTTP response",
                                                      SOUP_STATUS_NONE,
                                                      G_MAXUINT,
                                                      SOUP_STATUS_NONE,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class,
                                   PROP_REASON_PHRASE,
                                   g_param_spec_string ("reason-phrase",
                                                        "Reason phrase",
                                                        "The reason phrase of the HTTP response",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class,
                                   PROP_REQUEST,
                                   g_param_spec_object ("request",
                                                        "Request",
                                                        "The request object for this response",
                                                        EVD_TYPE_HTTP_REQUEST,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdHttpResponsePrivate));
}

static void
output_stream_iface_init (EvdOutputStreamInterface *iface)
{
  iface->write_fn = output_stream_write;
}

static void
evd_http_response_init (EvdHttpResponse *self)
{
  EvdHttpResponsePrivate *priv;

  priv = EVD_HTTP_RESPONSE_GET_PRIVATE (self);
  self->priv = priv;

  priv->status_code = 0;
  priv->reason_phrase = NULL;

  priv->encoding = SOUP_ENCODING_UNRECOGNIZED;

  priv->headers_sent = FALSE;
  priv->done = FALSE;
}

static void
evd_http_response_dispose (GObject *obj)
{
  EvdHttpResponse *self = EVD_HTTP_RESPONSE (obj);

  if (self->priv->request != NULL)
    {
      g_object_unref (self->priv->request);
      self->priv->request = NULL;
    }

  G_OBJECT_CLASS (evd_http_response_parent_class)->dispose (obj);
}

static void
evd_http_response_finalize (GObject *obj)
{
  EvdHttpResponse *self = EVD_HTTP_RESPONSE (obj);

  g_free (self->priv->reason_phrase);

  G_OBJECT_CLASS (evd_http_response_parent_class)->finalize (obj);
}

static void
evd_http_response_constructed (GObject *obj)
{
  EvdHttpResponse *self = EVD_HTTP_RESPONSE (obj);
  SoupMessageHeaders *headers;
  SoupEncoding encoding;

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (self));
  encoding = soup_message_headers_get_encoding (headers);

  if (encoding == SOUP_ENCODING_EOF)
    soup_message_headers_set_encoding (headers, SOUP_ENCODING_CHUNKED);

  G_OBJECT_CLASS (evd_http_response_parent_class)->constructed (obj);
}

static void
evd_http_response_set_property (GObject      *obj,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EvdHttpResponse *self;

  self = EVD_HTTP_RESPONSE (obj);

  switch (prop_id)
    {
    case PROP_REQUEST:
      self->priv->request = g_value_get_object (value);
      self->priv->http_version =
        evd_http_message_get_version (EVD_HTTP_MESSAGE (self->priv->request));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_http_response_get_property (GObject    *obj,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EvdHttpResponse *self;

  self = EVD_HTTP_RESPONSE (obj);

  switch (prop_id)
    {
    case PROP_STATUS_CODE:
      g_value_set_uint (value, self->priv->status_code);
      break;

    case PROP_REASON_PHRASE:
      g_value_set_string (value, evd_http_response_get_reason_phrase (self));
      break;

    case PROP_REQUEST:
      g_value_set_object (value, self->priv->request);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gssize
write_to_connection (EvdHttpResponse  *self,
                     const void       *buffer,
                     gsize             size,
                     GError          **error)
{
  EvdConnection *conn;
  GOutputStream *conn_stream;

  conn = evd_http_message_get_connection (EVD_HTTP_MESSAGE (self));
  conn_stream = g_io_stream_get_output_stream (G_IO_STREAM (conn));

  return g_output_stream_write (conn_stream,
                                buffer,
                                size,
                                NULL,
                                error);
}

static gssize
write_chunk (EvdHttpResponse   *self,
             const void        *buffer,
             gsize              size,
             GError          **error)
{
  gchar *chunk_hdr;
  GError *_error = NULL;
  gboolean result = TRUE;

  chunk_hdr = g_strdup_printf ("%x\r\n", (guint) size);
  result = write_to_connection (self,
                                (const void *) chunk_hdr,
                                strlen (chunk_hdr),
                                &_error);

  if (result > 0 && size > 0)
    result = write_to_connection (self,
                                  buffer,
                                  size,
                                  _error == NULL ? &_error : NULL);

  if (result)
    result = write_to_connection (self,
                                  "\r\n",
                                  2,
                                  _error == NULL ? &_error : NULL);

  g_free (chunk_hdr);
  if (_error != NULL)
    g_propagate_error (error, _error);

  return size;
}

static gssize
output_stream_write (EvdOutputStream  *stream,
                     const void       *buffer,
                     gsize             size,
                     GError          **error)
{
  EvdHttpResponse *self = EVD_HTTP_RESPONSE (stream);

  if (! self->priv->headers_sent)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Cannot write data, HTTP headers not yet sent");
      return -1;
    }

  if (self->priv->encoding == SOUP_ENCODING_CHUNKED)
    {
      return write_chunk (self, buffer, size, error);
    }
  else if (self->priv->encoding == SOUP_ENCODING_CONTENT_LENGTH ||
           self->priv->encoding == SOUP_ENCODING_EOF)
    {
      /* @TODO: Raise warning if writing beyond content boundaries */

      return write_to_connection (self, buffer, size, error);
    }
  else
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "Unsupported transfer encoding in HTTP response");
      return -1;
    }
}

static void
on_done_and_flushed (GObject      *obj,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  // EvdHttpResponse *self = EVD_HTTP_RESPONSE (obj);
  GError *error = NULL;

  if (! evd_output_stream_flush_finish (EVD_OUTPUT_STREAM (obj),
                                        res,
                                        &error))
    {
      g_print ("Error flushing connection after HTTP response was sent: %s\n", error->message);
      g_error_free (error);

      /* @TODO: mark the response as erroneous or uncompleted so that the
         underlying connection is not kept alive by any Web service */
    }

  /* @TODO: fire a 'finished' or 'done' signal */
}

/* public methods */

/**
 * evd_http_response_new:
 *
 * Returns: (transfer full): A new #EvdHttpResponse
 **/
EvdHttpResponse *
evd_http_response_new (EvdHttpRequest *request)
{
  EvdHttpResponse *self;
  EvdConnection *conn;

  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (request), NULL);

  conn = evd_http_message_get_connection (EVD_HTTP_MESSAGE (request));

  self = g_object_new (EVD_TYPE_HTTP_RESPONSE,
                       "connection", conn,
                       "request", request,
                       NULL);
  return self;
}

const gchar *
evd_http_response_get_reason_phrase (EvdHttpResponse *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_RESPONSE (self), NULL);

  return self->priv->reason_phrase;
}

guint
evd_http_response_get_status_code (EvdHttpResponse *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_RESPONSE (self), 0);

  return self->priv->status_code;
}

/**
 * evd_http_response_get_request:
 *
 * Returns: (transfer none):
 **/
EvdHttpRequest *
evd_http_response_get_request (EvdHttpResponse *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_RESPONSE (self), NULL);

  return self->priv->request;
}

gboolean
evd_http_response_write_headers (EvdHttpResponse  *self,
                                 guint             status_code,
                                 const gchar      *reason_phrase,
                                 GError          **error)
{
  GString *buf;
  gchar *st;
  gboolean result = TRUE;
  SoupMessageHeaders *headers;

  g_return_val_if_fail (EVD_IS_HTTP_RESPONSE (self), FALSE);

  if (self->priv->headers_sent)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_EXISTS,
                   "Reponse headers already sent");
      return FALSE;
    }

  if (reason_phrase == NULL)
    reason_phrase = soup_status_get_phrase (status_code);

  self->priv->status_code = status_code;
  self->priv->reason_phrase = g_strdup (reason_phrase);

  buf = g_string_new ("");

  /* send status line */
  st = g_strdup_printf ("HTTP/1.%d %d %s\r\n",
                        self->priv->http_version,
                        status_code,
                        reason_phrase);
  g_string_append_len (buf, st, strlen (st));
  g_free (st);

  /* send headers, if any */
  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (self));
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

      self->priv->encoding =
        soup_message_headers_get_encoding (headers);
    }
  else
    {
      self->priv->encoding = SOUP_ENCODING_CHUNKED;
    }

  g_string_append_len (buf, "\r\n", 2);

  self->priv->headers_sent = TRUE;

  if (write_to_connection (self, buf->str, buf->len, error) < 0)
    result = FALSE;

  g_string_free (buf, TRUE);

  return result;
}

void
evd_http_response_done (EvdHttpResponse *self)
{
  g_return_if_fail (EVD_IS_HTTP_RESPONSE (self));

  self->priv->done = TRUE;

  evd_output_stream_flush (EVD_OUTPUT_STREAM (self),
                           NULL,
                           on_done_and_flushed,
                           NULL);
}
