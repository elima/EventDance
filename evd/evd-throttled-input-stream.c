/*
 * evd-throttled-input-stream.c
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

#include "evd-throttled-input-stream.h"

G_DEFINE_TYPE (EvdThrottledInputStream, evd_throttled_input_stream, G_TYPE_FILTER_INPUT_STREAM)

#define EVD_THROTTLED_INPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                     EVD_TYPE_THROTTLED_INPUT_STREAM, \
                                                     EvdThrottledInputStreamPrivate))

/* private data */
struct _EvdThrottledInputStreamPrivate
{
  GList *stream_throttles;
};

/* signals */
enum
{
  SIGNAL_DELAY_READ,
  SIGNAL_LAST
};

static guint evd_throttled_input_stream_signals[SIGNAL_LAST] = { 0 };

static void     evd_throttled_input_stream_class_init         (EvdThrottledInputStreamClass *class);
static void     evd_throttled_input_stream_init               (EvdThrottledInputStream *self);
static void     evd_throttled_input_stream_dispose            (GObject *obj);

static gssize   evd_throttled_input_stream_read               (GInputStream  *stream,
                                                               void          *buffer,
                                                               gsize          size,
                                                               GCancellable  *cancellable,
                                                               GError       **error);

static void
evd_throttled_input_stream_class_init (EvdThrottledInputStreamClass *class)
{
  GObjectClass *obj_class;
  GInputStreamClass *input_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_throttled_input_stream_dispose;

  input_stream_class = G_INPUT_STREAM_CLASS (class);
  input_stream_class->read_fn = evd_throttled_input_stream_read;

  evd_throttled_input_stream_signals[SIGNAL_DELAY_READ] =
    g_signal_new ("delay-read",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdThrottledInputStreamClass, delay_read),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE,
                  1, G_TYPE_UINT);

  g_type_class_add_private (obj_class, sizeof (EvdThrottledInputStreamPrivate));
}

static void
evd_throttled_input_stream_init (EvdThrottledInputStream *self)
{
  EvdThrottledInputStreamPrivate *priv;

  priv = EVD_THROTTLED_INPUT_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  priv->stream_throttles = NULL;
}

static void
evd_throttled_input_stream_dispose (GObject *obj)
{
  EvdThrottledInputStream *self = EVD_THROTTLED_INPUT_STREAM (obj);
  GList *node;

  node = self->priv->stream_throttles;
  while (node != NULL)
    {
      g_object_unref (G_OBJECT (node->data));
      node = node->next;
    }
  g_list_free (self->priv->stream_throttles);

  G_OBJECT_CLASS (evd_throttled_input_stream_parent_class)->dispose (obj);
}

static void
evd_throttled_input_stream_report_size (EvdStreamThrottle *throttle,
                                        gsize             *size)
{
  evd_stream_throttle_report (throttle, *size);
}

static gsize
evd_throttled_input_stream_get_max_readable_priv (EvdThrottledInputStream *self,
                                                  gsize                    size,
                                                  guint                   *retry_wait)
{
  GList *node;

  node = self->priv->stream_throttles;
  while (node != NULL)
    {
      EvdStreamThrottle *throttle;

      throttle = EVD_STREAM_THROTTLE (node->data);

      size = MIN (size,
                  evd_stream_throttle_request (throttle,
                                               size,
                                               retry_wait));

      node = node->next;
    }

  return size;
}

static gssize
evd_throttled_input_stream_read (GInputStream  *stream,
                                 void          *buffer,
                                 gsize          size,
                                 GCancellable  *cancellable,
                                 GError       **error)
{
  EvdThrottledInputStream *self = EVD_THROTTLED_INPUT_STREAM (stream);
  gssize actual_size = 0;
  gsize limited_size;
  guint wait = 0;

  if (size == 0)
    return 0;

  limited_size = MIN (size,
                      evd_throttled_input_stream_get_max_readable_priv (
                                            EVD_THROTTLED_INPUT_STREAM (stream),
                                            size,
                                            &wait));

  if (limited_size > 0)
    {
      GInputStream *base_stream;

      base_stream =
        g_filter_input_stream_get_base_stream (G_FILTER_INPUT_STREAM (stream));

      actual_size = g_input_stream_read (base_stream,
                                         buffer,
                                         limited_size,
                                         cancellable,
                                         error);

      if (actual_size > 0)
        {
          g_list_foreach (self->priv->stream_throttles,
                          (GFunc) evd_throttled_input_stream_report_size,
                          &actual_size);
        }
    }
  else
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_WOULD_BLOCK,
                           "Resource temporarily unavailable");
      actual_size = -1;
    }

  if (wait > 0)
    {
      g_signal_emit (self,
                     evd_throttled_input_stream_signals[SIGNAL_DELAY_READ],
                     0,
                     wait,
                     NULL);
    }

  return actual_size;
}

/* public methods */

EvdThrottledInputStream *
evd_throttled_input_stream_new (GInputStream *base_stream)
{
  EvdThrottledInputStream *self;

  g_return_val_if_fail (G_IS_INPUT_STREAM (base_stream), NULL);

  self = g_object_new (EVD_TYPE_THROTTLED_INPUT_STREAM,
                       "base-stream", base_stream,
                       NULL);

  return self;
}

/**
 * evd_throttled_input_stream_get_max_readable:
 * @retry_wait: (out):
 *
 **/
gsize
evd_throttled_input_stream_get_max_readable (EvdThrottledInputStream *self,
                                             guint                   *retry_wait)
{
  g_return_val_if_fail (EVD_IS_THROTTLED_INPUT_STREAM (self), 0);

  return evd_throttled_input_stream_get_max_readable_priv (self,
                                                           G_MAXSSIZE,
                                                           retry_wait);
}

void
evd_throttled_input_stream_add_throttle (EvdThrottledInputStream *self,
                                         EvdStreamThrottle       *throttle)
{
  g_return_if_fail (EVD_IS_THROTTLED_INPUT_STREAM (self));
  g_return_if_fail (EVD_IS_STREAM_THROTTLE (throttle));

  if (g_list_find (self->priv->stream_throttles, throttle) == NULL)
    {
      g_object_ref (throttle);

      self->priv->stream_throttles = g_list_prepend (self->priv->stream_throttles,
                                                     (gpointer) throttle);
    }
}

void
evd_throttled_input_stream_remove_throttle (EvdThrottledInputStream *self,
                                            EvdStreamThrottle       *throttle)
{
  g_return_if_fail (EVD_IS_THROTTLED_INPUT_STREAM (self));
  g_return_if_fail (EVD_IS_STREAM_THROTTLE (throttle));

  if (g_list_find (self->priv->stream_throttles, throttle) != NULL)
    {
      self->priv->stream_throttles = g_list_remove (self->priv->stream_throttles,
                                                    (gpointer) throttle);

      g_object_unref (throttle);
    }
}
