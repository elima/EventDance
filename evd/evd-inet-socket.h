/*
 * evd-inet-socket.h
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

#ifndef __EVD_INET_SOCKET_H__
#define __EVD_INET_SOCKET_H__

#include "evd-socket.h"

G_BEGIN_DECLS

typedef struct _EvdInetSocket EvdInetSocket;
typedef struct _EvdInetSocketClass EvdInetSocketClass;

struct _EvdInetSocket
{
  EvdSocket parent;
};

struct _EvdInetSocketClass
{
  EvdSocketClass parent_class;
};

#define EVD_TYPE_INET_SOCKET           (evd_inet_socket_get_type ())
#define EVD_INET_SOCKET(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_INET_SOCKET, EvdInetSocket))
#define EVD_INET_SOCKET_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_INET_SOCKET, EvdInetSocketClass))
#define EVD_IS_INET_SOCKET(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_INET_SOCKET))
#define EVD_IS_INET_SOCKET_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_INET_SOCKET))
#define EVD_INET_SOCKET_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_INET_SOCKET, EvdInetSocketClass))


GType             evd_inet_socket_get_type         (void) G_GNUC_CONST;

EvdInetSocket    *evd_inet_socket_new              (void);

gboolean          evd_inet_socket_connect_to       (EvdInetSocket  *self,
						    const gchar    *address,
						    guint           port,
						    GError        **error);
gboolean          evd_inet_socket_bind             (EvdInetSocket  *self,
						    const gchar    *address,
						    guint           port,
						    gboolean        allow_reuse,
						    GError        **error);
gboolean          evd_inet_socket_listen           (EvdInetSocket  *self,
						    const gchar    *address,
						    guint           port,
						    GError        **error);

G_END_DECLS

#endif /* __EVD_INET_SOCKET_H__ */
