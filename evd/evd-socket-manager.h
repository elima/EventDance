/*
 * evd-socket-manager.h
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

#ifndef __EVD_SOCKET_MANAGER_H__
#define __EVD_SOCKET_MANAGER_H__

#include <glib-object.h>
#include <evd.h>

G_BEGIN_DECLS

typedef struct _EvdSocketManager EvdSocketManager;
typedef struct _EvdSocketManagerClass EvdSocketManagerClass;
typedef struct _EvdSocketManagerPrivate EvdSocketManagerPrivate;

struct _EvdSocketManager
{
  GObject parent;

  /* private structure */
  EvdSocketManagerPrivate *priv;
};

struct _EvdSocketManagerClass
{
  GObjectClass parent_class;

  /* signal prototypes */
  void (* signal_example) (EvdSocketManager *self,
			   gpointer          some_data);
};

/* error codes */
enum
{
  EVD_SOCKET_ERR_EPOLL_ADD
};

#define EVD_TYPE_SOCKET_MANAGER           (evd_socket_manager_get_type ())
#define EVD_SOCKET_MANAGER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_SOCKET_MANAGER, EvdSocketManager))
#define EVD_SOCKET_MANAGER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_SOCKET_MANAGER, EvdSocketManagerClass))
#define EVD_IS_SOCKET_MANAGER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_SOCKET_MANAGER))
#define EVD_IS_SOCKET_MANAGER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_SOCKET_MANAGER))
#define EVD_SOCKET_MANAGER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_SOCKET_MANAGER, EvdSocketManagerClass))


GType evd_socket_manager_get_type (void) G_GNUC_CONST;

EvdSocketManager *evd_socket_manager_get (void);

void evd_socket_manager_set_callback (GSourceFunc callback);
gboolean evd_socket_manager_add_socket (EvdSocket  *socket,
					GError    **error);
gboolean evd_socket_manager_del_socket (EvdSocket  *socket,
					GError    **error);

void evd_socket_manager_free_event_list (GList *list);


G_END_DECLS

#endif /* __EVD_SOCKET_MANAGER_H__ */
