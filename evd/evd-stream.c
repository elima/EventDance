/*
 * evd-stream.c
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
#include "evd-stream.h"

#define DOMAIN_QUARK_STRING "org.eventdance.glib.stream"

G_DEFINE_ABSTRACT_TYPE (EvdStream, evd_stream, G_TYPE_OBJECT)

#define EVD_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
	                             EVD_TYPE_STREAM, \
                                     EvdStreamPrivate))

/* private data */
struct _EvdStreamPrivate
{
  GClosure *receive_closure;

  gsize  bandwidth_in;
  gsize  bandwidth_out;
  guint  latency_in;
  guint  latency_out;

  GTimeVal current_time;
  gsize    bytes_in;
  gsize    bytes_out;
  gsize    last_in;
  gsize    last_out;

  gsize total_in;
  gsize total_out;

  GMutex *mutex;
};


/* properties */
enum
{
  PROP_0,
  PROP_RECEIVE_CLOSURE,
  PROP_BANDWIDTH_IN,
  PROP_BANDWIDTH_OUT
};

static void     evd_stream_class_init         (EvdStreamClass *class);
static void     evd_stream_init               (EvdStream *self);

static void     evd_stream_finalize           (GObject *obj);
static void     evd_stream_dispose            (GObject *obj);

static void     evd_stream_set_property       (GObject      *obj,
					       guint         prop_id,
					       const GValue *value,
					       GParamSpec   *pspec);
static void     evd_stream_get_property       (GObject    *obj,
					       guint       prop_id,
					       GValue     *value,
					       GParamSpec *pspec);

static void
evd_stream_class_init (EvdStreamClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_stream_dispose;
  obj_class->finalize = evd_stream_finalize;
  obj_class->get_property = evd_stream_get_property;
  obj_class->set_property = evd_stream_set_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_RECEIVE_CLOSURE,
                                   g_param_spec_boxed ("receive",
						       "Receive closure",
						       "The callback closure that will be invoked when data is ready to be read",
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

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdStreamPrivate));
}

static void
evd_stream_init (EvdStream *self)
{
  EvdStreamPrivate *priv;

  priv = EVD_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->receive_closure = NULL;

  priv->bandwidth_in = 0;
  priv->bandwidth_out = 0;
  priv->latency_in = 0;
  priv->latency_out = 0;

  priv->current_time.tv_sec = 0;
  priv->current_time.tv_usec = 0;

  priv->bytes_in = 0;
  priv->bytes_out = 0;
  priv->last_in = 0;
  priv->last_out = 0;

  priv->mutex = g_mutex_new ();
}

static void
evd_stream_dispose (GObject *obj)
{
  EvdStream *self = EVD_STREAM (obj);

  evd_stream_set_on_receive (self, NULL);

  G_OBJECT_CLASS (evd_stream_parent_class)->dispose (obj);
}

static void
evd_stream_finalize (GObject *obj)
{
  EvdStream *self = EVD_STREAM (obj);

  g_mutex_free (self->priv->mutex);

  G_OBJECT_CLASS (evd_stream_parent_class)->finalize (obj);
}

