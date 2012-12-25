/*
 * evd-io-stream-group.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2012, Igalia S.L.
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

#include "evd-io-stream-group.h"

#include "evd-stream-throttle.h"
#include "evd-io-stream.h"

G_DEFINE_TYPE (EvdIoStreamGroup, evd_io_stream_group, G_TYPE_OBJECT)

#define EVD_IO_STREAM_GROUP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                              EVD_TYPE_IO_STREAM_GROUP, \
                                              EvdIoStreamGroupPrivate))

/* private data */
struct _EvdIoStreamGroupPrivate
{
  EvdStreamThrottle *input_throttle;
  EvdStreamThrottle *output_throttle;

  gboolean recursed;

  GList *streams;
};

/* properties */
enum
{
  PROP_0,
  PROP_INPUT_THROTTLE,
  PROP_OUTPUT_THROTTLE
};

static void     evd_io_stream_group_class_init         (EvdIoStreamGroupClass *class);
static void     evd_io_stream_group_init               (EvdIoStreamGroup *self);

static void     evd_io_stream_group_finalize           (GObject *obj);

static void     evd_io_stream_group_get_property       (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static gboolean evd_io_stream_group_add_internal       (EvdIoStreamGroup *self,
                                                        GIOStream          *io_stream);
static gboolean evd_io_stream_group_remove_internal    (EvdIoStreamGroup *self,
                                                        GIOStream          *io_stream);

static void     io_stream_on_close                     (EvdIoStream *io_stream,
                                                        gpointer     user_data);

static void
evd_io_stream_group_class_init (EvdIoStreamGroupClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_io_stream_group_finalize;
  obj_class->get_property = evd_io_stream_group_get_property;

  class->add = evd_io_stream_group_add_internal;
  class->remove = evd_io_stream_group_remove_internal;

  g_object_class_install_property (obj_class, PROP_INPUT_THROTTLE,
                                   g_param_spec_object ("input-throttle",
                                                        "Input throttle object",
                                                        "The input throttle for all connections within the group",
                                                        EVD_TYPE_STREAM_THROTTLE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_OUTPUT_THROTTLE,
                                   g_param_spec_object ("output-throttle",
                                                        "Output throttle object",
                                                        "The output throttle for all connections within the group",
                                                        EVD_TYPE_STREAM_THROTTLE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdIoStreamGroupPrivate));
}

static void
evd_io_stream_group_init (EvdIoStreamGroup *self)
{
  EvdIoStreamGroupPrivate *priv;

  priv = EVD_IO_STREAM_GROUP_GET_PRIVATE (self);
  self->priv = priv;

  priv->input_throttle = evd_stream_throttle_new ();
  priv->output_throttle = evd_stream_throttle_new ();

  priv->recursed = FALSE;

  priv->streams = NULL;
}

static void
evd_io_stream_group_finalize (GObject *obj)
{
  EvdIoStreamGroup *self = EVD_IO_STREAM_GROUP (obj);
  GList *node;

  g_object_unref (self->priv->input_throttle);
  g_object_unref (self->priv->output_throttle);

  node = self->priv->streams;
  while (node != NULL)
    {
      evd_io_stream_group_remove (self, G_IO_STREAM (node->data));

      node = node->next;
    }
  g_list_free (self->priv->streams);

  G_OBJECT_CLASS (evd_io_stream_group_parent_class)->finalize (obj);
}

static void
evd_io_stream_group_get_property (GObject    *obj,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EvdIoStreamGroup *self;

  self = EVD_IO_STREAM_GROUP (obj);

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

static void
io_stream_on_close (EvdIoStream *io_stream, gpointer user_data)
{
  EvdIoStreamGroup *self = EVD_IO_STREAM_GROUP (user_data);

  evd_io_stream_group_remove (self, G_IO_STREAM (io_stream));
}

static gboolean
evd_io_stream_group_add_internal (EvdIoStreamGroup *self, GIOStream *io_stream)
{
  gboolean result = TRUE;

  result = evd_io_stream_set_group (EVD_IO_STREAM (io_stream), self);

  if (result)
    {
      g_signal_connect (io_stream,
                        "close",
                        G_CALLBACK (io_stream_on_close),
                        self);

      self->priv->streams = g_list_prepend (self->priv->streams, io_stream);
    }

  return result;
}

static gboolean
evd_io_stream_group_remove_internal (EvdIoStreamGroup *self,
                                     GIOStream        *io_stream)
{
  gboolean result = TRUE;

  result = evd_io_stream_set_group (EVD_IO_STREAM (io_stream), NULL);

  g_signal_handlers_disconnect_by_func (io_stream,
                                        G_CALLBACK (io_stream_on_close),
                                        self);

  self->priv->streams = g_list_remove (self->priv->streams, io_stream);

  return result;
}

/* public methods */

EvdIoStreamGroup *
evd_io_stream_group_new (void)
{
  EvdIoStreamGroup *self;

  self = g_object_new (EVD_TYPE_IO_STREAM_GROUP, NULL);

  return self;
}

gboolean
evd_io_stream_group_add (EvdIoStreamGroup *self, GIOStream *io_stream)
{
  EvdIoStreamGroupClass *class;

  g_return_val_if_fail (EVD_IS_IO_STREAM_GROUP (self), FALSE);
  g_return_val_if_fail (EVD_IS_IO_STREAM (io_stream), FALSE);

  class = EVD_IO_STREAM_GROUP_GET_CLASS (self);

  if (class->add != NULL)
    {
      gboolean result = TRUE;

      if (! self->priv->recursed)
        {
          self->priv->recursed = TRUE;
          result = class->add (self, io_stream);
          self->priv->recursed = FALSE;
        }

      return result;
    }

  return FALSE;
}

/**
 * evd_io_stream_group_remove:
 *
 **/
gboolean
evd_io_stream_group_remove (EvdIoStreamGroup *self, GIOStream *io_stream)
{
  EvdIoStreamGroupClass *class;

  g_return_val_if_fail (EVD_IS_IO_STREAM_GROUP (self), FALSE);
  g_return_val_if_fail (EVD_IS_IO_STREAM (io_stream), FALSE);

  class = EVD_IO_STREAM_GROUP_GET_CLASS (self);

  if (class->remove != NULL)
    {
      gboolean result = TRUE;

      if (! self->priv->recursed)
        {
          self->priv->recursed = TRUE;
          result = class->remove (self, io_stream);
          self->priv->recursed = FALSE;
        }

      return result;
    }

  return FALSE;
}
