/*
 * evd-io-stream.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011, Igalia S.L.
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

#include "evd-io-stream.h"

G_DEFINE_ABSTRACT_TYPE (EvdIoStream, evd_io_stream, G_TYPE_IO_STREAM)

#define EVD_IO_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                        EVD_TYPE_IO_STREAM, \
                                        EvdIoStreamPrivate))

/* private data */
struct _EvdIoStreamPrivate
{
  EvdStreamThrottle *input_throttle;
  EvdStreamThrottle *output_throttle;
};

/* properties */
enum
{
  PROP_0,
  PROP_INPUT_THROTTLE,
  PROP_OUTPUT_THROTTLE
};

static void     evd_io_stream_class_init         (EvdIoStreamClass *class);
static void     evd_io_stream_init               (EvdIoStream *self);

static void     evd_io_stream_finalize           (GObject *obj);

static void     evd_io_stream_get_property       (GObject    *obj,
                                                  guint       prop_id,
                                                  GValue     *value,
                                                  GParamSpec *pspec);

static void
evd_io_stream_class_init (EvdIoStreamClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_io_stream_finalize;
  obj_class->get_property = evd_io_stream_get_property;

  /* properties */
  g_object_class_install_property (obj_class, PROP_INPUT_THROTTLE,
                                   g_param_spec_object ("input-throttle",
                                                        "Input throttle object",
                                                        "The stream's input throttle object",
                                                        EVD_TYPE_STREAM_THROTTLE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_OUTPUT_THROTTLE,
                                   g_param_spec_object ("output-throttle",
                                                        "Output throttle object",
                                                        "The stream's output throttle object",
                                                        EVD_TYPE_STREAM_THROTTLE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdIoStreamPrivate));
}

static void
evd_io_stream_init (EvdIoStream *self)
{
  EvdIoStreamPrivate *priv;

  priv = EVD_IO_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  priv->input_throttle = evd_stream_throttle_new ();
  priv->output_throttle = evd_stream_throttle_new ();
}

static void
evd_io_stream_finalize (GObject *obj)
{
  EvdIoStream *self = EVD_IO_STREAM (obj);

  g_object_unref (self->priv->input_throttle);
  g_object_unref (self->priv->output_throttle);

  G_OBJECT_CLASS (evd_io_stream_parent_class)->finalize (obj);
}

static void
evd_io_stream_get_property (GObject    *obj,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  EvdIoStream *self;

  self = EVD_IO_STREAM (obj);

  switch (prop_id)
    {
    case PROP_INPUT_THROTTLE:
      g_value_set_object (value, self->priv->input_throttle);
      break;

    case PROP_OUTPUT_THROTTLE:
      g_value_set_object (value, self->priv->output_throttle);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* public methods */

/**
 * evd_io_stream_get_input_throttle:
 *
 * Returns: (transfer none):
 **/
EvdStreamThrottle *
evd_io_stream_get_input_throttle (EvdIoStream *self)
{
  g_return_val_if_fail (EVD_IS_IO_STREAM (self), NULL);

  return self->priv->input_throttle;
}

/**
 * evd_io_stream_get_output_throttle:
 *
 * Returns: (transfer none):
 **/
EvdStreamThrottle *
evd_io_stream_get_output_throttle (EvdIoStream *self)
{
  g_return_val_if_fail (EVD_IS_IO_STREAM (self), NULL);

  return self->priv->output_throttle;
}
