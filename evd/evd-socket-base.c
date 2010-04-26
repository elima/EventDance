/*
 * evd-socket-base.c
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

#include <math.h>
#include <string.h>

#include "evd-socket-base.h"

G_DEFINE_ABSTRACT_TYPE (EvdSocketBase, evd_socket_base, G_TYPE_OBJECT)

#define EVD_SOCKET_BASE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_SOCKET_BASE, \
                                          EvdSocketBasePrivate))

/* private data */
struct _EvdSocketBasePrivate
{
  GClosure *read_closure;
  GClosure *write_closure;

  EvdStreamThrottle *input_throttle;
};


/* properties */
enum
{
  PROP_0,
  PROP_READ_CLOSURE,
  PROP_WRITE_CLOSURE,

  PROP_INPUT_THROTTLE
};

static void     evd_socket_base_class_init         (EvdSocketBaseClass *class);
static void     evd_socket_base_init               (EvdSocketBase *self);

static void     evd_socket_base_finalize           (GObject *obj);
static void     evd_socket_base_dispose            (GObject *obj);

static void     evd_socket_base_set_property       (GObject      *obj,
                                                    guint         prop_id,
                                                    const GValue *value,
                                                    GParamSpec   *pspec);
static void     evd_socket_base_get_property       (GObject    *obj,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec);

static void     evd_socket_base_copy_properties    (EvdSocketBase *self,
                                                    EvdSocketBase *target);

