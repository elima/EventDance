/*
 * evd-connection-group.h
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

#ifndef __EVD_CONNECTION_GROUP_H__
#define __EVD_CONNECTION_GROUP_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdConnectionGroup EvdConnectionGroup;
typedef struct _EvdConnectionGroupClass EvdConnectionGroupClass;
typedef struct _EvdConnectionGroupPrivate EvdConnectionGroupPrivate;

struct _EvdConnectionGroup
{
  GObject parent;

  EvdConnectionGroupPrivate *priv;
};

struct _EvdConnectionGroupClass
{
  GObjectClass parent_class;

  /* virtual methods */
  gboolean (* add)         (EvdConnectionGroup *self,
                            GIOStream          *io_stream);
  gboolean (* remove)      (EvdConnectionGroup *self,
                            GIOStream          *io_stream);
};

#define EVD_TYPE_CONNECTION_GROUP           (evd_connection_group_get_type ())
#define EVD_CONNECTION_GROUP(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_CONNECTION_GROUP, EvdConnectionGroup))
#define EVD_CONNECTION_GROUP_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_CONNECTION_GROUP, EvdConnectionGroupClass))
#define EVD_IS_CONNECTION_GROUP(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_CONNECTION_GROUP))
#define EVD_IS_CONNECTION_GROUP_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_CONNECTION_GROUP))
#define EVD_CONNECTION_GROUP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_CONNECTION_GROUP, EvdConnectionGroupClass))


GType               evd_connection_group_get_type         (void) G_GNUC_CONST;

EvdConnectionGroup *evd_connection_group_new              (void);

gboolean            evd_connection_group_add              (EvdConnectionGroup *self,
                                                           GIOStream          *io_stream);
gboolean            evd_connection_group_remove           (EvdConnectionGroup *self,
                                                           GIOStream          *io_stream);

G_END_DECLS

#endif /* __EVD_CONNECTION_GROUP_H__ */
