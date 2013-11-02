/*
 * evd-websocket-protocol.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2012-2013, Igalia S.L.
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

#ifndef __EVD_WEBSOCKET_PROTOCOL_H__
#define __EVD_WEBSOCKET_PROTOCOL_H__

#include "evd-web-service.h"
#include "evd-http-connection.h"
#include "evd-http-request.h"
#include "evd-peer.h"

G_BEGIN_DECLS

/* websocket connection states */
typedef enum
{
  EVD_WEBSOCKET_STATE_NONE,
  EVD_WEBSOCKET_STATE_OPENING,
  EVD_WEBSOCKET_STATE_OPENED,
  EVD_WEBSOCKET_STATE_CLOSING,
  EVD_WEBSOCKET_STATE_CLOSED
} EvdWebsocketState;

typedef enum
{
  EVD_WEBSOCKET_CLOSE_NORMAL           = 1000,
  EVD_WEBSOCKET_CLOSE_GOING_AWAY       = 1001,
  EVD_WEBSOCKET_CLOSE_PROTOCOL_ERROR   = 1002,
  EVD_WEBSOCKET_CLOSE_UNSUPPORTED_DATA = 1003,
  EVD_WEBSOCKET_CLOSE_RESERVED         = 1004,
  EVD_WEBSOCKET_CLOSE_NO_STATUS        = 1005,
  EVD_WEBSOCKET_CLOSE_ABNORMAL         = 1006,
  EVD_WEBSOCKET_CLOSE_INVALID_DATA     = 1007,
  EVD_WEBSOCKET_CLOSE_POLICY_VIOLATION = 1008,
  EVD_WEBSOCKET_CLOSE_MESSAGE_TOO_BIG  = 1009,
  EVD_WEBSOCKET_CLOSE_MANDATORY_EXT    = 1010,
  EVD_WEBSOCKET_CLOSE_INTERNAL_ERROR   = 1011,
  EVD_WEBSOCKET_CLOSE_TLS_HANDSHAKE    = 1015
} EvdWebsocketClose;

typedef void (* EvdWebsocketFrameCb)         (EvdHttpConnection *conn,
                                              const gchar       *frame,
                                              gsize              frame_length,
                                              gboolean           is_binary,
                                              gpointer           user_data);
typedef void (* EvdWebsocketCloseCb)         (EvdHttpConnection *conn,
                                              gboolean           gracefully,
                                              gpointer           user_data);


gboolean          evd_websocket_protocol_handle_handshake_request  (EvdHttpConnection  *conn,
                                                                    EvdHttpRequest     *request,
                                                                    GError            **error);

EvdHttpRequest *  evd_websocket_protocol_create_handshake_request  (const gchar  *url,
                                                                    const gchar  *sub_protocol,
                                                                    const gchar  *origin,
                                                                    gchar       **key_base64);

gboolean          evd_websocket_protocol_handle_handshake_response (EvdHttpConnection   *conn,
                                                                    SoupHTTPVersion      http_version,
                                                                    guint                status_code,
                                                                    SoupMessageHeaders  *headers,
                                                                    const gchar         *handshake_key,
                                                                    GError             **error);

void              evd_websocket_protocol_bind                      (EvdHttpConnection   *conn,
                                                                    EvdWebsocketFrameCb  frame_cb,
                                                                    EvdWebsocketCloseCb  close_cb,
                                                                    gpointer             user_data,
                                                                    GDestroyNotify       user_data_destroy_notify);
void              evd_websocket_protocol_unbind                    (EvdHttpConnection *conn);

gboolean          evd_websocket_protocol_close                     (EvdHttpConnection  *conn,
                                                                    guint16             code,
                                                                    const gchar        *reason,
                                                                    GError            **error);

gboolean          evd_websocket_protocol_send                      (EvdHttpConnection  *conn,
                                                                    const gchar        *frame,
                                                                    gsize               frame_len,
                                                                    EvdMessageType      frame_type,
                                                                    GError            **error);

EvdWebsocketState evd_websocket_protocol_get_state                 (EvdHttpConnection *conn);

G_END_DECLS

#endif /* __EVD_WEBSOCKET_PROTOCOL_H__ */
