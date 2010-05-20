/*
 * evd-stream-throttle.h
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

#ifndef __EVD_STREAM_THROTTLE_H__
#define __EVD_STREAM_THROTTLE_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _EvdStreamThrottle EvdStreamThrottle;
typedef struct _EvdStreamThrottleClass EvdStreamThrottleClass;
typedef struct _EvdStreamThrottlePrivate EvdStreamThrottlePrivate;

struct _EvdStreamThrottle
{
  GObject parent;

  EvdStreamThrottlePrivate *priv;
};

struct _EvdStreamThrottleClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_STREAM_THROTTLE           (evd_stream_throttle_get_type ())
#define EVD_STREAM_THROTTLE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_STREAM_THROTTLE, EvdStreamThrottle))
#define EVD_STREAM_THROTTLE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_STREAM_THROTTLE, EvdStreamThrottleClass))
#define EVD_IS_STREAM_THROTTLE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_STREAM_THROTTLE))
#define EVD_IS_STREAM_THROTTLE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_STREAM_THROTTLE))
#define EVD_STREAM_THROTTLE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_STREAM_THROTTLE, EvdStreamThrottleClass))


GType              evd_stream_throttle_get_type             (void) G_GNUC_CONST;

EvdStreamThrottle *evd_stream_throttle_new                  (void);

gsize              evd_stream_throttle_request              (EvdStreamThrottle *self,
                                                             gsize              size,
                                                             guint             *wait);
void               evd_stream_throttle_report               (EvdStreamThrottle *self,
                                                             gsize              size);

gfloat             evd_stream_throttle_get_actual_bandwidth (EvdStreamThrottle *self);

G_END_DECLS

#endif /* __EVD_STREAM_THROTTLE_H__ */
