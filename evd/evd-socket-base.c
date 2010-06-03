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
  EvdStreamThrottle *output_throttle;
};


/* properties */
enum
{
  PROP_0,
  PROP_READ_CLOSURE,
  PROP_WRITE_CLOSURE,

  PROP_INPUT_THROTTLE,
  PROP_OUTPUT_THROTTLE
};

static void     evd_socket_base_class_init         (EvdSocketBaseClass *class);
static void     evd_socket_base_init               (EvdSocketBase *self);

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
  obj_class->get_property = evd_socket_base_get_property;
  obj_class->set_property = evd_socket_base_set_property;

  class->read_handler_marshal = g_cclosure_marshal_VOID__VOID;
  class->write_handler_marshal = g_cclosure_marshal_VOID__VOID;
  class->read_closure_changed = NULL;
  class->write_closure_changed = NULL;
  class->copy_properties = evd_socket_base_copy_properties;

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

  g_object_class_install_property (obj_class, PROP_OUTPUT_THROTTLE,
                                   g_param_spec_object ("output-throttle",
                                                        "Output throttle",
                                                        "The output stream throttle object",
                                                        EVD_TYPE_STREAM_THROTTLE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdSocketBasePrivate));
}

static void
evd_socket_base_init (EvdSocketBase *self)
{
  EvdSocketBasePrivate *priv;

  priv = EVD_SOCKET_BASE_GET_PRIVATE (self);
  self->priv = priv;

  priv->read_closure = NULL;
  priv->write_closure = NULL;

  priv->input_throttle = NULL;
  priv->output_throttle = NULL;
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

  if (self->priv->output_throttle != NULL)
    {
      g_object_unref (self->priv->output_throttle);
      self->priv->output_throttle = NULL;
    }

  if (self->priv->read_closure != NULL)
    {
      g_closure_unref (self->priv->read_closure);
      self->priv->read_closure = NULL;
    }

  if (self->priv->write_closure != NULL)
    {
      g_closure_unref (self->priv->write_closure);
      self->priv->write_closure = NULL;
    }

  G_OBJECT_CLASS (evd_socket_base_parent_class)->dispose (obj);
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

    case PROP_OUTPUT_THROTTLE:
      g_value_set_object (value, evd_socket_base_get_output_throttle (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_base_copy_properties (EvdSocketBase *self, EvdSocketBase *target)
{
  EvdStreamThrottle *stream_throttle;
  gfloat bandwidth;
  gfloat latency;

  stream_throttle = evd_socket_base_get_input_throttle (target);

  if (self->priv->input_throttle != NULL)
    {
      g_object_get (self->priv->input_throttle,
                    "bandwidth", &bandwidth,
                    "latency", &latency,
                    NULL);
    }
  else
    {
      bandwidth = 0.0;
      latency = 0.0;
    }

  g_object_set (stream_throttle,
                "bandwidth", bandwidth,
                "latency", latency,
                NULL);

  stream_throttle = evd_socket_base_get_output_throttle (target);

  if (self->priv->output_throttle != NULL)
    {
      g_object_get (self->priv->output_throttle,
                    "bandwidth", &bandwidth,
                    "latency", &latency,
                    NULL);
    }
  else
    {
      bandwidth = 0.0;
      latency = 0.0;
    }

  g_object_set (stream_throttle,
                "bandwidth", bandwidth,
                "latency", latency,
                NULL);
}

/* public methods */

/**
 * evd_socket_base_set_on_read:
 * @self: The #EvdSocketBase.
 * @closure: (in) (allow-none): The #GClosure to be invoked.
 *
 * Specifies the closure to be invoked when data is waiting to be read from the
 * stream.
 **/
void
evd_socket_base_set_on_read (EvdSocketBase *self,
                             GClosure      *closure)
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

/**
 * evd_socket_base_get_on_read:
 * @self: The #EvdSocketBase.
 *
 * Returns: (transfer none): A #GClosure representing the current read handler,
 * or NULL.
 **/
GClosure *
evd_socket_base_get_on_read (EvdSocketBase *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_BASE (self), NULL);

  return self->priv->read_closure;
}

/**
 * evd_socket_base_set_on_write:
 * @self: The #EvdSocketBase.
 * @closure: (in) (allow-none): The #GClosure to be invoked.
 *
 * Specifies the closure to be invoked when it becomes safe to write data to the
 * stream.
 **/
void
evd_socket_base_set_on_write (EvdSocketBase *self,
                              GClosure      *closure)
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

/**
 * evd_socket_base_get_on_write:
 * @self: The #EvdSocketBase.
 *
 * Returns: (transfer none): A #GClosure representing the current write handler,
 * or NULL.
 **/
GClosure *
evd_socket_base_get_on_write (EvdSocketBase *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_BASE (self), NULL);

  return self->priv->write_closure;
}

/**
 * evd_socket_base_set_read_handler:
 * @self: The #EvdSocketBase.
 * @callback: (allow-none): The #GCallback to call upon read condition.
 * @user_data: Pointer to arbitrary data to pass in @callback.
 *
 * Specifies a pointer to the function to be invoked when data is waiting
 * to be read from the stream.
 **/
void
evd_socket_base_set_read_handler (EvdSocketBase *self,
                                  GCallback      callback,
                                  gpointer       user_data)
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

/**
 * evd_socket_base_set_write_handler:
 * @self: The #EvdSocketBase.
 * @callback: (allow-none): The #GCallback to call upon write condition.
 * @user_data: Pointer to arbitrary data to pass in @callback.
 *
 * Specifies a pointer to the function to be invoked when it becomes safe to
 * write data to the stream.
 **/
void
evd_socket_base_set_write_handler (EvdSocketBase *self,
                                   GCallback      callback,
                                   gpointer       user_data)
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

/**
 * evd_socket_base_get_input_throttle:
 *
 * Returns: (transfer none): The input #EvdStreamThrottle object
 **/
EvdStreamThrottle *
evd_socket_base_get_input_throttle (EvdSocketBase *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_BASE (self), NULL);

  if (self->priv->input_throttle == NULL)
    self->priv->input_throttle = evd_stream_throttle_new ();

  return self->priv->input_throttle;
}

/**
 * evd_socket_base_get_output_throttle:
 *
 * Returns: (transfer none): The output #EvdStreamThrottle object
 **/
EvdStreamThrottle *
evd_socket_base_get_output_throttle (EvdSocketBase *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_BASE (self), NULL);

  if (self->priv->output_throttle == NULL)
    self->priv->output_throttle = evd_stream_throttle_new ();

  return self->priv->output_throttle;
}

guint64
evd_socket_base_get_total_read (EvdSocketBase *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_BASE (self), 0);

  return
    evd_stream_throttle_get_total (evd_socket_base_get_input_throttle (self));
}

guint64
evd_socket_base_get_total_written (EvdSocketBase *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET_BASE (self), 0);

  return
    evd_stream_throttle_get_total (evd_socket_base_get_output_throttle (self));
}
