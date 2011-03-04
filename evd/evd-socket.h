/*
 * evd-socket.h
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

#ifndef __EVD_SOCKET_H__
#define __EVD_SOCKET_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdSocket EvdSocket;
typedef struct _EvdSocketClass EvdSocketClass;
typedef struct _EvdSocketPrivate EvdSocketPrivate;
typedef struct _EvdSocketEvent EvdSocketEvent;

typedef void (* EvdSocketNotifyConditionCallback) (EvdSocket    *self,
                                                   GIOCondition  condition,
                                                   gpointer      user_data);

/* socket states */
typedef enum
{
  EVD_SOCKET_STATE_CLOSED,
  EVD_SOCKET_STATE_CONNECTING,
  EVD_SOCKET_STATE_CONNECTED,
  EVD_SOCKET_STATE_RESOLVING,
  EVD_SOCKET_STATE_BOUND,
  EVD_SOCKET_STATE_LISTENING,
  EVD_SOCKET_STATE_TLS_HANDSHAKING,
  EVD_SOCKET_STATE_CLOSING
} EvdSocketState;

struct _EvdSocket
{
  GObject parent;

  /* private structure */
  EvdSocketPrivate *priv;
};

struct _EvdSocketClass
{
  GObjectClass parent_class;

  /* virtual methods */
  gboolean (* handle_condition) (EvdSocket *self, GIOCondition condition);
  gboolean (* cleanup)          (EvdSocket *self, GError **error);

  /* signal prototypes */
  void (* error)          (EvdSocket *self,
                           guint32    error_domain,
                           gint       error_code,
                           gchar     *error_message,
                           gpointer   user_data);
  void (* state_changed)  (EvdSocket      *self,
                           EvdSocketState  new_state,
                           EvdSocketState  old_state);
  void (* close)          (EvdSocket *self);
  void (* new_connection) (EvdSocket *self,
                           GIOStream *socket,
                           gpointer   user_data);
};

#define EVD_TYPE_SOCKET           (evd_socket_get_type ())
#define EVD_SOCKET(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_SOCKET, EvdSocket))
#define EVD_SOCKET_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_SOCKET, EvdSocketClass))
#define EVD_IS_SOCKET(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_SOCKET))
#define EVD_IS_SOCKET_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_SOCKET))
#define EVD_SOCKET_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_SOCKET, EvdSocketClass))


GType           evd_socket_get_type                      (void) G_GNUC_CONST;

EvdSocket      *evd_socket_new                           (void);

GSocket        *evd_socket_get_socket                    (EvdSocket *self);
GSocketFamily   evd_socket_get_family                    (EvdSocket *self);
EvdSocketState  evd_socket_get_status                    (EvdSocket *self);

gint            evd_socket_get_priority                  (EvdSocket *self);
void            evd_socket_set_priority                  (EvdSocket *self,
                                                          gint       priority);

gboolean        evd_socket_close                         (EvdSocket  *self,
                                                          GError    **error);

GSocketAddress *evd_socket_get_remote_address            (EvdSocket  *self,
                                                          GError    **error);
GSocketAddress *evd_socket_get_local_address             (EvdSocket  *self,
                                                          GError    **error);

gboolean        evd_socket_shutdown                      (EvdSocket  *self,
                                                          gboolean    shutdown_read,
                                                          gboolean    shutdown_write,
                                                          GError    **error);

gboolean        evd_socket_watch_condition               (EvdSocket     *self,
                                                          GIOCondition   cond,
                                                          GError       **error);
GIOCondition    evd_socket_get_condition                 (EvdSocket *self);

void            evd_socket_set_notify_condition_callback (EvdSocket                        *self,
                                                          EvdSocketNotifyConditionCallback  callback,
                                                          gpointer                          user_data);

gboolean        evd_socket_bind_addr                     (EvdSocket       *self,
                                                          GSocketAddress  *address,
                                                          gboolean         allow_reuse,
                                                          GError         **error);
void            evd_socket_bind_async                    (EvdSocket           *self,
                                                          const gchar         *address,
                                                          GCancellable        *cancellable,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);
gboolean        evd_socket_bind_finish                   (EvdSocket     *self,
                                                          GAsyncResult  *result,
                                                          GError       **error);

gboolean        evd_socket_listen_addr                   (EvdSocket       *self,
                                                          GSocketAddress  *address,
                                                          GError         **error);
void            evd_socket_listen_async                  (EvdSocket           *self,
                                                          const gchar         *address,
                                                          GCancellable        *cancellable,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);
gboolean        evd_socket_listen_finish                 (EvdSocket     *self,
                                                          GAsyncResult  *result,
                                                          GError       **error);

void            evd_socket_connect_async                 (EvdSocket           *self,
                                                          const gchar         *address,
                                                          GCancellable        *cancellable,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);
void            evd_socket_connect_async_addr            (EvdSocket           *self,
                                                          GSocketAddress      *address,
                                                          GCancellable        *cancellable,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);
GIOStream      *evd_socket_connect_finish                (EvdSocket     *self,
                                                          GAsyncResult  *result,
                                                          GError       **error);

G_END_DECLS

#endif /* __EVD_SOCKET_H__ */
