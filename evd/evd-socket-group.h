/*
 * evd-socket-group.h
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

#ifndef __EVD_SOCKET_GROUP_H__
#define __EVD_SOCKET_GROUP_H__

#include "evd-socket.h"

G_BEGIN_DECLS

typedef struct _EvdSocketGroup EvdSocketGroup;
typedef struct _EvdSocketGroupClass EvdSocketGroupClass;
typedef struct _EvdSocketGroupPrivate EvdSocketGroupPrivate;

struct _EvdSocketGroup
{
  GObject parent;

  /* private structure */
  EvdSocketGroupPrivate *priv;
};

struct _EvdSocketGroupClass
{
  GObjectClass parent_class;

  /* virtual methods */
  void (* add)    (EvdSocketGroup *self, EvdSocket *socket);
  void (* remove) (EvdSocketGroup *self, EvdSocket *socket);
};

#define EVD_TYPE_SOCKET_GROUP           (evd_socket_group_get_type ())
#define EVD_SOCKET_GROUP(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_SOCKET_GROUP, EvdSocketGroup))
#define EVD_SOCKET_GROUP_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_SOCKET_GROUP, EvdSocketGroupClass))
#define EVD_IS_SOCKET_GROUP(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_SOCKET_GROUP))
#define EVD_IS_SOCKET_GROUP_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_SOCKET_GROUP))
#define EVD_SOCKET_GROUP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_SOCKET_GROUP, EvdSocketGroupClass))


GType           evd_socket_group_get_type         (void) G_GNUC_CONST;

EvdSocketGroup *evd_socket_group_new              (void);

void            evd_socket_group_add              (EvdSocketGroup *self,
						   EvdSocket      *socket);
void            evd_socket_group_remove           (EvdSocketGroup *self,
						   EvdSocket      *socket);

void            evd_socket_group_set_read_closure (EvdSocketGroup *self,
						   GClosure       *closure);

G_END_DECLS

#endif /* __EVD_SOCKET_GROUP_H__ */
