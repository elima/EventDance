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

/**
 * SECTION:evd-socket
 * @short_description: EventDance's base socket class.
 *
 * #EvdSocket sockets are Berkeley sockets with finer network control and scalability
 * under high-concurrency scenarios. It's the class that ultimately handles all network
 * access in EventDance framework. #EvdSocket is based on GSocket, and extends it by
 * providing more features like SSL/TLS support, bandwith and latency control, etc; together
 * with an efficient mechanism to watch socket condition changes, based on the <ulink type="http"
 * url="http://www.kernel.org/doc/man-pages/online/pages/man4/epoll.4.html">
 * epoll event notification facility</ulink> available on Linux, in edge-triggered mode.
 *
 * #EvdSocket is designed to work always in non-blocking mode. As everything in
 * EventDance framework, all network IO logic should be strictly asynchronous.
 */
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
  gboolean (* cleanup)          (EvdSocket *self, GError **error);

  /* signal prototypes */
  void (* error)           (EvdSocket *self,
                            GError    *error);
  void (* state_changed)   (EvdSocket      *self,
                            EvdSocketState  new_state,
                            EvdSocketState  old_state);
  void (* close)           (EvdSocket *self);
  void (* new_connection)  (EvdSocket *self,
                            EvdSocket *socket);
};

#define EVD_TYPE_SOCKET           (evd_socket_get_type ())
#define EVD_SOCKET(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_SOCKET, EvdSocket))
#define EVD_SOCKET_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_SOCKET, EvdSocketClass))
#define EVD_IS_SOCKET(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_SOCKET))
#define EVD_IS_SOCKET_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_SOCKET))
#define EVD_SOCKET_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_SOCKET, EvdSocketClass))


GType evd_socket_get_type (void) G_GNUC_CONST;

EvdSocket    *evd_socket_new              (void);

GSocket      *evd_socket_get_socket       (EvdSocket *self);
GMainContext *evd_socket_get_context      (EvdSocket *self);
GSocketFamily evd_socket_get_family       (EvdSocket *self);
EvdSocketState evd_socket_get_status      (EvdSocket *self);

gint          evd_socket_get_priority     (EvdSocket *self);
void          evd_socket_set_priority     (EvdSocket *self, gint priority);

gboolean      evd_socket_close            (EvdSocket *self, GError **error);

gboolean      evd_socket_bind_addr        (EvdSocket       *self,
                                           GSocketAddress  *address,
                                           gboolean         allow_reuse,
                                           GError         **error);
gboolean      evd_socket_bind             (EvdSocket    *self,
                                           const gchar  *address,
                                           gboolean      allow_reuse,
                                           GError      **error);
gboolean      evd_socket_listen_addr      (EvdSocket       *self,
                                           GSocketAddress  *address,
                                           GError         **error);

/**
 * evd_socket_listen:
 * @self: The #EvdSocket to listen on.
 * @address: (allow-none): A string representing the socket address to listen on, or NULL if
 *                         the socket was previously bound to an address using #evd_socket_bind
 *                         or #evd_socket_bind_addr. Only works for connection-oriented sockets.
 * @error: (out) (transfer full): The #GError to return, or NULL.
 *
 * Return value: TRUE on success or FALSE on error.
 *
 */
gboolean      evd_socket_listen           (EvdSocket    *self,
                                           const gchar  *address,
                                           GError      **error);

/**
 * evd_socket_connect_addr:
 * @self: The #EvdSocket to connect.
 * @address: The #GSocketAddress to connect to.
 * @error: (out) (transfer full): The #GError to return, or NULL.
 *
 * Attempts to connect the socket to the specified address. If
 * <emphasis>connect-timeout</emphasis> property is greater than zero, the connect
 * opertation will abort after that time in miliseconds and a
 * <emphasis>connect-timeout</emphasis> error will be signaled.
 *
 * Return value: TRUE on success or FALSE on error.
 */
gboolean      evd_socket_connect_addr     (EvdSocket       *self,
                                           GSocketAddress  *address,
                                           GError         **error);

