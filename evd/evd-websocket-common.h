/*
 * evd-websocket-common.h
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

#ifndef __EVD_WEBSOCKET_COMMON_H__
#define __EVD_WEBSOCKET_COMMON_H__

#include <glib.h>
#include <gio/gio.h>

#include <evd-web-service.h>
#include <evd-http-connection.h>
#include <evd-http-request.h>
#include <evd-peer.h>

G_BEGIN_DECLS

#define EVD_WEBSOCKET_DATA_KEY "org.eventdance.lib.Websocket.CONN_DATA"

#define EVD_WEBSOCKET_MAX_PAYLOAD_SIZE 0x40000000

typedef void     (* EvdWebsocketFrameCb)         (EvdHttpConnection *conn,
                                                  const gchar       *frame,
                                                  gsize              frame_length,
                                                  gboolean           is_binary,
                                                  gpointer           user_data);
typedef void     (* EvdWebsocketCloseCb)         (EvdHttpConnection *conn,
                                                  gboolean           gracefully,
                                                  gpointer           user_data);

typedef struct _EvdWebsocketData EvdWebsocketData;

typedef gboolean (* EvdWebsocketProcessDataFunc)    (EvdWebsocketData *data);
typedef gboolean (* EvdWebsocketSendCloseFrameFunc) (EvdWebsocketData  *data,
                                                     guint16            code,
                                                     const gchar       *reason,
                                                     GError           **error);
typedef gboolean (* EvdWebsocketSendDataFrameFunc)  (EvdWebsocketData  *data,
                                                     const gchar       *frame,
                                                     gsize              frame_len,
                                                     EvdMessageType     frame_type,
                                                     GError           **error);

typedef enum
{
  /* common states */
  EVD_WEBSOCKET_STATE_IDLE,
  EVD_WEBSOCKET_STATE_CLOSING,
  EVD_WEBSOCKET_STATE_CLOSED,
  EVD_WEBSOCKET_STATE_READING_PAYLOAD_LEN,

  /* version 00 specific */
  EVD_WEBSOCKET_STATE_READING_BINARY_FRAME,
  EVD_WEBSOCKET_STATE_READING_TEXT_FRAME,

  /* version 08 specific */
  EVD_WEBSOCKET_STATE_READING_HEADER,
  EVD_WEBSOCKET_STATE_READING_MASKING_KEY,
  EVD_WEBSOCKET_STATE_READING_PAYLOAD,

  /* padding for future expansion */
  EVD_WEBSOCKET_STATE_PADDING0,
  EVD_WEBSOCKET_STATE_PADDING1,
  EVD_WEBSOCKET_STATE_PADDING2,
  EVD_WEBSOCKET_STATE_PADDING3,
  EVD_WEBSOCKET_STATE_PADDING4,
  EVD_WEBSOCKET_STATE_PADDING5,
  EVD_WEBSOCKET_STATE_PADDING6,
  EVD_WEBSOCKET_STATE_PADDING7
} EvdWebsocketStates;

struct _EvdWebsocketData
{
  gint ref_count;

  guint version;
  gboolean server;

  EvdHttpConnection *conn;

  EvdWebsocketFrameCb frame_cb;
  EvdWebsocketCloseCb close_cb;

  EvdWebsocketProcessDataFunc process_data_func;
  EvdWebsocketSendCloseFrameFunc send_close_frame_func;
  EvdWebsocketSendDataFrameFunc send_data_frame_func;

  gpointer user_data;
  GDestroyNotify user_data_destroy_notify;

  guint8 state;

  GString *buf;
  gsize buf_len;
  gsize offset;

  guint8 opcode;
  gsize payload_len;
  gchar *frame_data;
  gsize frame_len;

  gboolean close_frame_sent;

  gboolean fin;
  gboolean masked;
  guint8 masking_key[4];
  gchar *extensions_data;
  gsize extension_len;
};

void                evd_websocket_common_handle_handshake_request        (EvdWebService       *web_service,
                                                                          EvdHttpConnection   *conn,
                                                                          EvdHttpRequest      *request,
                                                                          GAsyncReadyCallback  callback,
                                                                          gpointer             user_data);
gboolean            evd_websocket_common_handle_handshake_request_finish (GAsyncResult  *result,
                                                                          GError       **error);

void                evd_websocket_common_setup_connection                (EvdHttpConnection              *conn,
                                                                          guint8                          version,
                                                                          gboolean                        is_server,
                                                                          EvdWebsocketProcessDataFunc     process_data_func,
                                                                          EvdWebsocketSendCloseFrameFunc  send_close_frame_func,
                                                                          EvdWebsocketSendDataFrameFunc   send_data_frame_func);

void                evd_websocket_common_bind                            (EvdHttpConnection   *conn,
                                                                          EvdWebsocketFrameCb  frame_cb,
                                                                          EvdWebsocketCloseCb  close_cb,
                                                                          gpointer             user_data,
                                                                          GDestroyNotify       user_data_destroy_notify);
gboolean            evd_websocket_common_is_bound                        (EvdHttpConnection *conn);

gboolean            evd_websocket_common_close                           (EvdHttpConnection  *conn,
                                                                          guint16             code,
                                                                          const gchar        *reason,
                                                                          GError            **error);

gboolean            evd_websocket_common_send                            (EvdHttpConnection  *conn,
                                                                          const gchar        *frame,
                                                                          gsize               frame_len,
                                                                          EvdMessageType      frame_type,
                                                                          GError            **error);

G_END_DECLS

#endif /* __EVD_WEBSOCKET_COMMON_H__ */
