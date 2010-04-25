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

  gsize  bandwidth_in;
  gsize  bandwidth_out;
  gulong latency_in;
  gulong latency_out;

  GTimeVal current_time;
  gsize    bytes_in;
  gsize    bytes_out;
  GTimeVal last_in;
  GTimeVal last_out;

  gsize total_in;
  gsize total_out;
};


/* properties */
enum
{
  PROP_0,
  PROP_READ_CLOSURE,
  PROP_WRITE_CLOSURE,
  PROP_BANDWIDTH_IN,
  PROP_BANDWIDTH_OUT,
  PROP_LATENCY_IN,
  PROP_LATENCY_OUT
};

G_LOCK_DEFINE_STATIC (counters);

static void     evd_socket_base_class_init         (EvdSocketBaseClass *class);
static void     evd_socket_base_init               (EvdSocketBase *self);

static void     evd_socket_base_finalize           (GObject *obj);

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

  g_object_class_install_property (obj_class, PROP_BANDWIDTH_IN,
                                   g_param_spec_float ("bandwidth-in",
                                                       "Inbound bandwidth limit",
                                                       "The maximum bandwidth for reading, in kilobytes",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_BANDWIDTH_OUT,
                                   g_param_spec_float ("bandwidth-out",
                                                       "Outbound bandwidth limit",
                                                       "The maximum bandwidth for writing, in kilobytes",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_LATENCY_IN,
                                   g_param_spec_float ("latency-in",
                                                       "Inbound minimum latency",
                                                       "The minimum time between two reads, in miliseconds",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_LATENCY_OUT,
                                   g_param_spec_float ("latency-out",
                                                       "Outbound minimum latency",
                                                       "The minimum time between two writes, in miliseconds",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
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

  priv->bandwidth_in = 0;
  priv->bandwidth_out = 0;
  priv->latency_in = 0;
  priv->latency_out = 0;

  priv->current_time.tv_sec = 0;
  priv->current_time.tv_usec = 0;

  priv->bytes_in = 0;
  priv->bytes_out = 0;
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

    case PROP_BANDWIDTH_IN:
      self->priv->bandwidth_in = (gsize) (g_value_get_float (value) * 1024.0);
      break;

    case PROP_BANDWIDTH_OUT:
      self->priv->bandwidth_out = (gsize) (g_value_get_float (value) * 1024.0);
      break;

      /* Latency properties are in miliseconds, but we store the value
         internally  in microseconds, to allow up to 1/1000 fraction of a
         milisecond */
    case PROP_LATENCY_IN:
      self->priv->latency_in = (gulong) (g_value_get_float (value) * 1000.0);
      break;

    case PROP_LATENCY_OUT:
      self->priv->latency_out = (gulong) (g_value_get_float (value) * 1000.0);
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

    case PROP_BANDWIDTH_IN:
      g_value_set_float (value, self->priv->bandwidth_in / 1024.0);
      break;

    case PROP_BANDWIDTH_OUT:
      g_value_set_float (value, self->priv->bandwidth_out / 1024.0);
      break;

      /* Latency values are stored in microseconds internally */
    case PROP_LATENCY_IN:
      g_value_set_float (value, self->priv->latency_in / 1000.0);
      break;

    case PROP_LATENCY_OUT:
      g_value_set_float (value, self->priv->latency_out / 1000.0);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_base_update_current_time (EvdSocketBase *self)
{
  GTimeVal time_val;

  g_get_current_time (&time_val);

  if (time_val.tv_sec != self->priv->current_time.tv_sec)
    {
      G_LOCK (counters);

      self->priv->bytes_in = 0;
      self->priv->bytes_out = 0;

      G_UNLOCK (counters);
    }

  g_memmove (&self->priv->current_time, &time_val, sizeof (GTimeVal));
}

static gulong
g_timeval_get_diff_micro (GTimeVal *time1, GTimeVal *time2)
{
  gulong result;

  result = ABS (time2->tv_sec - time1->tv_sec) * G_USEC_PER_SEC;
  result += ABS (time2->tv_usec - time1->tv_usec) / 1000;

  return result;
}

static gsize
evd_socket_base_request (EvdSocketBase *self,
                         gsize      bandwidth,
                         gulong     latency,
                         gsize      bytes,
                         GTimeVal  *last,
                         gsize      size,
                         guint     *wait)
{
  gsize actual_size = size;

  if ( (wait != NULL) && (*wait < 0) )
    *wait = 0;

  G_LOCK (counters);

  /*  latency check */
  if (latency > 0)
    {
      gulong elapsed;

      elapsed = g_timeval_get_diff_micro (&self->priv->current_time,
                                          last);
      if (elapsed < latency)
        {
          actual_size = 0;

          if (wait != NULL)
            *wait = MAX ((guint) ((latency - elapsed) / 1000),
                         *wait);
        }
    }

  /* bandwidth check */
  if ( (bandwidth > 0) && (actual_size > 0) )
    {
      actual_size = MAX ((gssize) (bandwidth - bytes), 0);
      actual_size = MIN (actual_size, size);

      if (wait != NULL)
        if (actual_size < size)
          *wait = MAX ((guint) (((1000001 - self->priv->current_time.tv_usec) / 1000)) + 1,
                       *wait);
    }

  G_UNLOCK (counters);

  return actual_size;
}

static void
evd_socket_base_copy_properties (EvdSocketBase *self, EvdSocketBase *target)
{
  target->priv->bandwidth_in = self->priv->bandwidth_in;
  target->priv->bandwidth_out = self->priv->bandwidth_out;
  target->priv->latency_in = self->priv->latency_in;
  target->priv->latency_out = self->priv->latency_out;
}

/* protected methods */

void
evd_socket_base_report_read (EvdSocketBase *self,
                        gsize     size)
{
  evd_socket_base_update_current_time (self);

  G_LOCK (counters);

  self->priv->bytes_in += size;
  self->priv->total_in += size;

  g_memmove (&self->priv->last_in,
             &self->priv->current_time,
             sizeof (GTimeVal));

  G_UNLOCK (counters);
}

void
evd_socket_base_report_write (EvdSocketBase *self,
                         gsize     size)
{
  evd_socket_base_update_current_time (self);

  G_LOCK (counters);

  self->priv->bytes_out += size;
  self->priv->total_out += size;

  g_memmove (&self->priv->last_out,
             &self->priv->current_time,
             sizeof (GTimeVal));

  G_UNLOCK (counters);
}

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

gsize
evd_socket_base_request_read  (EvdSocketBase *self,
                          gsize      size,
                          guint     *wait)
{
  evd_socket_base_update_current_time (self);

  return evd_socket_base_request (self,
                             self->priv->bandwidth_in,
                             self->priv->latency_in,
                             self->priv->bytes_in,
                             &self->priv->last_in,
                             size,
                             wait);
}

gsize
evd_socket_base_request_write (EvdSocketBase *self,
                          gsize      size,
                          guint     *wait)
{
  evd_socket_base_update_current_time (self);

  return evd_socket_base_request (self,
                             self->priv->bandwidth_out,
                             self->priv->latency_out,
                             self->priv->bytes_out,
                             &self->priv->last_out,
                             size,
                             wait);
}

gulong
evd_socket_base_get_total_read (EvdSocketBase *self)
{
  return self->priv->total_in;
}

gulong
evd_socket_base_get_total_written (EvdSocketBase *self)
{
  return self->priv->total_out;
}

gfloat
evd_socket_base_get_actual_bandwidth_in (EvdSocketBase *self)
{
  return self->priv->bytes_in / 1024.0;
}

gfloat
evd_socket_base_get_actual_bandwidth_out (EvdSocketBase *self)
{
  return self->priv->bytes_out / 1024.0;
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
