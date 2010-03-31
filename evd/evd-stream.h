/*
 * evd-stream.h
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

#ifndef __EVD_STREAM_H__
#define __EVD_STREAM_H__

#include <glib-object.h>

#include "evd-tls-session.h"

G_BEGIN_DECLS

typedef struct _EvdStream EvdStream;
typedef struct _EvdStreamClass EvdStreamClass;
typedef struct _EvdStreamPrivate EvdStreamPrivate;

struct _EvdStream
{
  GObject parent;

  /* private structure */
  EvdStreamPrivate *priv;
};

struct _EvdStreamClass
{
  GObjectClass parent_class;

  GClosureMarshal read_handler_marshal;
  GClosureMarshal write_handler_marshal;

  /* virtual methods */
  void (* read_closure_changed) (EvdStream *self);
  void (* write_closure_changed) (EvdStream *self);
};

#define EVD_TYPE_STREAM           (evd_stream_get_type ())
#define EVD_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_STREAM, EvdStream))
#define EVD_STREAM_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_STREAM, EvdStreamClass))
#define EVD_IS_STREAM(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_STREAM))
#define EVD_IS_STREAM_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_STREAM))
#define EVD_STREAM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_STREAM, EvdStreamClass))


GType          evd_stream_get_type       (void) G_GNUC_CONST;

EvdStream     *evd_stream_new            (void);

/**
 * evd_stream_set_read_handler:
 * @self: The #EvdStream.
 * @callback: (allow-none): The #GCallback to call upon read condition.
 * @user_data: Pointer to arbitrary data to pass in @callback.
 *
 * Specifies a pointer to the function to be invoked when data is waiting
 * to be read from the stream.
 */
void           evd_stream_set_read_handler        (EvdStream *self,
                                                   GCallback  callback,
                                                   gpointer   user_data);

/**
 * evd_stream_set_on_read:
 * @self: The #EvdStream.
 * @closure: (in) (allow-none): The #GClosure to be invoked.
 *
 * Specifies the closure to be invoked when data is waiting to be read from the
 * stream.
 */
void           evd_stream_set_on_read    (EvdStream *self,
                                          GClosure  *closure);

/**
 * evd_stream_get_on_read:
 * @self: The #EvdStream.
 *
 * Return value: (transfer none): A #GClosure representing the current read handler,
 * or NULL.
 */
GClosure      *evd_stream_get_on_read    (EvdStream *self);

/**
 * evd_stream_set_write_handler:
 * @self: The #EvdStream.
 * @callback: (allow-none): The #GCallback to call upon write condition.
 * @user_data: Pointer to arbitrary data to pass in @callback.
 *
 * Specifies a pointer to the function to be invoked when it becomes safe to
 * write data to the stream.
 */
void           evd_stream_set_write_handler (EvdStream *self,
                                             GCallback  callback,
                                             gpointer   user_data);

/**
 * evd_stream_set_on_write:
 * @self: The #EvdStream.
 * @closure: (in) (allow-none): The #GClosure to be invoked.
 *
 * Specifies the closure to be invoked when it becomes safe to write data to the
 * stream.
 */
void           evd_stream_set_on_write   (EvdStream *self,
                                          GClosure  *closure);

/**
 * evd_stream_get_on_write:
 * @self: The #EvdStream.
 *
 * Return value: (transfer none): A #GClosure representing the current write handler,
 * or NULL.
 */
GClosure      *evd_stream_get_on_write   (EvdStream *self);

gsize          evd_stream_request_write  (EvdStream *self,
                                          gsize      size,
                                          guint     *wait);
gsize          evd_stream_request_read   (EvdStream *self,
                                          gsize      size,
                                          guint     *wait);

gulong         evd_stream_get_total_read (EvdStream *self);
gulong         evd_stream_get_total_written (EvdStream *self);

gfloat         evd_stream_get_actual_bandwidth_in  (EvdStream *self);
gfloat         evd_stream_get_actual_bandwidth_out (EvdStream *self);

G_END_DECLS

#endif /* __EVD_STREAM_H__ */
