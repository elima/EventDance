/*
 * evd-io-stream-group.h
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

#ifndef __EVD_IO_STREAM_GROUP_H__
#define __EVD_IO_STREAM_GROUP_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdIoStreamGroup EvdIoStreamGroup;
typedef struct _EvdIoStreamGroupClass EvdIoStreamGroupClass;
typedef struct _EvdIoStreamGroupPrivate EvdIoStreamGroupPrivate;

struct _EvdIoStreamGroup
{
  GObject parent;

  EvdIoStreamGroupPrivate *priv;
};

struct _EvdIoStreamGroupClass
{
  GObjectClass parent_class;

  /* virtual methods */
  gboolean (* add)         (EvdIoStreamGroup *self,
                            GIOStream        *io_stream);
  gboolean (* remove)      (EvdIoStreamGroup *self,
                            GIOStream        *io_stream);
};

#define EVD_TYPE_IO_STREAM_GROUP           (evd_io_stream_group_get_type ())
#define EVD_IO_STREAM_GROUP(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_IO_STREAM_GROUP, EvdIoStreamGroup))
#define EVD_IO_STREAM_GROUP_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_IO_STREAM_GROUP, EvdIoStreamGroupClass))
#define EVD_IS_IO_STREAM_GROUP(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_IO_STREAM_GROUP))
#define EVD_IS_IO_STREAM_GROUP_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_IO_STREAM_GROUP))
#define EVD_IO_STREAM_GROUP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_IO_STREAM_GROUP, EvdIoStreamGroupClass))


GType               evd_io_stream_group_get_type         (void) G_GNUC_CONST;

EvdIoStreamGroup   *evd_io_stream_group_new              (void);

gboolean            evd_io_stream_group_add              (EvdIoStreamGroup *self,
                                                          GIOStream        *io_stream);
gboolean            evd_io_stream_group_remove           (EvdIoStreamGroup *self,
                                                          GIOStream        *io_stream);

G_END_DECLS

#endif /* __EVD_IO_STREAM_GROUP_H__ */
