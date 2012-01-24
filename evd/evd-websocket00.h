/*
 * evd-websocket00.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2012, Igalia S.L.
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

#ifndef __EVD_WEBSOCKET00_H__
#define __EVD_WEBSOCKET00_H__

#include <evd-web-service.h>
#include <evd-http-connection.h>
#include <evd-http-request.h>
#include <evd-peer.h>

G_BEGIN_DECLS

void                evd_websocket00_handle_handshake_request    (EvdWebService       *web_service,
                                                                 EvdHttpConnection   *conn,
                                                                 EvdHttpRequest      *request,
                                                                 GAsyncReadyCallback  callback,
                                                                 gpointer             user_data);

gboolean            evd_websocket00_send                        (EvdHttpConnection  *conn,
                                                                 const gchar        *frame,
                                                                 gsize               frame_len,
                                                                 gboolean            is_binary,
                                                                 GError            **error);

gboolean            evd_websocket00_close                       (EvdHttpConnection  *conn,
                                                                 guint16             code,
                                                                 const gchar        *reason,
                                                                 GError            **error);

G_END_DECLS

#endif /* __EVD_WEBSOCKET00_H__ */