/**
 * evd_socket_connect_to:
 * @self: The #EvdSocket to connect.
 * @address: A string representing the socket address to connect to.
 * @error: (out) (transfer full): The #GError to return, or NULL.
 *
 * Similar to #evd_socket_connect_addr, but address is a string that will
 * be resolved to a #GSocketAddress internally.
 *
 * For unix addresses, a valid filename should be provided since abstract unix
 * addresses are not supported. For IP addresses, a string in the format "host:port"
 * is expected. <emphasis>host</emphasis> can be an IP address version 4 or 6 (if
 * supported by the OS), or any domain name.
 *
 * If <emphasis>connect-timeout</emphasis> property is greater than zero, the connect
 * opertation will abort after that time in miliseconds and a
 * <emphasis>connect-timeout</emphasis> error will be signaled.
 *
 * Return value: TRUE on success or FALSE on error.
 */
gboolean      evd_socket_connect_to       (EvdSocket    *self,
                                           const gchar  *address,
                                           GError      **error);

/**
 * evd_socket_read:
 * @self: The #EvdSocket to read from.
 * @buffer: (out) (transfer full): The buffer to store the data.
 * @size: Maximum number of bytes to read.
 * @error: The #GError to return, or NULL.
 *
 * Reads up to @size bytes of data from the socket input stream. The data read will be copied
 * into @buffer.
 *
 * Return value: The actual number of bytes read.
 */
gssize        evd_socket_read             (EvdSocket *self,
                                           gchar     *buffer,
                                           gsize      size,
                                           GError   **error);

/**
 * evd_socket_write:
 * @self: The #EvdSocket to write to.
 * @buffer: (transfer none): Buffer holding the data to be written. Can contain nulls.
 * @size: (in): Maximum number of bytes to write. @buffer should be at least @size long.
 * @error: (out) (transfer full): The #GError to return, or NULL.
 *
 * Writes up to @size bytes of data to the socket.
 *
 * If #auto-write property is TRUE, this method will always respond as it was able to send
 * all data requested, and will buffer and handle the actual writting internally.
 *
 * Return value: The actual number of bytes written.
 */
gssize        evd_socket_write            (EvdSocket    *self,
                                           const gchar  *buffer,
                                           gsize         size,
                                           GError      **error);

/**
 * evd_socket_unread:
 * @self: The #EvdSocket to unread data to.
 * @buffer: (transfer none): Buffer holding the data to be unread. Can contain nulls.
 * @size: Number of bytes to unread.
 * @error: (out) (transfer full): A pointer to a #GError to return, or NULL.
 *
 * Stores @size bytes from @buffer in the local read buffer of the socket. Next calls
 * to read will first get data from the local buffer, before performing the actual read
 * operation. This is useful when one needs to do some action with a data just read, but doesn't
 * want to remove the data from the input stream of the socket.
 *
 * Normally, it would be used to write back data that was previously read, to made it available
 * in further calls to read. But in practice any data can be unread.
 *
 * This feature was implemented basically to provide type-of-stream detection on a socket
 * (e.g. a service selector).
 *
 * Return value: The actual number of bytes unread.
 */
gssize        evd_socket_unread           (EvdSocket    *self,
                                           const gchar  *buffer,
                                           gsize         size,
                                           GError      **error);

gboolean      evd_socket_can_read               (EvdSocket *self);
gboolean      evd_socket_can_write              (EvdSocket *self);

GSocketAddress *evd_socket_get_remote_address   (EvdSocket  *self,
                                                 GError    **error);
GSocketAddress *evd_socket_get_local_address    (EvdSocket  *self,
                                                 GError    **error);

gsize           evd_socket_get_max_readable     (EvdSocket *self);
gsize           evd_socket_get_max_writable     (EvdSocket *self);

gboolean        evd_socket_starttls             (EvdSocket   *self,
                                                 EvdTlsMode   mode,
                                                 GError     **error);

gboolean        evd_socket_shutdown             (EvdSocket  *self,
                                                 gboolean    shutdown_read,
                                                 gboolean    shutdown_write,
                                                 GError    **error);

/**
 * evd_socket_get_tls_session:
 *
 * Returns: (transfer none): The #EvdTlsSession object
 **/
EvdTlsSession  *evd_socket_get_tls_session      (EvdSocket *self);

gboolean        evd_socket_get_tls_active       (EvdSocket *self);

/**
 * evd_socket_get_input_stream:
 *
 * Returns: (transfer none):
 **/
GInputStream   *evd_socket_get_input_stream     (EvdSocket *self);

/**
 * evd_socket_get_output_stream:
 *
 * Returns: (transfer none):
 **/
GOutputStream  *evd_socket_get_output_stream    (EvdSocket *self);

G_END_DECLS

#endif /* __EVD_SOCKET_H__ */
