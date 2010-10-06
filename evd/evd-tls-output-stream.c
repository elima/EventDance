/*
 * evd-tls-output-stream.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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

#include "evd-tls-output-stream.h"

G_DEFINE_TYPE (EvdTlsOutputStream, evd_tls_output_stream, EVD_TYPE_BUFFERED_OUTPUT_STREAM)

#define EVD_TLS_OUTPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                EVD_TYPE_TLS_OUTPUT_STREAM, \
                                                EvdTlsOutputStreamPrivate))

/* private data */
struct _EvdTlsOutputStreamPrivate
{
  EvdTlsSession *session;
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
}

static void
evd_tls_output_stream_finalize (GObject *obj)
{
  EvdTlsOutputStream *self = EVD_TLS_OUTPUT_STREAM (obj);

  g_object_unref (self->priv->session);

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
      self->priv->session = g_value_dup_object (value);
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
evd_tls_output_stream_push (EvdTlsSession  *session,
                            const gchar    *buffer,
                            gsize           size,
                            gpointer        user_data,
                            GError        **error)
{
  EvdTlsOutputStream *self = EVD_TLS_OUTPUT_STREAM (user_data);
  gssize result;

  if (g_output_stream_is_closed (G_OUTPUT_STREAM (self)))
    return 0;

  result =
    G_OUTPUT_STREAM_CLASS (evd_tls_output_stream_parent_class)->
    write_fn (G_OUTPUT_STREAM (self),
              buffer,
              size,
              NULL,
              error);

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
