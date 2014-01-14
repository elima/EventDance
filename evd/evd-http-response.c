/*
 * evd-http-response.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2013, Igalia S.L.
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

#include <libsoup/soup-method.h>

#include "evd-http-response.h"

#define EVD_HTTP_RESPONSE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                            EVD_TYPE_HTTP_RESPONSE, \
                                            EvdHttpResponsePrivate))

G_DEFINE_TYPE (EvdHttpResponse, evd_http_response, G_TYPE_OUTPUT_STREAM)

/* private data */
struct _EvdHttpResponsePrivate
{
  EvdConnection *conn;
  guint status_code;
  gchar *reason_phrase;
};

/* properties */
enum
{
  PROP_0,
  PROP_CONNECTION,
  PROP_STATUS_CODE,
  PROP_REASON_PHRASE
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
static gssize   write_fn                               (GOutputStream  *stream,
                                                        const void     *buffer,
                                                        gsize           count,
                                                        GCancellable   *cancellable,
                                                        GError        **error);

static void
evd_http_response_class_init (EvdHttpResponseClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  GOutputStreamClass *stream_class = G_OUTPUT_STREAM_CLASS (class);

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

  g_object_class_install_property (obj_class, PROP_STATUS_CODE,
                                   g_param_spec_uint ("status-code",
                                                      "Status code",
                                                      "The status code of the HTTP response",
                                                      SOUP_STATUS_NONE,
                                                      G_MAXUINT,
                                                      SOUP_STATUS_OK,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_REASON_PHRASE,
                                   g_param_spec_string ("reason-phrase",
                                                        "Reason phrase",
                                                        "The reason phrase of the HTTP response",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdHttpResponsePrivate));
}

static void
evd_http_response_init (EvdHttpResponse *self)
{
  EvdHttpResponsePrivate *priv;

  priv = EVD_HTTP_RESPONSE_GET_PRIVATE (self);
  self->priv = priv;
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

  G_OBJECT_CLASS (evd_http_response_parent_class)->dispose (obj);
}

static void
evd_http_response_finalize (GObject *obj)
{
  EvdHttpResponse *self = EVD_HTTP_RESPONSE (obj);

  if (self->priv->reason_phrase != NULL)
    g_free (self->priv->reason_phrase);

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

    case PROP_REASON_PHRASE:
      evd_http_response_set_reason_phrase (self, g_value_dup_string (value));
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
evd_http_response_new (void)
{
  EvdHttpResponse *self;

  self = g_object_new (EVD_TYPE_HTTP_RESPONSE, NULL);

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

void
evd_http_response_set_status_code (EvdHttpResponse *self,
                                   guint            status_code)
{
  g_return_if_fail (EVD_IS_HTTP_RESPONSE (self));

  self->priv->status_code = status_code;
}

guint
evd_http_response_get_status_code (EvdHttpResponse *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_RESPONSE (self), 0);

  return self->priv->status_code;
}
