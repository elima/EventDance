/*
 * evd-io-stream.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011-2012, Igalia S.L.
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

#include "evd-marshal.h"

G_DEFINE_ABSTRACT_TYPE (EvdIoStream, evd_io_stream, G_TYPE_IO_STREAM)

#define EVD_IO_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                        EVD_TYPE_IO_STREAM, \
                                        EvdIoStreamPrivate))

/* private data */
struct _EvdIoStreamPrivate
{
  EvdStreamThrottle *input_throttle;
  EvdStreamThrottle *output_throttle;

  EvdIoStreamGroup *group;
};

/* signals */
enum
{
  SIGNAL_GROUP_CHANGED,
  SIGNAL_LAST
};

static guint evd_io_stream_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_INPUT_THROTTLE,
  PROP_OUTPUT_THROTTLE,
  PROP_GROUP
};

static void     evd_io_stream_class_init         (EvdIoStreamClass *class);
static void     evd_io_stream_init               (EvdIoStream *self);

static void     evd_io_stream_finalize           (GObject *obj);

static void     evd_io_stream_set_property       (GObject      *obj,
                                                  guint         prop_id,
                                                  const GValue *value,
                                                  GParamSpec   *pspec);
static void     evd_io_stream_get_property       (GObject    *obj,
                                                  guint       prop_id,
                                                  GValue     *value,
                                                  GParamSpec *pspec);

static void     on_group_destroyed               (gpointer  data,
                                                  GObject  *where_the_object_was);

static void
evd_io_stream_class_init (EvdIoStreamClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_io_stream_finalize;
  obj_class->get_property = evd_io_stream_get_property;
  obj_class->set_property = evd_io_stream_set_property;

  /* signals */
  evd_io_stream_signals[SIGNAL_GROUP_CHANGED] =
    g_signal_new ("group-changed",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdIoStreamClass, signal_group_changed),
                  NULL, NULL,
                  evd_marshal_VOID__OBJECT_OBJECT,
                  G_TYPE_NONE,
                  2, G_TYPE_OBJECT, G_TYPE_OBJECT);

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

  /**
   * EvdIoStream:group: (transfer none):
   *
   * The #EvdIoStreamGroup group this stream belongs to.
   *
   * Since: 0.2
   **/
  g_object_class_install_property (obj_class, PROP_GROUP,
                                   g_param_spec_object ("group",
                                                        "IO stream group",
                                                        "The group this stream belongs to",
                                                        EVD_TYPE_IO_STREAM_GROUP,
                                                        G_PARAM_READWRITE |
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

  priv->group = NULL;
}

static void
evd_io_stream_finalize (GObject *obj)
{
  EvdIoStream *self = EVD_IO_STREAM (obj);

  g_object_unref (self->priv->input_throttle);
  g_object_unref (self->priv->output_throttle);

  if (self->priv->group != NULL)
    {
      g_object_weak_unref (G_OBJECT (self->priv->group),
                           on_group_destroyed,
                           self);
    }

  G_OBJECT_CLASS (evd_io_stream_parent_class)->finalize (obj);
}

static void
evd_io_stream_set_property (GObject      *obj,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  EvdIoStream *self;

  self = EVD_IO_STREAM (obj);

  switch (prop_id)
    {
    case PROP_GROUP:
      evd_io_stream_set_group (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
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

    case PROP_GROUP:
      g_value_set_object (value, self->priv->group);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
on_group_destroyed (gpointer  data,
                    GObject  *where_the_object_was)
{
  EvdIoStream *self = EVD_IO_STREAM (data);

  if (where_the_object_was == G_OBJECT (self->priv->group))
    {
      EvdIoStreamClass *class;

      self->priv->group = NULL;

      class = EVD_IO_STREAM_GET_CLASS (self);
      if (class->group_changed != NULL)
        class->group_changed (self, NULL, NULL);

      g_signal_emit (self,
                     evd_io_stream_signals[SIGNAL_GROUP_CHANGED],
                     0,
                     NULL,
                     NULL,
                     NULL);
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

/**
 * evd_io_stream_set_group:
 * @group: (allow-none):
 *
 **/
gboolean
evd_io_stream_set_group (EvdIoStream *self, EvdIoStreamGroup *group)
{
  EvdIoStreamGroup *old_group = NULL;
  EvdIoStreamClass *class;

  g_return_val_if_fail (EVD_IS_IO_STREAM (self), FALSE);
  g_return_val_if_fail (group == NULL || EVD_IS_IO_STREAM_GROUP (group),
                        FALSE);

  if (group == self->priv->group)
    return FALSE;

  if (self->priv->group != NULL)
    {
      old_group = self->priv->group;
      self->priv->group = NULL;

      g_object_weak_unref (G_OBJECT (old_group),
                           on_group_destroyed,
                           self);

      evd_io_stream_group_remove (old_group, G_IO_STREAM (self));
    }

  self->priv->group = group;

  if (group != NULL)
    {
      g_object_weak_ref (G_OBJECT (group),
                         on_group_destroyed,
                         self);

      evd_io_stream_group_add (group, G_IO_STREAM (self));
    }

  class = EVD_IO_STREAM_GET_CLASS (self);
  if (class->group_changed != NULL)
    class->group_changed (self, group, old_group);

  g_signal_emit (self,
                 evd_io_stream_signals[SIGNAL_GROUP_CHANGED],
                 0,
                 group,
                 old_group,
                 NULL);

  return TRUE;
}

/**
 * evd_io_stream_get_group:
 *
 * Returns: (transfer none):
 **/
EvdIoStreamGroup *
evd_io_stream_get_group (EvdIoStream *self)
{
  g_return_val_if_fail (EVD_IS_IO_STREAM (self), NULL);

  return self->priv->group;
}
