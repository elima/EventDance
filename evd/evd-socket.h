/*
 * evd-socket.h
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

#ifndef __EVD_SOCKET_H__
#define __EVD_SOCKET_H__

#include <gio/gio.h>

#include "evd-socket-base.h"
#include "evd-tls-session.h"

G_BEGIN_DECLS

typedef struct _EvdSocket EvdSocket;
typedef struct _EvdSocketClass EvdSocketClass;
typedef struct _EvdSocketPrivate EvdSocketPrivate;
typedef struct _EvdSocketEvent EvdSocketEvent;

/* socket states */
typedef enum
{
  EVD_SOCKET_STATE_CLOSED,
  EVD_SOCKET_STATE_CONNECTING,
  EVD_SOCKET_STATE_CONNECTED,
  EVD_SOCKET_STATE_RESOLVING,
  EVD_SOCKET_STATE_BOUND,
  EVD_SOCKET_STATE_LISTENING,
  EVD_SOCKET_STATE_TLS_HANDSHAKING
} EvdSocketState;

struct _EvdSocket
{
  EvdSocketBase parent;

  /* private structure */
  EvdSocketPrivate *priv;
};

struct _EvdSocketClass
{
  EvdSocketBaseClass parent_class;

  /* virtual methods */
  gboolean (* handle_condition) (EvdSocket *self, GIOCondition condition);
  void     (* invoke_on_read)   (EvdSocket *self);
  void     (* invoke_on_write)  (EvdSocket *self);
  gboolean (* cleanup)          (EvdSocket *self, GError **error);

  /* signal prototypes */
  void (* error)          (EvdSocket *self,
                           GError    *error);
  void (* state_changed)  (EvdSocket      *self,
                           EvdSocketState  new_state,
                           EvdSocketState  old_state);
  void (* close)          (EvdSocket *self);
  void (* new_connection) (EvdSocket *self,
                           EvdSocket *socket);
};

#define EVD_TYPE_SOCKET           (evd_socket_get_type ())
#define EVD_SOCKET(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_SOCKET, EvdSocket))
#define EVD_SOCKET_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_SOCKET, EvdSocketClass))
#define EVD_IS_SOCKET(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_SOCKET))
#define EVD_IS_SOCKET_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_SOCKET))
#define EVD_SOCKET_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_SOCKET, EvdSocketClass))


GType           evd_socket_get_type           (void) G_GNUC_CONST;

EvdSocket      *evd_socket_new                (void);

GSocket        *evd_socket_get_socket         (EvdSocket *self);
GMainContext   *evd_socket_get_context        (EvdSocket *self);
GSocketFamily   evd_socket_get_family         (EvdSocket *self);
EvdSocketState  evd_socket_get_status         (EvdSocket *self);

gint            evd_socket_get_priority       (EvdSocket *self);
void            evd_socket_set_priority       (EvdSocket *self, gint priority);

gboolean        evd_socket_close              (EvdSocket *self, GError **error);

gboolean        evd_socket_bind_addr          (EvdSocket       *self,
                                               GSocketAddress  *address,
                                               gboolean         allow_reuse,
                                               GError         **error);
gboolean        evd_socket_bind               (EvdSocket    *self,
                                               const gchar  *address,
                                               gboolean      allow_reuse,
                                               GError      **error);

gboolean        evd_socket_listen_addr        (EvdSocket       *self,
                                               GSocketAddress  *address,
                                               GError         **error);
gboolean        evd_socket_listen             (EvdSocket    *self,
                                               const gchar  *address,
                                               GError      **error);

gboolean        evd_socket_connect_addr       (EvdSocket       *self,
                                               GSocketAddress  *address,
                                               GError         **error);
gboolean        evd_socket_connect_to         (EvdSocket    *self,
                                               const gchar  *address,
                                               GError      **error);

gssize          evd_socket_read               (EvdSocket *self,
                                               gchar     *buffer,
                                               gsize      size,
                                               GError   **error);

gssize          evd_socket_write              (EvdSocket    *self,
                                               const gchar  *buffer,
                                               gsize         size,
                                               GError      **error);

gssize          evd_socket_unread             (EvdSocket    *self,
                                               const gchar  *buffer,
                                               gsize         size,
                                               GError      **error);

gboolean        evd_socket_can_read           (EvdSocket *self);
gboolean        evd_socket_can_write          (EvdSocket *self);

GSocketAddress *evd_socket_get_remote_address (EvdSocket  *self,
                                               GError    **error);
GSocketAddress *evd_socket_get_local_address  (EvdSocket  *self,
                                               GError    **error);

gsize           evd_socket_get_max_readable   (EvdSocket *self);
gsize           evd_socket_get_max_writable   (EvdSocket *self);

gboolean        evd_socket_starttls           (EvdSocket   *self,
                                               EvdTlsMode   mode,
                                               GError     **error);

gboolean        evd_socket_shutdown           (EvdSocket  *self,
                                               gboolean    shutdown_read,
                                               gboolean    shutdown_write,
                                               GError    **error);

EvdTlsSession  *evd_socket_get_tls_session    (EvdSocket *self);
gboolean        evd_socket_get_tls_active     (EvdSocket *self);

GInputStream   *evd_socket_get_input_stream   (EvdSocket *self);
GOutputStream  *evd_socket_get_output_stream  (EvdSocket *self);

G_END_DECLS

#endif /* __EVD_SOCKET_H__ */
