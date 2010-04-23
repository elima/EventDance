/*
 * evd-socket-input-stream.c
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

#include <evd-socket.h>

#include "evd-socket-input-stream.h"

G_DEFINE_TYPE (EvdSocketInputStream, evd_socket_input_stream, G_TYPE_INPUT_STREAM)

#define EVD_SOCKET_INPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                  EVD_TYPE_SOCKET_INPUT_STREAM, \
                                                  EvdSocketInputStreamPrivate))

/* private data */
struct _EvdSocketInputStreamPrivate
{
  EvdSocket *socket;
};

/* signals */
enum
{
  SIGNAL_DRAINED,
  SIGNAL_LAST
};

static guint evd_socket_input_stream_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_SOCKET
};

static void     evd_socket_input_stream_class_init         (EvdSocketInputStreamClass *class);
static void     evd_socket_input_stream_init               (EvdSocketInputStream *self);

static void     evd_socket_input_stream_finalize           (GObject *obj);

static void     evd_socket_input_stream_set_property       (GObject      *obj,
                                                            guint         prop_id,
                                                            const GValue *value,
                                                            GParamSpec   *pspec);
static void     evd_socket_input_stream_get_property       (GObject    *obj,
                                                            guint       prop_id,
                                                            GValue     *value,
                                                            GParamSpec *pspec);

static gssize   evd_socket_input_stream_read               (GInputStream  *stream,
                                                            void          *buffer,
                                                            gsize          size,
                                                            GCancellable  *cancellable,
                                                            GError       **error);
static void
evd_socket_input_stream_class_init (EvdSocketInputStreamClass *class)
{
  GObjectClass *obj_class;
  GInputStreamClass *input_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_socket_input_stream_finalize;
  obj_class->get_property = evd_socket_input_stream_get_property;
  obj_class->set_property = evd_socket_input_stream_set_property;

  input_stream_class = G_INPUT_STREAM_CLASS (class);
  input_stream_class->read_fn = evd_socket_input_stream_read;

  evd_socket_input_stream_signals[SIGNAL_DRAINED] =
    g_signal_new ("drained",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdSocketInputStreamClass, drained),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  g_object_class_install_property (obj_class, PROP_SOCKET,
				   g_param_spec_object ("socket",
							"socket",
							"The socket that this stream wraps",
							EVD_TYPE_SOCKET,
                                                        G_PARAM_CONSTRUCT_ONLY |
							G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  g_type_class_add_private (obj_class, sizeof (EvdSocketInputStreamPrivate));
}

static void
evd_socket_input_stream_init (EvdSocketInputStream *self)
{
  EvdSocketInputStreamPrivate *priv;

  priv = EVD_SOCKET_INPUT_STREAM_GET_PRIVATE (self);
  self->priv = priv;
}

static void
evd_socket_input_stream_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_socket_input_stream_parent_class)->finalize (obj);
}

static void
evd_socket_input_stream_set_property (GObject      *obj,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  EvdSocketInputStream *self;

  self = EVD_SOCKET_INPUT_STREAM (obj);

  switch (prop_id)
    {
    case PROP_SOCKET:
      self->priv->socket = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_input_stream_get_property (GObject    *obj,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  EvdSocketInputStream *self;

  self = EVD_SOCKET_INPUT_STREAM (obj);

  switch (prop_id)
    {
    case PROP_SOCKET:
      g_value_set_object (value, self->priv->socket);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gssize
evd_socket_input_stream_read (GInputStream  *stream,
                              void          *buffer,
                              gsize          size,
                              GCancellable  *cancellable,
                              GError       **error)
{
  EvdSocketInputStream *self = EVD_SOCKET_INPUT_STREAM (stream);
  GError *_error = NULL;
  GSocket *socket;
  gssize actual_size = 0;

  socket = evd_socket_get_socket (self->priv->socket);

  if ( (actual_size = g_socket_receive (socket,
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
          actual_size = -1;
        }
    }
  else if (actual_size < size)
    {
      g_object_ref (self);
      g_signal_emit (self,
                     evd_socket_input_stream_signals[SIGNAL_DRAINED],
                     0,
                     NULL);
      g_object_unref (self);
    }

  return actual_size;
}

/* public methods */

EvdSocketInputStream *
evd_socket_input_stream_new (EvdSocket *socket)
{
  EvdSocketInputStream *self;

  self = g_object_new (EVD_TYPE_SOCKET_INPUT_STREAM,
                       "socket", socket,
                       NULL);

  return self;
}