static void
evd_stream_set_property (GObject      *obj,
			 guint         prop_id,
			 const GValue *value,
			 GParamSpec   *pspec)
{
  EvdStream *self;

  self = EVD_STREAM (obj);

  switch (prop_id)
    {
    case PROP_RECEIVE_CLOSURE:
      evd_stream_set_on_receive (self, g_value_get_boxed (value));
      break;

    case PROP_BANDWIDTH_IN:
      self->priv->bandwidth_in = (gsize) (g_value_get_float (value) * 1024.0);
      break;

    case PROP_BANDWIDTH_OUT:
      self->priv->bandwidth_out = (gsize) (g_value_get_float (value) * 1024.0);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_stream_get_property (GObject    *obj,
			 guint       prop_id,
			 GValue     *value,
			 GParamSpec *pspec)
{
  EvdStream *self;

  self = EVD_STREAM (obj);

  switch (prop_id)
    {
    case PROP_RECEIVE_CLOSURE:
      g_value_set_boxed (value, self->priv->receive_closure);
      break;

    case PROP_BANDWIDTH_IN:
      g_value_set_float (value, self->priv->bandwidth_in / 1024.0);
      break;

    case PROP_BANDWIDTH_OUT:
      g_value_set_float (value, self->priv->bandwidth_out / 1024.0);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_stream_update_current_time (EvdStream *self)
{
  GTimeVal time_val;

  g_get_current_time (&time_val);

  if (time_val.tv_sec != self->priv->current_time.tv_sec)
    {
      g_mutex_lock (self->priv->mutex);

      self->priv->bytes_in = 0;
      self->priv->bytes_out = 0;

      g_mutex_unlock (self->priv->mutex);
    }

  g_memmove (&self->priv->current_time, &time_val, sizeof (GTimeVal));
}

static gsize
evd_stream_request (EvdStream *self,
		    gsize      bandwidth,
		    guint      latency,
		    gsize      bytes,
		    gsize      last,
		    gsize      size,
		    guint     *wait)
{
  gsize actual_size = size;

  if (wait != NULL)
    *wait = 0;

  if (bandwidth > 0)
    {
      /* bandwidth check */
      g_mutex_lock (self->priv->mutex);

      actual_size = MAX ((gssize) (bandwidth - bytes), 0);
      actual_size = MIN (actual_size, size);

      if (wait != NULL)
	if (actual_size < size)
	  *wait = (guint) (((1000001 - self->priv->current_time.tv_usec) / 1000)) + 1;

      g_mutex_unlock (self->priv->mutex);
    }

  /* TODO: check latency */

  return actual_size;
}

/* protected methods */

void
evd_stream_report_read (EvdStream *self,
			gsize     size)
{
  evd_stream_update_current_time (self);

  g_mutex_lock (self->priv->mutex);

  self->priv->bytes_in += size;
  self->priv->total_in += size;

  g_mutex_unlock (self->priv->mutex);
}

void
evd_stream_report_write (EvdStream *self,
			 gsize     size)
{
  evd_stream_update_current_time (self);

  g_mutex_lock (self->priv->mutex);

  self->priv->bytes_out += size;
  self->priv->total_out += size;

  g_mutex_unlock (self->priv->mutex);
}

/* public methods */

EvdStream *
evd_stream_new (void)
{
  EvdStream *self;

  self = g_object_new (EVD_TYPE_STREAM, NULL);

  return self;
}

void
evd_stream_set_on_receive (EvdStream *self,
			   GClosure  *closure)
{
  g_return_if_fail (EVD_IS_STREAM (self));

  if (self->priv->receive_closure != NULL)
    g_closure_unref (self->priv->receive_closure);

  if (closure != NULL)
    g_closure_ref (closure);

  self->priv->receive_closure = closure;
}

GClosure *
evd_stream_get_on_receive (EvdStream *self)
{
  g_return_val_if_fail (EVD_IS_STREAM (self), NULL);

  return self->priv->receive_closure;
}

gsize
evd_stream_request_read  (EvdStream *self,
			  gsize      size,
			  guint     *wait)
{
  evd_stream_update_current_time (self);

  return evd_stream_request (self,
			     self->priv->bandwidth_in,
			     self->priv->latency_in,
			     self->priv->bytes_in,
			     self->priv->last_in,
			     size,
			     wait);
}

gsize
evd_stream_request_write (EvdStream *self,
			  gsize      size,
			  guint     *wait)
{
  evd_stream_update_current_time (self);

  return evd_stream_request (self,
			     self->priv->bandwidth_out,
			     self->priv->latency_out,
			     self->priv->bytes_out,
			     self->priv->last_out,
			     size,
			     wait);
}

gulong
evd_stream_get_total_read (EvdStream *self)
{
  return self->priv->total_in;
}

gulong
evd_stream_get_total_written (EvdStream *self)
{
  return self->priv->total_out;
}

gfloat
evd_stream_get_actual_bandwidth_in (EvdStream *self)
{
  return self->priv->bytes_in / 1024.0;
}

gfloat
evd_stream_get_actual_bandwidth_out (EvdStream *self)
{
  return self->priv->bytes_out / 1024.0;
}
