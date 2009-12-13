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

#include "evd-stream.h"

G_BEGIN_DECLS

/**
 * SECTION:evd-socket
 * @short_description: EventDance's base socket class.
 *
 * #EvdSocket sockets are Berkeley sockets with finer network control and scalability
 * under high-concurrency scenarios. It's the class that ultimately handles all network
 * access in EventDance framework. #EvdSocket is based on GSocket, and extends it by
 * providing more features together with an efficient mechanism to watch socket
 * condition changes, based on the <ulink type="http"
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

/**
 * EvdSocketReadHandler:
 * @socket: The #EvdSocket
 * @user_data: (allow-none): A #gpointer to user defined data to pass in callback.
 *
 * Prototype for a callback to be executed when 'read' event is received on a socket.
 */
typedef void (* EvdSocketReadHandler) (EvdSocket *socket,
                                       gpointer   user_data);

typedef void (* EvdSocketWriteHandler) (EvdSocket *socket,
                                        gpointer   user_data);

/* socket states */
typedef enum
{
  EVD_SOCKET_STATE_CLOSED,
  EVD_SOCKET_STATE_CONNECTING,
  EVD_SOCKET_STATE_CONNECTED,
  EVD_SOCKET_STATE_BINDING,
  EVD_SOCKET_STATE_BOUND,
  EVD_SOCKET_STATE_LISTENING
} EvdSocketState;

struct _EvdSocket
{
  EvdStream parent;

  /* private structure */
  EvdSocketPrivate *priv;
};

struct _EvdSocketClass
{
  EvdStreamClass parent_class;