static void
evd_socket_base_class_init (EvdSocketBaseClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_socket_base_dispose;
  obj_class->finalize = evd_socket_base_finalize;
  obj_class->get_property = evd_socket_base_get_property;
  obj_class->set_property = evd_socket_base_set_property;

  class->read_handler_marshal = g_cclosure_marshal_VOID__VOID;
  class->write_handler_marshal = g_cclosure_marshal_VOID__VOID;
  class->read_closure_changed = NULL;
  class->write_closure_changed = NULL;
  class->copy_properties = evd_socket_base_copy_properties;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_READ_CLOSURE,
                                   g_param_spec_boxed ("read-closure",
                                                       "Read closure",
                                                       "The callback closure that will be invoked when data is ready to be read",
                                                       G_TYPE_CLOSURE,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property (obj_class, PROP_WRITE_CLOSURE,
                                   g_param_spec_boxed ("write-closure",
                                                       "Write closure",
                                                       "The callback closure that will be invoked when data is ready to be written",
                                                       G_TYPE_CLOSURE,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property (obj_class, PROP_INPUT_THROTTLE,
                                   g_param_spec_object ("input-throttle",
                                                        "Input throttle",
                                                        "The input stream throttle object",
                                                        EVD_TYPE_STREAM_THROTTLE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdSocketBasePrivate));
}

static void
evd_socket_base_init (EvdSocketBase *self)
{
  EvdSocketBasePrivate *priv;

  priv = EVD_SOCKET_BASE_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->read_closure = NULL;
  priv->write_closure = NULL;

  priv->input_throttle = NULL;
}

static void
evd_socket_base_dispose (GObject *obj)
{
  EvdSocketBase *self = EVD_SOCKET_BASE (obj);

  if (self->priv->input_throttle != NULL)
    {
      g_object_unref (self->priv->input_throttle);
      self->priv->input_throttle = NULL;
    }

  G_OBJECT_CLASS (evd_socket_base_parent_class)->dispose (obj);
}

static void
evd_socket_base_finalize (GObject *obj)
{
  EvdSocketBase *self = EVD_SOCKET_BASE (obj);

  if (self->priv->read_closure != NULL)
    g_closure_unref (self->priv->read_closure);

  if (self->priv->write_closure != NULL)
    g_closure_unref (self->priv->write_closure);

  G_OBJECT_CLASS (evd_socket_base_parent_class)->finalize (obj);
}

static void
evd_socket_base_set_property (GObject      *obj,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EvdSocketBase *self;

  self = EVD_SOCKET_BASE (obj);

  switch (prop_id)
    {
    case PROP_READ_CLOSURE:
      evd_socket_base_set_on_read (self, g_value_get_boxed (value));
      break;

    case PROP_WRITE_CLOSURE:
      evd_socket_base_set_on_write (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_base_get_property (GObject    *obj,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EvdSocketBase *self;

  self = EVD_SOCKET_BASE (obj);

  switch (prop_id)
    {
    case PROP_READ_CLOSURE:
      g_value_set_boxed (value, self->priv->read_closure);
      break;

    case PROP_WRITE_CLOSURE:
      g_value_set_boxed (value, self->priv->write_closure);
      break;

    case PROP_INPUT_THROTTLE:
      g_value_set_object (value, evd_socket_base_get_input_throttle (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_base_copy_properties (EvdSocketBase *self, EvdSocketBase *target)
{
  /* @TODO */
}

/* protected methods */

/* public methods */

void
evd_socket_base_set_on_read (EvdSocketBase *self,
                        GClosure  *closure)
{
  EvdSocketBaseClass *class;

  g_return_if_fail (EVD_IS_SOCKET_BASE (self));

  if (closure == self->priv->read_closure)
    return;

  if (self->priv->read_closure != NULL)
    g_closure_unref (self->priv->read_closure);

  if (closure != NULL)
    {
      g_closure_ref (closure);
      g_closure_sink (closure);
    }

  self->priv->read_closure = closure;

  class = EVD_SOCKET_BASE_GET_CLASS (self);
  if (class->read_closure_changed != NULL)
    class->read_closure_changed (self);
}

GClosure *
evd_socket_base_get_on_read (EvdSocketBase *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_BASE (self), NULL);

  return self->priv->read_closure;
}

void
evd_socket_base_set_on_write (EvdSocketBase *self,
                         GClosure  *closure)
{
  EvdSocketBaseClass *class;

  g_return_if_fail (EVD_IS_SOCKET_BASE (self));

  if (closure == self->priv->write_closure)
    return;

  if (self->priv->write_closure != NULL)
    g_closure_unref (self->priv->write_closure);

  if (closure != NULL)
    {
      g_closure_ref (closure);
      g_closure_sink (closure);
    }

  self->priv->write_closure = closure;

  class = EVD_SOCKET_BASE_GET_CLASS (self);
  if (class->write_closure_changed != NULL)
    class->write_closure_changed (self);
}

GClosure *
evd_socket_base_get_on_write (EvdSocketBase *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_BASE (self), NULL);

  return self->priv->write_closure;
}

void
evd_socket_base_set_read_handler (EvdSocketBase *self,
                             GCallback  callback,
                             gpointer   user_data)
{
  GClosure *closure = NULL;

  g_return_if_fail (EVD_IS_SOCKET_BASE (self));

  if (callback != NULL)
    {
      EvdSocketBaseClass *class;

      class = EVD_SOCKET_BASE_GET_CLASS (self);

      g_return_if_fail (class->read_handler_marshal != NULL);

      closure = g_cclosure_new (callback, user_data, NULL);
      if (class->read_handler_marshal != NULL)
        g_closure_set_marshal (closure, class->read_handler_marshal);
    }

  evd_socket_base_set_on_read (EVD_SOCKET_BASE (self), closure);
}

void
evd_socket_base_set_write_handler (EvdSocketBase *self,
                              GCallback  callback,
                              gpointer   user_data)
{
  GClosure *closure = NULL;

  g_return_if_fail (EVD_IS_SOCKET_BASE (self));

  if (callback != NULL)
    {
      EvdSocketBaseClass *class;

      class = EVD_SOCKET_BASE_GET_CLASS (self);

      g_return_if_fail (class->write_handler_marshal != NULL);

      closure = g_cclosure_new (callback, user_data, NULL);
      if (class->write_handler_marshal != NULL)
        g_closure_set_marshal (closure, class->write_handler_marshal);
    }

  evd_socket_base_set_on_write (EVD_SOCKET_BASE (self), closure);
}

EvdStreamThrottle *
evd_socket_base_get_input_throttle (EvdSocketBase *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_BASE (self), NULL);

  if (self->priv->input_throttle == NULL)
    self->priv->input_throttle = evd_stream_throttle_new ();

  return self->priv->input_throttle;
}
