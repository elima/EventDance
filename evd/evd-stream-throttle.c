/*
 * evd-stream-throttle.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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

#include <math.h>
#include <string.h>

#include "evd-stream-throttle.h"

/* private data */
struct _EvdStreamThrottlePrivate
{
  gsize  bandwidth;
  gulong latency;

  gint64  current_time_us;
  gsize   bytes;
  gsize   actual_bandwidth;
  guint64 total;
  gint64  last_time_us;
};

G_DEFINE_TYPE_WITH_PRIVATE (EvdStreamThrottle,
                            evd_stream_throttle,
                            G_TYPE_OBJECT)

/* properties */
enum
{
  PROP_0,
  PROP_BANDWIDTH,
  PROP_LATENCY,
  PROP_TOTAL
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

  g_object_class_install_property (obj_class, PROP_TOTAL,
                                   g_param_spec_uint64 ("total",
                                                        "Total transferred",
                                                        "Total sum of bytes ever reported to this stream-throttle",
                                                        0,
                                                        G_MAXUINT64,
                                                        0,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
evd_stream_throttle_init (EvdStreamThrottle *self)
{
  EvdStreamThrottlePrivate *priv;

  priv = evd_stream_throttle_get_instance_private (self);
  self->priv = priv;

  priv->bandwidth = 0;
  priv->latency = 0;

  priv->current_time_us = 0;

  priv->bytes = 0;
  priv->total = 0;
  priv->actual_bandwidth = 0;

  priv->last_time_us = 0;
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

    case PROP_TOTAL:
      g_value_set_uint64 (value, evd_stream_throttle_get_total (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_stream_throttle_update_current_time (EvdStreamThrottle *self)
{
  gint64 now_us;

  now_us = g_get_real_time ();

  if ((self->priv->current_time_us / G_USEC_PER_SEC) != (now_us / G_USEC_PER_SEC))
    {
      G_LOCK (counters);

      self->priv->actual_bandwidth = self->priv->bytes;
      self->priv->bytes = 0;

      G_UNLOCK (counters);
    }

  self->priv->current_time_us = now_us;
}

static gsize
evd_stream_throttle_request_internal (EvdStreamThrottle *self,
                                      gsize              bandwidth,
                                      gulong             latency,
                                      gsize              bytes,
                                      gint64            *last_time_us,
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

      if (*last_time_us == 0)
        elapsed = G_MAXULONG;
      else
        {
          gint64 diff = self->priv->current_time_us - *last_time_us;
          if (diff < 0)
            diff = -diff;
          elapsed = (gulong) diff;
        }

      if (elapsed < latency)
        {
          actual_size = 0;

          if (wait != NULL)
            *wait = MAX ((gulong) (latency - elapsed) / 1000 + 2,
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
          {
            gint64 micro_part = self->priv->current_time_us % G_USEC_PER_SEC;
            guint remaining_ms =
              (guint) (((G_USEC_PER_SEC + 1) - micro_part) / 1000);
            *wait = MAX (remaining_ms + 1, *wait);
          }
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
  g_return_val_if_fail (EVD_IS_STREAM_THROTTLE (self), -1);

  evd_stream_throttle_update_current_time (self);

  return evd_stream_throttle_request_internal (self,
                                               self->priv->bandwidth,
                                               self->priv->latency,
                                               self->priv->bytes,
                                               &self->priv->last_time_us,
                                               size,
                                               wait);
}

void
evd_stream_throttle_report (EvdStreamThrottle *self, gsize size)
{
  g_return_if_fail (EVD_IS_STREAM_THROTTLE (self));

  evd_stream_throttle_update_current_time (self);

  G_LOCK (counters);

  self->priv->bytes += size;
  self->priv->total += size;

  self->priv->last_time_us = self->priv->current_time_us;

  G_UNLOCK (counters);
}

gfloat
evd_stream_throttle_get_actual_bandwidth (EvdStreamThrottle *self)
{
  g_return_val_if_fail (EVD_IS_STREAM_THROTTLE (self), -1.0);

  return self->priv->actual_bandwidth / 1024.0;
}

guint64
evd_stream_throttle_get_total (EvdStreamThrottle *self)
{
  g_return_val_if_fail (EVD_IS_STREAM_THROTTLE (self), 0);

  return self->priv->total;
}
