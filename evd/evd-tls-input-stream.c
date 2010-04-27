/*
 * evd-tls-input-stream.c
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

#include "evd-tls-input-stream.h"

G_DEFINE_TYPE (EvdTlsInputStream, evd_tls_input_stream, G_TYPE_FILTER_INPUT_STREAM)

#define EVD_TLS_INPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                               EVD_TYPE_TLS_INPUT_STREAM, \
                                               EvdTlsInputStreamPrivate))

/* private data */
struct _EvdTlsInputStreamPrivate
{
  EvdTlsSession *session;
};

/* signals */
enum
{
  SIGNAL_DRAINED,
  SIGNAL_LAST
};

static guint evd_tls_input_stream_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_SESSION
};

static void     evd_tls_input_stream_class_init         (EvdTlsInputStreamClass *class);
static void     evd_tls_input_stream_init               (EvdTlsInputStream *self);
static void     evd_tls_input_stream_finalize           (GObject *obj);

static void     evd_tls_input_stream_set_property       (GObject      *obj,
                                                         guint         prop_id,
                                                         const GValue *value,
                                                         GParamSpec   *pspec);
static void     evd_tls_input_stream_get_property       (GObject    *obj,
                                                         guint       prop_id,
                                                         GValue     *value,
                                                         GParamSpec *pspec);

static gssize   evd_tls_input_stream_read               (GInputStream  *stream,
                                                         void          *buffer,
                                                         gsize          size,
                                                         GCancellable  *cancellable,
                                                         GError       **error);

static void     evd_tls_input_stream_session_destroy_notify (gpointer  user_data,
                                                             GObject  *obj);

static void
evd_tls_input_stream_class_init (EvdTlsInputStreamClass *class)
{
  GObjectClass *obj_class;
  GInputStreamClass *input_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_tls_input_stream_finalize;
  obj_class->get_property = evd_tls_input_stream_get_property;
  obj_class->set_property = evd_tls_input_stream_set_property;

  input_stream_class = G_INPUT_STREAM_CLASS (class);
  input_stream_class->read_fn = evd_tls_input_stream_read;

  evd_tls_input_stream_signals[SIGNAL_DRAINED] =
    g_signal_new ("drained",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdTlsInputStreamClass, drained),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  g_object_class_install_property (obj_class, PROP_SESSION,
				   g_param_spec_object ("session",
							"The TLS session",
							"The TLS session associated with this stream",
							EVD_TYPE_TLS_SESSION,
                                                        G_PARAM_CONSTRUCT_ONLY |
							G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  g_type_class_add_private (obj_class, sizeof (EvdTlsInputStreamPrivate));
}

static void
evd_tls_input_stream_init (EvdTlsInputStream *self)
{
  EvdTlsInputStreamPrivate *priv;

  priv = EVD_TLS_INPUT_STREAM_GET_PRIVATE (self);
  self->priv = priv;
}

static void
evd_tls_input_stream_finalize (GObject *obj)
{
  EvdTlsInputStream *self = EVD_TLS_INPUT_STREAM (obj);

  g_object_weak_unref (G_OBJECT (self->priv->session),
                       evd_tls_input_stream_session_destroy_notify,
                       self);

  G_OBJECT_CLASS (evd_tls_input_stream_parent_class)->finalize (obj);
}

static void
evd_tls_input_stream_set_property (GObject      *obj,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EvdTlsInputStream *self;

  self = EVD_TLS_INPUT_STREAM (obj);

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
evd_tls_input_stream_get_property (GObject    *obj,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  EvdTlsInputStream *self;

  self = EVD_TLS_INPUT_STREAM (obj);

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
evd_tls_input_stream_pull (EvdTlsSession *session,
                           gchar         *buffer,
                           gsize          size,
                           gpointer       user_data)
{
  EvdTlsInputStream *self = EVD_TLS_INPUT_STREAM (user_data);
  GError *error = NULL;
  gssize result = EVD_TLS_ERROR_AGAIN;
  GInputStream *base_stream;

  base_stream =
    g_filter_input_stream_get_base_stream (G_FILTER_INPUT_STREAM (self));

  result = g_input_stream_read (base_stream, buffer, size, NULL, &error);

  if (result == 0)
    {
      g_object_ref (self);
      g_signal_emit (self, evd_tls_input_stream_signals[SIGNAL_DRAINED], 0, NULL);
      g_object_unref (self);

      if (! g_input_stream_has_pending (G_INPUT_STREAM (self)) &&
          ! g_input_stream_set_pending (G_INPUT_STREAM (self), &error))
        {
          /* @TODO: report this error through EvdSocket, somehow */
          g_debug ("EvdTlsInputStream pull func error: %s", error->message);
        }

      result = EVD_TLS_ERROR_AGAIN;
    }
  else
    {
      g_input_stream_clear_pending (G_INPUT_STREAM (self));

      if (result < 0)
        {
          /* @TODO: report this error through EvdSocket, somehow */
          g_debug ("EvdTlsInputStream pull func error: %s", error->message);
        }
    }

  return result;
}

static gssize
evd_tls_input_stream_read (GInputStream  *stream,
                           void          *buffer,
                           gsize          size,
                           GCancellable  *cancellable,
                           GError       **error)
{
  EvdTlsInputStream *self = EVD_TLS_INPUT_STREAM (stream);
  GError *_error = NULL;
  gssize actual_size;

  actual_size = evd_tls_session_read (self->priv->session,
                                      buffer,
                                      size,
                                      &_error);

  /* hack to gracefully recover from peer
     abruptly closing TLS connection */
  if (actual_size < 0)
    {
      if (_error->code == EVD_TLS_ERROR_UNEXPECTED_PACKET_LEN)
        {
          g_error_free (_error);
          actual_size = 0;

          g_input_stream_clear_pending (stream);
          g_input_stream_close (stream, NULL, error);
        }
      else
        {
          if (error != NULL)
            *error = _error;
        }
    }

  return actual_size;
}

static void
evd_tls_input_stream_session_destroy_notify (gpointer  user_data,
                                             GObject  *obj)
{
  EvdTlsInputStream *self = EVD_TLS_INPUT_STREAM (user_data);

  g_object_unref (self);
}

/* public methods */

EvdTlsInputStream *
evd_tls_input_stream_new (EvdTlsSession *session,
                          GInputStream  *base_stream)
{
  EvdTlsInputStream *self;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (session), NULL);
  g_return_val_if_fail (G_IS_INPUT_STREAM (base_stream), NULL);

  self = g_object_new (EVD_TYPE_TLS_INPUT_STREAM,
                       "session", session,
                       "base-stream", base_stream,
                       NULL);

  evd_tls_session_set_transport_pull_func (session,
                                           evd_tls_input_stream_pull,
                                           self);

  g_object_weak_ref (G_OBJECT (session),
                     evd_tls_input_stream_session_destroy_notify,
                     self);

  return self;
}
