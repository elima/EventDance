/*
 * evd-connection-group.c
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009, Igalia S.L.
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
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "evd-connection-group.h"

#include "evd-stream-throttle.h"

G_DEFINE_TYPE (EvdConnectionGroup, evd_connection_group, G_TYPE_OBJECT)

#define EVD_CONNECTION_GROUP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                               EVD_TYPE_CONNECTION_GROUP, \
                                               EvdConnectionGroupPrivate))

/* private data */
struct _EvdConnectionGroupPrivate
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

static void     evd_connection_group_class_init         (EvdConnectionGroupClass *class);
static void     evd_connection_group_init               (EvdConnectionGroup *self);

static void     evd_connection_group_finalize           (GObject *obj);

static void     evd_connection_group_set_property       (GObject      *obj,
                                                         guint         prop_id,
                                                         const GValue *value,
                                                         GParamSpec   *pspec);
static void     evd_connection_group_get_property       (GObject    *obj,
                                                         guint       prop_id,
                                                         GValue     *value,
                                                         GParamSpec *pspec);

static gboolean evd_connection_group_add_internal       (EvdConnectionGroup  *self,
                                                         EvdConnection       *conn,
                                                         GError             **error);
static gboolean evd_connection_group_remove_internal    (EvdConnectionGroup *self,
                                                         EvdConnection      *conn);

static void
evd_connection_group_class_init (EvdConnectionGroupClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_connection_group_finalize;
  obj_class->set_property = evd_connection_group_set_property;
  obj_class->get_property = evd_connection_group_get_property;

  class->add = evd_connection_group_add_internal;
  class->remove = evd_connection_group_remove_internal;

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

  g_type_class_add_private (obj_class, sizeof (EvdConnectionGroupPrivate));
}

static void
evd_connection_group_init (EvdConnectionGroup *self)
{
  EvdConnectionGroupPrivate *priv;

  priv = EVD_CONNECTION_GROUP_GET_PRIVATE (self);
  self->priv = priv;

  priv->input_throttle = evd_stream_throttle_new ();
  priv->output_throttle = evd_stream_throttle_new ();
}

static void
evd_connection_group_finalize (GObject *obj)
{
  EvdConnectionGroup *self = EVD_CONNECTION_GROUP (obj);

  g_object_unref (self->priv->input_throttle);
  g_object_unref (self->priv->output_throttle);

  G_OBJECT_CLASS (evd_connection_group_parent_class)->finalize (obj);
}

static void
evd_connection_group_set_property (GObject      *obj,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EvdConnectionGroup *self;

  self = EVD_CONNECTION_GROUP (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_connection_group_get_property (GObject    *obj,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  EvdConnectionGroup *self;

  self = EVD_CONNECTION_GROUP (obj);

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

static gboolean
evd_connection_group_add_internal (EvdConnectionGroup  *self,
                                   EvdConnection       *conn,
                                   GError             **error)
{
  EvdConnectionGroup *group;

  g_object_get (conn, "group", &group, NULL);

  if (group != self)
    g_object_set (conn, "group", self, NULL);

  return TRUE;
}

static gboolean
evd_connection_group_remove_internal (EvdConnectionGroup *self,
                                      EvdConnection      *conn)
{
  EvdConnectionGroup *group;

  g_object_get (conn, "group", &group, NULL);

  if (group != self)
    return FALSE;
  else
    g_object_set (conn, "group", NULL, NULL);

  return TRUE;
}

/* public methods */

EvdConnectionGroup *
evd_connection_group_new (void)
{
  EvdConnectionGroup *self;

  self = g_object_new (EVD_TYPE_CONNECTION_GROUP, NULL);

  return self;
}

gboolean
evd_connection_group_add (EvdConnectionGroup  *self,
                          EvdConnection       *connection,
                          GError             **error)
{
  EvdConnectionGroupClass *class;

  g_return_val_if_fail (EVD_IS_CONNECTION_GROUP (self), FALSE);
  g_return_val_if_fail (EVD_IS_CONNECTION (connection), FALSE);

  class = EVD_CONNECTION_GROUP_GET_CLASS (self);
  if (class->add != NULL)
    return class->add (self, connection, error);
  else
    return TRUE;
}

gboolean
evd_connection_group_remove (EvdConnectionGroup *self,
                             EvdConnection      *connection)
{
  EvdConnectionGroupClass *class;

  g_return_val_if_fail (EVD_IS_CONNECTION_GROUP (self), FALSE);
  g_return_val_if_fail (EVD_IS_CONNECTION (connection), FALSE);

  class = EVD_CONNECTION_GROUP_GET_CLASS (self);
  if (class->remove != NULL)
    return class->remove (self, connection);
  else
    return TRUE;
}