  /* virtual methods */
  gboolean (* event_handler)  (EvdSocket *self, GIOCondition condition);
  void     (* invoke_on_read) (EvdSocket *self);
  gboolean (* cleanup)        (EvdSocket *self, GError **error);

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

/* event message to pass to sockets objects*/
struct _EvdSocketEvent
{
  GIOCondition  condition;
  EvdSocket    *socket;
};

/* error codes */
typedef enum
{
  EVD_SOCKET_ERROR_UNKNOWN,
  EVD_SOCKET_ERROR_NOT_CONNECTING,
  EVD_SOCKET_ERROR_NOT_CONNECTED,
  EVD_SOCKET_ERROR_NOT_BOUND,
  EVD_SOCKET_ERROR_CLOSE,
  EVD_SOCKET_ERROR_ACCEPT,
  EVD_SOCKET_ERROR_BUFFER_OVERFLOW,
  EVD_SOCKET_ERROR_CONNECT_TIMEOUT,
  EVD_SOCKET_ERROR_LAST
} EvdSocketError;

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

gboolean      evd_socket_bind             (EvdSocket       *self,
                                           GSocketAddress  *address,
                                           gboolean         allow_reuse,
                                           GError         **error);
gboolean      evd_socket_listen           (EvdSocket       *self,
                                           GSocketAddress  *address,
                                           GError         **error);
EvdSocket    *evd_socket_accept           (EvdSocket *socket, GError **error);

/**
 * evd_socket_connect_to:
 * @self: The #EvdSocket to connect.
 * @address: The #GSocketAddress to connect to.
 * @error: (out) (transfer full): The #GError to return, or NULL.
 *
 * Attempts to connect the socket to the specified address. If
 * <emphasis>connect-timeout</emphasis> property is greater than zero, the connect
 * opertation will abort after that time in miliseconds and a
 * <emphasis>connect-timeout</emphasis> signal will be triggered.
 *
 * Return value: TRUE on success or FALSE on error.
 *
 * Rename to: connect_toto
 */
gboolean      evd_socket_connect_to       (EvdSocket       *self,
                                           GSocketAddress  *address,
                                           GError         **error);
gboolean      evd_socket_cancel_connect   (EvdSocket *self, GError **error);

void          evd_socket_set_read_handler (EvdSocket            *self,
                                           EvdSocketReadHandler  handler,
                                           gpointer              user_data);

void          evd_socket_set_write_handler (EvdSocket             *self,
                                            EvdSocketWriteHandler  handler,
                                            gpointer               user_data);

/**
 * evd_socket_read_len:
 * @self: The #EvdSocket to read from.
 * @buffer: (out) (transfer full): The buffer to store the data.
 * @size: (in): Maximum number of bytes to read.
 * @error: (out) (transfer full): The #GError to return, or NULL.
 *
 * Reads up to @size bytes of data from the socket. The data read will be copied
 * into @buffer.
 *
 * Return value: The actual number of bytes read.
 */
gssize        evd_socket_read_len         (EvdSocket *self,
                                           gchar     *buffer,
                                           gsize      size,
                                           GError   **error);

/**
 * evd_socket_read:
 * @self: The #EvdSocket to read from.
 * @size: (inout): Maximum number of bytes to read.
 * @error: (out) (transfer full): The #GError to return, or NULL.
 *
 * Reads up to @size bytes of data from the socket. This method assumes data will
 * not contain null characters. To read binary data, #evd_socket_read_buffer should be
 * used instead.
 *
 * Return value: (transfer full): A null-terminated string containing the data read, or NULL on error.
 */
gchar         *evd_socket_read            (EvdSocket *self,
                                           gsize     *size,
                                           GError   **error);

/**
 * evd_socket_write_len:
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
gssize        evd_socket_write_len        (EvdSocket    *self,
                                           const gchar  *buffer,
                                           gsize         size,
                                           GError      **error);

/**
 * evd_socket_write:
 * @self: The #EvdSocket to write to.
 * @buffer: (transfer none): Buffer holding the data to be written. Should not contain nulls.
 * @error: (out) (transfer full): The #GError to return, or NULL.
 *
 * Writes up to @size bytes of data to the socket. @buffer should be a null-terminated string.
 * To send binary data, #evd_socket_write_buffer should be used instead. As @buffer cannot
 * contain null characters, the number of bytes to write is obtained from its length.
 *
 * The #auto-write property affects this method exactly as in #evd_socket_write_buffer.
 *
 * Return value: The actual number of bytes written.
 */
gssize        evd_socket_write            (EvdSocket    *self,
                                           const gchar  *buffer,
                                           GError      **error);

/**
 * evd_socket_unread_len:
 * @self: The #EvdSocket to unread data to.
 * @buffer: (transfer none): Buffer holding the data to be unread. Can contain nulls.
 * @size: (inout): Number of bytes to unread.
 *
 * Stores @size bytes from @buffer in the local read buffer of the socket. Next calls
 * to read will first get data from the local buffer, before performing the actual read
 * operation. This is useful when one needs to do some action with a data just read, but doesn't
 * want to remove the data from the socket, and avoid interferance with other reading operation
 * that is in course.
 * Normally, it would be used to write back data that was previously read, to made it available
 * in further calls to read. But in practice any data can be unread.
 *
 * This feature was implemented basically to provide type-of-stream detection on a socket
 * (e.g. a service selector).
 *
 * Return value: The actual number of bytes unread.
 */

gssize        evd_socket_unread_len    (EvdSocket    *self,
                                        const gchar  *buffer,
                                        gsize         size,
                                        GError      **error);

/**
 * evd_socket_unread:
 * @self: The #EvdSocket to unread data to.
 * @buffer: (transfer none): Buffer holding the data to be unread. Cannot contain nulls.
 *
 * Works exactly like #evd_socket_unread_buffer but buffer is a null-terminated string,
 * thus @size is calculated internally.
 *
 * Return value: The actual number of bytes unread.
 */
gssize        evd_socket_unread           (EvdSocket    *self,
                                           const gchar  *buffer,
                                           GError      **error);

gboolean      evd_socket_has_write_data_pending (EvdSocket *self);

gboolean      evd_socket_can_read               (EvdSocket *self);
gboolean      evd_socket_can_write              (EvdSocket *self);

GSocketAddress *evd_socket_get_remote_address   (EvdSocket  *self,
                                                 GError    **error);
GSocketAddress *evd_socket_get_local_address    (EvdSocket  *self,
                                                 GError    **error);

gsize           evd_socket_get_max_readable     (EvdSocket *self);
gsize           evd_socket_get_max_writable     (EvdSocket *self);

G_END_DECLS

#endif /* __EVD_SOCKET_H__ */
