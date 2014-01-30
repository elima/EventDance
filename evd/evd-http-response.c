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
  EvdConnection *conn;
  guint status_code;
  gchar *reason_phrase;
  SoupMessageHeaders *headers;

  EvdHttpRequest *request;

  SoupEncoding encoding;
};

/* properties */
enum
{
  PROP_0,
  PROP_CONNECTION,
  PROP_STATUS_CODE,
  PROP_REASON_PHRASE,
  PROP_HEADERS,
  PROP_REQUEST
};

static void     evd_http_response_class_init           (EvdHttpResponseClass *class);
static void     evd_http_response_init                 (EvdHttpResponse *self);

static void     evd_http_response_finalize             (GObject *obj);
static void     evd_http_response_dispose              (GObject *obj);

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

  stream_class->write_fn = write_fn;

  g_object_class_install_property (obj_class,
                                   PROP_CONNECTION,
                                   g_param_spec_object ("connection",
                                                        "Connection",
                                                        "The TCP connection used by the HTTP response",
                                                        EVD_TYPE_CONNECTION,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
  http_message_class->type = SOUP_MESSAGE_HEADERS_RESPONSE;

  g_object_class_install_property (obj_class, PROP_STATUS_CODE,
                                   g_param_spec_uint ("status-code",
                                                      "Status code",
                                                      "The status code of the HTTP response",
                                                      SOUP_STATUS_NONE,
                                                      G_MAXUINT,
                                                      SOUP_STATUS_OK,
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
                                   PROP_HEADERS,
                                   g_param_spec_boxed ("headers",
                                                       "HTTP headers",
                                                       "The HTTP response headers",
                                                       SOUP_TYPE_MESSAGE_HEADERS,
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
}

static void
evd_http_response_init (EvdHttpResponse *self)
{
  EvdHttpResponsePrivate *priv;

  priv = EVD_HTTP_RESPONSE_GET_PRIVATE (self);
  self->priv = priv;

  priv->status_code = 0;
  priv->reason_phrase = NULL;
  priv->headers = NULL;

  priv->encoding = SOUP_ENCODING_UNRECOGNIZED;
}

static void
evd_http_response_dispose (GObject *obj)
{
  EvdHttpResponse *self = EVD_HTTP_RESPONSE (obj);

  if (self->priv->conn != NULL)
    {
      g_object_unref (self->priv->conn);
      self->priv->conn = NULL;
    }

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

  if (self->priv->headers != NULL)
    soup_message_headers_free (self->priv->headers);

  G_OBJECT_CLASS (evd_http_response_parent_class)->finalize (obj);
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
    case PROP_CONNECTION:
      self->priv->conn = g_value_get_object (value);
      break;

    case PROP_HEADERS:
      self->priv->headers = g_value_get_boxed (value);
      break;

    case PROP_REQUEST:
      self->priv->request = g_value_get_object (value);
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
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->conn);
      break;

    case PROP_STATUS_CODE:
      g_value_set_uint (value, self->priv->status_code);
      break;

    case PROP_REASON_PHRASE:
      g_value_set_string (value, evd_http_response_get_reason_phrase (self));
      break;

    case PROP_HEADERS:
      g_value_set_boxed (value, self->priv->headers);
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
                     gsize             count,
                     GCancellable     *cancellable,
                     GError          **error)
{
  GOutputStream *conn_stream;

  conn_stream = g_io_stream_get_output_stream (G_IO_STREAM (self->priv->conn));

  return g_output_stream_write (conn_stream,
                                buffer,
                                count,
                                cancellable,
                                error);
}

static gssize
write_fn (GOutputStream  *stream,
          const void     *buffer,
          gsize           count,
          GCancellable   *cancellable,
          GError        **error)
{
  /* @TODO: check that headers have been sent.
     Raise warning if writing beyond content boundaries. */

  return write_to_connection (EVD_HTTP_RESPONSE (stream),
                              buffer,
                              count,
                              cancellable,
                              error);
}

/* public methods */

EvdHttpResponse *
evd_http_response_new (EvdConnection *conn, EvdHttpRequest *request)
{
  EvdHttpResponse *self;

  g_return_val_if_fail (EVD_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (conn), NULL);

  self = g_object_new (EVD_TYPE_HTTP_RESPONSE,
                       "connection", conn,
                       "request", request,
                       NULL);

  return self;
}

/**
 * evd_http_response_get_connection:
 *
 * Returns: (transfer none):
 **/
EvdConnection *
evd_http_response_get_connection (EvdHttpResponse *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_RESPONSE (self), NULL);

  return self->priv->conn;
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
  SoupHTTPVersion version;
  gboolean result = TRUE;

  g_return_val_if_fail (EVD_IS_HTTP_RESPONSE (self), FALSE);

  /* @TODO: check that headers have not been already sent */

  buf = g_string_new ("");

  if (reason_phrase == NULL)
    reason_phrase = soup_status_get_phrase (status_code);

  version =
    evd_http_message_get_version (EVD_HTTP_MESSAGE (self->priv->request));

  /* send status line */
  st = g_strdup_printf ("HTTP/1.%d %d %s\r\n",
                        version,
                        status_code,
                        reason_phrase);
  g_string_append_len (buf, st, strlen (st));
  g_free (st);

  /* send headers, if any */
  if (self->priv->headers != NULL)
    {
      SoupMessageHeadersIter iter;
      const gchar *name;
      const gchar *value;

      soup_message_headers_iter_init (&iter, self->priv->headers);
      while (soup_message_headers_iter_next (&iter, &name, &value))
        {
          st = g_strdup_printf ("%s: %s\r\n", name, value);
          g_string_append_len (buf, st, strlen (st));
          g_free (st);
        }

      self->priv->encoding =
        soup_message_headers_get_encoding (self->priv->headers);
    }
  else
    {
      self->priv->encoding = SOUP_ENCODING_EOF;
    }

  g_string_append_len (buf, "\r\n", 2);

  if (write_to_connection (self, buf->str, buf->len, NULL, error) < 0)
    result = FALSE;

  g_string_free (buf, TRUE);

  return result;
}
