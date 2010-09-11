/*
 * evd-socket-output-stream.c
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

#include "evd-error.h"
#include "evd-socket-output-stream.h"

G_DEFINE_TYPE (EvdSocketOutputStream, evd_socket_output_stream, G_TYPE_OUTPUT_STREAM)

#define EVD_SOCKET_OUTPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                   EVD_TYPE_SOCKET_OUTPUT_STREAM, \
                                                   EvdSocketOutputStreamPrivate))

/* private data */
struct _EvdSocketOutputStreamPrivate
{
  EvdSocket *socket;
};

/* signals */
enum
{
  SIGNAL_FILLED,
  SIGNAL_LAST
};

static guint evd_socket_output_stream_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_SOCKET
};

static void     evd_socket_output_stream_class_init         (EvdSocketOutputStreamClass *class);
static void     evd_socket_output_stream_init               (EvdSocketOutputStream *self);

static void     evd_socket_output_stream_finalize           (GObject *obj);

static void     evd_socket_output_stream_set_property       (GObject      *obj,
                                                             guint         prop_id,
                                                             const GValue *value,
                                                             GParamSpec   *pspec);
static void     evd_socket_output_stream_get_property       (GObject    *obj,
                                                             guint       prop_id,
                                                             GValue     *value,
                                                             GParamSpec *pspec);

static gssize   evd_socket_output_stream_write              (GOutputStream  *stream,
                                                             const void     *buffer,
                                                             gsize          size,
                                                             GCancellable  *cancellable,
                                                             GError       **error);
static void
evd_socket_output_stream_class_init (EvdSocketOutputStreamClass *class)
{
  GObjectClass *obj_class;
  GOutputStreamClass *output_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_socket_output_stream_finalize;
  obj_class->get_property = evd_socket_output_stream_get_property;
  obj_class->set_property = evd_socket_output_stream_set_property;

  output_stream_class = G_OUTPUT_STREAM_CLASS (class);
  output_stream_class->write_fn = evd_socket_output_stream_write;

  evd_socket_output_stream_signals[SIGNAL_FILLED] =
    g_signal_new ("filled",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdSocketOutputStreamClass, filled),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  g_object_class_install_property (obj_class, PROP_SOCKET,
				   g_param_spec_object ("socket",
							"socket",
							"The socket that this stream wraps",
							EVD_TYPE_SOCKET,
							G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  g_type_class_add_private (obj_class, sizeof (EvdSocketOutputStreamPrivate));
}

static void
evd_socket_output_stream_init (EvdSocketOutputStream *self)
{
  EvdSocketOutputStreamPrivate *priv;

  priv = EVD_SOCKET_OUTPUT_STREAM_GET_PRIVATE (self);
  self->priv = priv;
}

static void
evd_socket_output_stream_finalize (GObject *obj)
{
  EvdSocketOutputStream *self = EVD_SOCKET_OUTPUT_STREAM (obj);

  g_object_unref (self->priv->socket);

  G_OBJECT_CLASS (evd_socket_output_stream_parent_class)->finalize (obj);
}

static void
evd_socket_output_stream_set_property (GObject      *obj,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  EvdSocketOutputStream *self;

  self = EVD_SOCKET_OUTPUT_STREAM (obj);

  switch (prop_id)
    {
    case PROP_SOCKET:
      evd_socket_output_stream_set_socket (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_output_stream_get_property (GObject    *obj,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  EvdSocketOutputStream *self;

  self = EVD_SOCKET_OUTPUT_STREAM (obj);

  switch (prop_id)
    {
    case PROP_SOCKET:
      g_value_set_object (value, evd_socket_output_stream_get_socket (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gssize
evd_socket_output_stream_write (GOutputStream  *stream,
                                const void     *buffer,
                                gsize          size,
                                GCancellable  *cancellable,
                                GError       **error)
{
  EvdSocketOutputStream *self = EVD_SOCKET_OUTPUT_STREAM (stream);
  GError *_error = NULL;
  GSocket *socket;
  gssize actual_size = 0;

  socket = evd_socket_get_socket (self->priv->socket);

  if (socket == NULL)
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_NOT_WRITABLE,
                           "Socket is not writable");

      return -1;
    }

  if ( (actual_size = g_socket_send (socket,
                                     buffer,
                                     size,
                                     cancellable,
                                     &_error)) < 0)
    {
      if ( (_error)->code == G_IO_ERROR_WOULD_BLOCK)
        {
          g_error_free (_error);
          actual_size = 0;
        }
      else
        {
          if (error != NULL)
            *error = _error;
          else
            g_error_free (_error);

          return -1;
        }
    }

  if (actual_size < size)
    {
      g_object_ref (self);
      g_signal_emit (self,
                     evd_socket_output_stream_signals[SIGNAL_FILLED],
                     0,
                     NULL);
      g_object_unref (self);
    }

  return actual_size;
}

/* public methods */

EvdSocketOutputStream *
evd_socket_output_stream_new (EvdSocket *socket)
{
  EvdSocketOutputStream *self;

  g_return_val_if_fail (EVD_IS_SOCKET (socket), NULL);

  self = g_object_new (EVD_TYPE_SOCKET_OUTPUT_STREAM,
                       "socket", socket,
                       NULL);

  return self;
}

void
evd_socket_output_stream_set_socket (EvdSocketOutputStream *self,
                                     EvdSocket             *socket)
{
  g_return_if_fail (EVD_IS_SOCKET_OUTPUT_STREAM (self));
  g_return_if_fail (EVD_IS_SOCKET (socket));

  if (self->priv->socket != NULL)
    g_object_unref (self->priv->socket);

  self->priv->socket = socket;
  g_object_ref (self->priv->socket);
}

EvdSocket *
evd_socket_output_stream_get_socket (EvdSocketOutputStream *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_OUTPUT_STREAM (self), NULL);

  return self->priv->socket;
}
