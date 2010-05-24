/*
 * evd-tls-output-stream.c
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

#include <evd-tls-session.h>

#include "evd-tls-output-stream.h"

G_DEFINE_TYPE (EvdTlsOutputStream, evd_tls_output_stream, G_TYPE_FILTER_OUTPUT_STREAM)

#define EVD_TLS_OUTPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                EVD_TYPE_TLS_OUTPUT_STREAM, \
                                                EvdTlsOutputStreamPrivate))

/* private data */
struct _EvdTlsOutputStreamPrivate
{
  EvdTlsSession *session;
  GString *buf;
};

/* properties */
enum
{
  PROP_0,
  PROP_SESSION
};

static void     evd_tls_output_stream_class_init         (EvdTlsOutputStreamClass *class);
static void     evd_tls_output_stream_init               (EvdTlsOutputStream *self);
static void     evd_tls_output_stream_finalize           (GObject *obj);

static void     evd_tls_output_stream_set_property       (GObject      *obj,
                                                          guint         prop_id,
                                                          const GValue *value,
                                                          GParamSpec   *pspec);
static void     evd_tls_output_stream_get_property       (GObject    *obj,
                                                          guint       prop_id,
                                                          GValue     *value,
                                                          GParamSpec *pspec);

static gssize   evd_tls_output_stream_write              (GOutputStream  *stream,
                                                          const void     *buffer,
                                                          gsize          size,
                                                          GCancellable  *cancellable,
                                                          GError       **error);

static void
evd_tls_output_stream_class_init (EvdTlsOutputStreamClass *class)
{
  GObjectClass *obj_class;
  GOutputStreamClass *output_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_tls_output_stream_finalize;
  obj_class->get_property = evd_tls_output_stream_get_property;
  obj_class->set_property = evd_tls_output_stream_set_property;

  output_stream_class = G_OUTPUT_STREAM_CLASS (class);
  output_stream_class->write_fn = evd_tls_output_stream_write;

   g_object_class_install_property (obj_class, PROP_SESSION,
				   g_param_spec_object ("session",
							"The TLS session",
							"The TLS session associated with this stream",
							EVD_TYPE_TLS_SESSION,
                                                        G_PARAM_CONSTRUCT_ONLY |
							G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  g_type_class_add_private (obj_class, sizeof (EvdTlsOutputStreamPrivate));
}

static void
evd_tls_output_stream_init (EvdTlsOutputStream *self)
{
  EvdTlsOutputStreamPrivate *priv;

  priv = EVD_TLS_OUTPUT_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  priv->buf = g_string_new ("");
}

static void
evd_tls_output_stream_finalize (GObject *obj)
{
  EvdTlsOutputStream *self = EVD_TLS_OUTPUT_STREAM (obj);

  g_string_free (self->priv->buf, TRUE);

  G_OBJECT_CLASS (evd_tls_output_stream_parent_class)->finalize (obj);
}

static void
evd_tls_output_stream_set_property (GObject      *obj,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  EvdTlsOutputStream *self;

  self = EVD_TLS_OUTPUT_STREAM (obj);

  switch (prop_id)
    {
    case PROP_SESSION:
      self->priv->session = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_tls_output_stream_get_property (GObject    *obj,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  EvdTlsOutputStream *self;

  self = EVD_TLS_OUTPUT_STREAM (obj);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, self->priv->session);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gssize
evd_tls_output_stream_real_write (EvdTlsOutputStream  *self,
                                  const gchar         *buffer,
                                  gsize                size,
                                  GError             **error)
{
  GOutputStream *base_stream;

  base_stream =
    g_filter_output_stream_get_base_stream (G_FILTER_OUTPUT_STREAM (self));

  return g_output_stream_write (base_stream, buffer, size, NULL, error);
}

static gssize
evd_tls_output_stream_push (EvdTlsSession  *session,
                            const gchar    *buffer,
                            gsize           size,
                            gpointer        user_data,
                            GError        **error)
{
  EvdTlsOutputStream *self = EVD_TLS_OUTPUT_STREAM (user_data);
  gssize result;

  /* if we already have data pending to be pushed, just
     append new data to the tail of the buffer */
  if (self->priv->buf->len > 0)
    {
      g_string_append_len (self->priv->buf,
                           buffer,
                           size);

      return size;
    }

  result = evd_tls_output_stream_real_write (self,
                                             buffer,
                                             size,
                                             error);

  if (result >= 0 && result < size)
    {
      g_string_append_len (self->priv->buf,
                           (const gchar *) ( (guintptr) buffer + result),
                           size - result);

      if (! g_output_stream_has_pending (G_OUTPUT_STREAM (self)) &&
          ! g_output_stream_set_pending (G_OUTPUT_STREAM (self), error))
        {
          result = -1;
        }
      else
        {
          result = size;
        }
    }
  else
    {
      g_output_stream_clear_pending (G_OUTPUT_STREAM (self));
    }

  return result;
}

static gssize
evd_tls_output_stream_write (GOutputStream  *stream,
                             const void     *buffer,
                             gsize          size,
                             GCancellable  *cancellable,
                             GError       **error)
{
  EvdTlsOutputStream *self = EVD_TLS_OUTPUT_STREAM (stream);

  return evd_tls_session_write (self->priv->session,
                                buffer,
                                size,
                                error);
}

/* public methods */

EvdTlsOutputStream *
evd_tls_output_stream_new (EvdTlsSession *session,
                           GOutputStream *base_stream)
{
  EvdTlsOutputStream *self;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (session), NULL);
  g_return_val_if_fail (G_IS_OUTPUT_STREAM (base_stream), NULL);

  self = g_object_new (EVD_TYPE_TLS_OUTPUT_STREAM,
                       "session", session,
                       "base-stream", base_stream,
                       NULL);

  evd_tls_session_set_transport_push_func (session,
                                           evd_tls_output_stream_push,
                                           self);

  return self;
}

void
evd_tls_output_stream_notify_write (EvdTlsOutputStream *self,
                                    gint                priority)
{
  g_return_if_fail (EVD_IS_TLS_OUTPUT_STREAM (self));

  if (self->priv->buf->len > 0)
    {
      gssize actual_size;
      GError *error = NULL;

      actual_size = evd_tls_output_stream_real_write (self,
                                                      self->priv->buf->str,
                                                      self->priv->buf->len,
                                                      &error);

      if (actual_size > 0)
        {
          g_string_erase (self->priv->buf, 0, actual_size);

          if (self->priv->buf->len == 0)
            g_output_stream_clear_pending (G_OUTPUT_STREAM (self));
        }
    }
}
