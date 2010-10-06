/*
 * evd-throttled-output-stream.h
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

#ifndef __EVD_THROTTLED_OUTPUT_STREAM_H__
#define __EVD_THROTTLED_OUTPUT_STREAM_H__

#include <glib-object.h>
#include <gio/gio.h>

#include "evd-stream-throttle.h"

G_BEGIN_DECLS

typedef struct _EvdThrottledOutputStream EvdThrottledOutputStream;
typedef struct _EvdThrottledOutputStreamClass EvdThrottledOutputStreamClass;
typedef struct _EvdThrottledOutputStreamPrivate EvdThrottledOutputStreamPrivate;

struct _EvdThrottledOutputStream
{
  GFilterOutputStream parent;

  EvdThrottledOutputStreamPrivate *priv;
};

struct _EvdThrottledOutputStreamClass
{
  GFilterOutputStreamClass parent_class;

  /* signal prototypes */
  void (* delay_write) (EvdThrottledOutputStream *self,
                        guint                    wait,
                        gpointer                 user_data);
};

#define EVD_TYPE_THROTTLED_OUTPUT_STREAM           (evd_throttled_output_stream_get_type ())
#define EVD_THROTTLED_OUTPUT_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_THROTTLED_OUTPUT_STREAM, EvdThrottledOutputStream))
#define EVD_THROTTLED_OUTPUT_STREAM_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_THROTTLED_OUTPUT_STREAM, EvdThrottledOutputStreamClass))
#define EVD_IS_THROTTLED_OUTPUT_STREAM(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_THROTTLED_OUTPUT_STREAM))
#define EVD_IS_THROTTLED_OUTPUT_STREAM_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_THROTTLED_OUTPUT_STREAM))
#define EVD_THROTTLED_OUTPUT_STREAM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_THROTTLED_OUTPUT_STREAM, EvdThrottledOutputStreamClass))


GType                    evd_throttled_output_stream_get_type         (void) G_GNUC_CONST;

EvdThrottledOutputStream *evd_throttled_output_stream_new             (GOutputStream *base_stream);

gsize                    evd_throttled_output_stream_get_max_writable (EvdThrottledOutputStream *self,
                                                                       guint                   *retry_wait);

void                     evd_throttled_output_stream_add_throttle     (EvdThrottledOutputStream *self,
                                                                       EvdStreamThrottle        *throttle);
void                     evd_throttled_output_stream_remove_throttle  (EvdThrottledOutputStream *self,
                                                                       EvdStreamThrottle        *throttle);

G_END_DECLS

#endif /* __EVD_THROTTLED_OUTPUT_STREAM_H__ */
