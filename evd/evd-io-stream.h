/*
 * evd-io-stream.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011-2012, Igalia S.L.
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

#ifndef __EVD_IO_STREAM_H__
#define __EVD_IO_STREAM_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <evd-stream-throttle.h>
#include <evd-io-stream-group.h>

G_BEGIN_DECLS

typedef struct _EvdIoStream EvdIoStream;
typedef struct _EvdIoStreamClass EvdIoStreamClass;
typedef struct _EvdIoStreamPrivate EvdIoStreamPrivate;

struct _EvdIoStream
{
  GIOStream parent;

  EvdIoStreamPrivate *priv;
};

struct _EvdIoStreamClass
{
  GIOStreamClass parent_class;

  /* virtual methods */
  void (* group_changed) (EvdIoStream      *self,
                          EvdIoStreamGroup *new_group,
                          EvdIoStreamGroup *old_group);

  /* signals */
  void (* signal_group_changed) (EvdIoStream      *self,
                                 EvdIoStreamGroup *new_group,
                                 EvdIoStreamGroup *old_group,
                                 gpointer          user_data);

  /* padding for future expansion */
  void (* _padding_0_) (void);
  void (* _padding_1_) (void);
  void (* _padding_2_) (void);
  void (* _padding_3_) (void);
  void (* _padding_4_) (void);
  void (* _padding_5_) (void);
};

#define EVD_TYPE_IO_STREAM           (evd_io_stream_get_type ())
#define EVD_IO_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_IO_STREAM, EvdIoStream))
#define EVD_IO_STREAM_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_IO_STREAM, EvdIoStreamClass))
#define EVD_IS_IO_STREAM(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_IO_STREAM))
#define EVD_IS_IO_STREAM_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_IO_STREAM))
#define EVD_IO_STREAM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_IO_STREAM, EvdIoStreamClass))


GType                      evd_io_stream_get_type                 (void) G_GNUC_CONST;

EvdStreamThrottle *        evd_io_stream_get_input_throttle       (EvdIoStream *self);
EvdStreamThrottle *        evd_io_stream_get_output_throttle      (EvdIoStream *self);

gboolean                   evd_io_stream_set_group                (EvdIoStream      *self,
                                                                   EvdIoStreamGroup *group);
EvdIoStreamGroup *         evd_io_stream_get_group                (EvdIoStream *self);

G_END_DECLS

#endif /* __EVD_IO_STREAM_H__ */
