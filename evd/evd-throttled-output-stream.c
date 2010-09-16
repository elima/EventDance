/*
 * evd-throttled-output-stream.c
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

#include "evd-throttled-output-stream.h"

G_DEFINE_TYPE (EvdThrottledOutputStream, evd_throttled_output_stream, G_TYPE_FILTER_OUTPUT_STREAM)

#define EVD_THROTTLED_OUTPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                      EVD_TYPE_THROTTLED_OUTPUT_STREAM, \
                                                      EvdThrottledOutputStreamPrivate))

/* private data */
struct _EvdThrottledOutputStreamPrivate
{
  GList *stream_throttles;
};

/* signals */
enum
{
  SIGNAL_DELAY_WRITE,
  SIGNAL_LAST
};

static guint evd_throttled_output_stream_signals[SIGNAL_LAST] = { 0 };

static void     evd_throttled_output_stream_class_init         (EvdThrottledOutputStreamClass *class);
static void     evd_throttled_output_stream_init               (EvdThrottledOutputStream *self);
static void     evd_throttled_output_stream_dispose            (GObject *obj);

static gssize   evd_throttled_output_stream_write              (GOutputStream  *stream,
                                                                const void     *buffer,
                                                                gsize          size,
                                                                GCancellable  *cancellable,
                                                                GError       **error);

static void
evd_throttled_output_stream_class_init (EvdThrottledOutputStreamClass *class)
{
  GObjectClass *obj_class;
  GOutputStreamClass *output_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_throttled_output_stream_dispose;

  output_stream_class = G_OUTPUT_STREAM_CLASS (class);
  output_stream_class->write_fn = evd_throttled_output_stream_write;
  output_stream_class->flush_async = NULL;
  output_stream_class->flush_finish = NULL;

  evd_throttled_output_stream_signals[SIGNAL_DELAY_WRITE] =
    g_signal_new ("delay-write",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdThrottledOutputStreamClass, delay_write),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE,
                  1, G_TYPE_UINT);

  g_type_class_add_private (obj_class, sizeof (EvdThrottledOutputStreamPrivate));
}

static void
evd_throttled_output_stream_init (EvdThrottledOutputStream *self)
{
  EvdThrottledOutputStreamPrivate *priv;

  priv = EVD_THROTTLED_OUTPUT_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  priv->stream_throttles = NULL;
}

static void
evd_throttled_output_stream_dispose (GObject *obj)
{
  EvdThrottledOutputStream *self = EVD_THROTTLED_OUTPUT_STREAM (obj);
  GList *node;

  node = self->priv->stream_throttles;
  while (node != NULL)
    {
      g_object_unref (G_OBJECT (node->data));
      node = node->next;
    }
  g_list_free (self->priv->stream_throttles);

  G_OBJECT_CLASS (evd_throttled_output_stream_parent_class)->dispose (obj);
}

static void
evd_throttled_output_stream_report_size (EvdStreamThrottle *throttle,
                                         gsize             *size)
{
  evd_stream_throttle_report (throttle, *size);
}

static gsize
evd_throttled_output_stream_get_max_writable_priv (EvdThrottledOutputStream *self,
                                                   gsize                    size,
                                                   guint                   *retry_wait)
{
  GList *node;
  guint _retry_wait = 0;

  node = self->priv->stream_throttles;
  while (node != NULL)
    {
      EvdStreamThrottle *throttle;

      throttle = EVD_STREAM_THROTTLE (node->data);

      size = MIN (size,
                  evd_stream_throttle_request (throttle,
                                               size,
                                               &_retry_wait));

      node = node->next;
    }

  if (_retry_wait > 0)
    {
      g_signal_emit (self,
                     evd_throttled_output_stream_signals[SIGNAL_DELAY_WRITE],
                     0,
                     _retry_wait,
                     NULL);
    }

  if (retry_wait != NULL)
    *retry_wait = _retry_wait;

  return size;
}

static gssize
evd_throttled_output_stream_write (GOutputStream  *stream,
                                   const void     *buffer,
                                   gsize          size,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  EvdThrottledOutputStream *self = EVD_THROTTLED_OUTPUT_STREAM (stream);
  gssize actual_size = 0;
  gsize limited_size;
  guint wait = 0;

  if (size == 0)
    return 0;

  limited_size = MIN (size,
                      evd_throttled_output_stream_get_max_writable_priv (
                                            EVD_THROTTLED_OUTPUT_STREAM (stream),
                                            size,
                                            &wait));

  if (limited_size > 0)
    {
      GOutputStream *base_stream;

      base_stream =
        g_filter_output_stream_get_base_stream (G_FILTER_OUTPUT_STREAM (stream));

      actual_size = g_output_stream_write (base_stream,
                                           buffer,
                                           limited_size,
                                           cancellable,
                                           error);

      if (actual_size > 0)
        {
          g_list_foreach (self->priv->stream_throttles,
                          (GFunc) evd_throttled_output_stream_report_size,
                          &actual_size);
        }
    }

  return actual_size;
}

/* public methods */

EvdThrottledOutputStream *
evd_throttled_output_stream_new (GOutputStream *base_stream)
{
  EvdThrottledOutputStream *self;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (base_stream), NULL);

  self = g_object_new (EVD_TYPE_THROTTLED_OUTPUT_STREAM,
                       "base-stream", base_stream,
                       NULL);

  return self;
}

/**
 * evd_throttled_output_stream_get_max_readable:
 * @retry_wait: (out):
 *
 **/
gsize
evd_throttled_output_stream_get_max_writable (EvdThrottledOutputStream *self,
                                              guint                   *retry_wait)
{
  g_return_val_if_fail (EVD_IS_THROTTLED_OUTPUT_STREAM (self), 0);

  return evd_throttled_output_stream_get_max_writable_priv (self,
                                                            G_MAXSSIZE,
                                                            retry_wait);
}

void
evd_throttled_output_stream_add_throttle (EvdThrottledOutputStream *self,
                                          EvdStreamThrottle       *throttle)
{
  g_return_if_fail (EVD_IS_THROTTLED_OUTPUT_STREAM (self));
  g_return_if_fail (EVD_IS_STREAM_THROTTLE (throttle));

  if (g_list_find (self->priv->stream_throttles, throttle) == NULL)
    {
      g_object_ref (throttle);

      self->priv->stream_throttles = g_list_prepend (self->priv->stream_throttles,
                                                     (gpointer) throttle);
    }
}

void
evd_throttled_output_stream_remove_throttle (EvdThrottledOutputStream *self,
                                             EvdStreamThrottle       *throttle)
{
  g_return_if_fail (EVD_IS_THROTTLED_OUTPUT_STREAM (self));
  g_return_if_fail (EVD_IS_STREAM_THROTTLE (throttle));

  if (g_list_find (self->priv->stream_throttles, throttle) != NULL)
    {
      self->priv->stream_throttles = g_list_remove (self->priv->stream_throttles,
                                                    (gpointer) throttle);

      g_object_unref (throttle);
    }
}
