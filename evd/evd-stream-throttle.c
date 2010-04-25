/*
 * evd-stream-throttle.c
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

#include <math.h>
#include <string.h>

#include "evd-stream-throttle.h"

G_DEFINE_TYPE (EvdStreamThrottle, evd_stream_throttle, G_TYPE_OBJECT)

#define EVD_STREAM_THROTTLE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                              EVD_TYPE_STREAM_THROTTLE, \
                                              EvdStreamThrottlePrivate))

/* private data */
struct _EvdStreamThrottlePrivate
{
  gsize  bandwidth;
  gulong latency;

  GTimeVal current_time;
  gsize    bytes;
  GTimeVal last;
};

/* properties */
enum
{
  PROP_0,
  PROP_BANDWIDTH,
  PROP_LATENCY,
};

G_LOCK_DEFINE_STATIC (counters);

static void     evd_stream_throttle_class_init         (EvdStreamThrottleClass *class);
static void     evd_stream_throttle_init               (EvdStreamThrottle *self);

static void     evd_stream_throttle_set_property       (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void     evd_stream_throttle_get_property       (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static void
evd_stream_throttle_class_init (EvdStreamThrottleClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->get_property = evd_stream_throttle_get_property;
  obj_class->set_property = evd_stream_throttle_set_property;

  g_object_class_install_property (obj_class, PROP_BANDWIDTH,
                                   g_param_spec_float ("bandwidth",
                                                       "Bandwidth limit",
                                                       "The maximum bandwidth in kilobytes",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_LATENCY,
                                   g_param_spec_float ("latency",
                                                       "Minimum latency",
                                                       "The minimum time between two transfers, in miliseconds",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdStreamThrottlePrivate));
}

static void
evd_stream_throttle_init (EvdStreamThrottle *self)
{
  EvdStreamThrottlePrivate *priv;

  priv = EVD_STREAM_THROTTLE_GET_PRIVATE (self);
  self->priv = priv;

  priv->bandwidth = 0;
  priv->latency = 0;

  priv->current_time.tv_sec = 0;
  priv->current_time.tv_usec = 0;

  priv->bytes = 0;
}

static void
evd_stream_throttle_set_property (GObject      *obj,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EvdStreamThrottle *self;

  self = EVD_STREAM_THROTTLE (obj);

  switch (prop_id)
    {
    case PROP_BANDWIDTH:
      self->priv->bandwidth = (gsize) (g_value_get_float (value) * 1024.0);
      break;

      /* Latency properties are in miliseconds, but we store the value
         internally  in microseconds, to allow up to 1/1000 fraction of a
         milisecond */
    case PROP_LATENCY:
      self->priv->latency = (gulong) (g_value_get_float (value) * 1000.0);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_stream_throttle_get_property (GObject    *obj,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EvdStreamThrottle *self;

  self = EVD_STREAM_THROTTLE (obj);

  switch (prop_id)
    {
    case PROP_BANDWIDTH:
      g_value_set_float (value, self->priv->bandwidth / 1024.0);
      break;

      /* Latency values are stored in microseconds internally */
    case PROP_LATENCY:
      g_value_set_float (value, self->priv->latency / 1000.0);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_stream_throttle_update_current_time (EvdStreamThrottle *self)
{
  GTimeVal time_val;

  g_get_current_time (&time_val);

  if (time_val.tv_sec != self->priv->current_time.tv_sec)
    {
      G_LOCK (counters);

      self->priv->bytes = 0;

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
evd_stream_throttle_request_internal (EvdStreamThrottle *self,
                                      gsize              bandwidth,
                                      gulong             latency,
                                      gsize              bytes,
                                      GTimeVal          *last,
                                      gsize              size,
                                      guint             *wait)
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

/* public methods */

EvdStreamThrottle *
evd_stream_throttle_new (void)
{
  EvdStreamThrottle *self;

  self = g_object_new (EVD_TYPE_STREAM_THROTTLE, NULL);

  return self;
}

gsize
evd_stream_throttle_request  (EvdStreamThrottle *self,
                              gsize              size,
                              guint             *wait)
{
  evd_stream_throttle_update_current_time (self);

  return evd_stream_throttle_request_internal (self,
                                               self->priv->bandwidth,
                                               self->priv->latency,
                                               self->priv->bytes,
                                               &self->priv->last,
                                               size,
                                               wait);
}

void
evd_stream_throttle_report (EvdStreamThrottle *self, gsize size)
{
  evd_stream_throttle_update_current_time (self);

  G_LOCK (counters);

  self->priv->bytes += size;

  g_memmove (&self->priv->last,
             &self->priv->current_time,
             sizeof (GTimeVal));

  G_UNLOCK (counters);
}

gfloat
evd_stream_throttle_get_actual_bandwidth (EvdStreamThrottle *self)
{
  return self->priv->bytes / 1024.0;
}
