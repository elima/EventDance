/*
 * evd-websocket-server.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011, Igalia S.L.
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

#include <string.h>
#include <stdio.h>
#include <libsoup/soup-headers.h>

#include "evd-websocket-server.h"

#include "evd-transport.h"
#include "evd-http-connection.h"

#define CONN_DATA_KEY "org.eventdance.lib.WebsocketServer.CONN_DATA"
#define PEER_DATA_KEY "org.eventdance.lib.WebsocketServer.PEER_DATA"

#define BLOCK_SIZE 0x08FF

typedef struct
{
  EvdWebsocketServer *self;
  EvdHttpRequest *request;
  EvdPeer *peer;
  guint32 value1;
  guint32 value2;
  gchar payload[8];
} HandshakeData;

typedef enum
{
  STATUS_IDLE,
  STATUS_READING_RAW,
  STATUS_READING_TEXT,
  STATUS_CLOSING
} ConnStatus;

typedef struct
{
  EvdTransport *transport;
  EvdPeer *peer;
  EvdConnection *conn;

  GString *buf;
  gssize size;

  ConnStatus status;
  goffset frame_start;
  goffset offset;
  gsize frame_size;
} ConnData;

static void     evd_websocket_server_class_init           (EvdWebsocketServerClass *class);
static void     evd_websocket_server_init                 (EvdWebsocketServer *self);

static void     evd_websocket_server_transport_iface_init (EvdTransportInterface *iface);

static void     evd_websocket_server_request_handler      (EvdWebService     *web_service,
                                                           EvdHttpConnection *conn,
                                                           EvdHttpRequest    *request);

static gboolean evd_websocket_server_remove               (EvdIoStreamGroup *io_stream_group,
                                                           GIOStream        *io_stream);

static gboolean evd_websocket_server_send                 (EvdTransport *transport,
                                                           EvdPeer       *peer,
                                                           const gchar   *buffer,
                                                           gsize          size,
                                                           GError       **error);

static gboolean evd_websocket_server_peer_is_connected    (EvdTransport *transport,
                                                           EvdPeer      *peer);

static void     evd_websocket_server_peer_closed          (EvdTransport *transport,
                                                           EvdPeer      *peer,
                                                           gboolean      gracefully);

static void     connection_read                           (GInputStream *stream,
                                                           ConnData     *data);

static gboolean process_received_data                     (ConnData *data);

G_DEFINE_TYPE_WITH_CODE (EvdWebsocketServer, evd_websocket_server, EVD_TYPE_WEB_SERVICE,
                         G_IMPLEMENT_INTERFACE (EVD_TYPE_TRANSPORT,
                                                evd_websocket_server_transport_iface_init));

static void
evd_websocket_server_class_init (EvdWebsocketServerClass *class)
{
  EvdIoStreamGroupClass *io_stream_group_class =
    EVD_IO_STREAM_GROUP_CLASS (class);
  EvdWebServiceClass *web_service_class = EVD_WEB_SERVICE_CLASS (class);

  io_stream_group_class->remove = evd_websocket_server_remove;

  web_service_class->request_handler = evd_websocket_server_request_handler;
}

static void
evd_websocket_server_transport_iface_init (EvdTransportInterface *iface)
{
  iface->send = evd_websocket_server_send;
  iface->peer_is_connected = evd_websocket_server_peer_is_connected;
  iface->peer_closed = evd_websocket_server_peer_closed;
}

static void
evd_websocket_server_init (EvdWebsocketServer *self)
{
  evd_service_set_io_stream_type (EVD_SERVICE (self), EVD_TYPE_HTTP_CONNECTION);
}

static void
notify_frame (EvdTransport *transport,
              EvdPeer      *peer,
              const gchar  *buf,
              gsize         size)
{
  EvdTransportInterface *iface;

  iface = EVD_TRANSPORT_GET_INTERFACE (transport);

  iface->receive (transport, peer, buf, size);
}

static gboolean
read_frame_len76 (ConnData *data)
{
  guint8 b;
  guint8 b_v;

  while (data->offset < data->size)
    {
      b = data->buf->str[data->offset];

      data->offset++;

      b_v = b & 0x7F;

      data->frame_size = data->frame_size * 128 + b_v;

      if ((b_v & 0x80) == 0)
        return TRUE;
    }

  return FALSE;
}

static void
read_raw_msg (ConnData *data)
{
  /* @TODO */
}

static void
read_text_msg (ConnData *data)
{
  while (data->offset < data->size)
    {
      if (data->buf->str[data->offset] == (gchar) 0xFF)
        {
          notify_frame (data->transport,
                        data->peer,
                        data->buf->str + data->frame_start,
                        data->offset - data->frame_start);

          data->status = STATUS_IDLE;
          data->offset++;

          return;
        }
      else
        {
          data->offset++;
        }
    }
}

static gboolean
process_received_data (ConnData *data)
{
  while (data->offset < data->size)
    {
      if (data->status == STATUS_IDLE)
        {
          gchar frame_type;

          frame_type = data->buf->str[data->offset];
          data->offset++;

          if (frame_type == (gchar) 0xFF)
            {
              /* close handshake */
              data->status = STATUS_CLOSING;
            }
          else if ((frame_type & 0x80) == 0x80)
            {
              /* raw data */
              data->status = STATUS_READING_TEXT;
            }
          else
            {
              /* text data */
              data->frame_start = data->offset;
              data->status = STATUS_READING_TEXT;
            }
        }
      else if (data->status == STATUS_READING_TEXT)
        {
          read_text_msg (data);
        }
      else if (data->status == STATUS_READING_RAW)
        {
          read_raw_msg (data);
        }
      else if (data->status == STATUS_CLOSING &&
               (read_frame_len76 (data)))
        {
          if (data->frame_size == 0)
            {
              evd_transport_close_peer (data->transport,
                                        data->peer,
                                        TRUE,
                                        NULL);
            }
          else
            {
              /* invalid close handshake, abort connection */
              g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);
            }

          return FALSE;
        }
    }

  if (data->status == STATUS_IDLE)
    {
      data->size = 0;
      data->offset = 0;
      if (data->buf->len > BLOCK_SIZE)
        g_string_set_size (data->buf, BLOCK_SIZE);
    }

  return TRUE;
}

static void
on_connection_read (GObject      *obj,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  ConnData *data = user_data;
  EvdConnection *conn;
  GError *error = NULL;
  gssize size;

  conn = data->conn;

  evd_peer_touch (data->peer);

  size = g_input_stream_read_finish (G_INPUT_STREAM (obj),
                                     res,
                                     &error);
  if (size < 0)
    {
      g_debug ("Error reading from WebSocket: %s", error->message);
      g_error_free (error);
    }
  else if (size > 0)
    {
      data->size += size;

      if (process_received_data (data))
        connection_read (G_INPUT_STREAM (obj), data);
    }

  g_object_unref (conn);
}

static void
connection_read (GInputStream *stream, ConnData *data)
{
  if (data->offset + BLOCK_SIZE >= data->buf->len)
    g_string_set_size (data->buf, data->offset + BLOCK_SIZE);

  g_object_ref (data->conn);
  g_input_stream_read_async (stream,
                             data->buf->str + data->offset,
                             BLOCK_SIZE,
                             G_PRIORITY_DEFAULT,
                             NULL,
                             on_connection_read,
                             data);
}

static void
handshake76_stage2 (EvdHttpConnection *conn, HandshakeData *data)
{
  guchar st[16];
  guchar md5[16];
  gsize md5_len = 16;
  GChecksum *chksum;
  SoupMessageHeaders *headers;
  SoupMessageHeaders *req_headers;
  GError *error = NULL;
  const gchar *origin;
  SoupURI *uri;
  SoupURI *ws_uri;
  gchar *ws_uri_str;

  /* compute MD5 checksum */
  data->value1 = g_htonl (data->value1);
  data->value2 = g_htonl (data->value2);

  memmove (st, &data->value1, 4);
  memmove (st + 4, &data->value2, 4);
  memmove (st + 8, data->payload, 8);

  chksum = g_checksum_new (G_CHECKSUM_MD5);
  g_checksum_update (chksum, st, 16);
  g_checksum_get_digest (chksum, md5, &md5_len);
  g_checksum_free (chksum);

  /* build HTTP response */
  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

  soup_message_headers_replace (headers, "Connection", "Upgrade");
  soup_message_headers_replace (headers, "Upgrade", "WebSocket");

  /* @TODO: handle allowed origins externally */
  req_headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (data->request));
  origin = soup_message_headers_get_one (req_headers, "Origin");
  soup_message_headers_replace (headers, "Sec-WebSocket-Origin", origin);

  uri = evd_http_request_get_uri (data->request);
  ws_uri = soup_uri_copy (uri);
  if (evd_connection_get_tls_active (EVD_CONNECTION (conn)))
    soup_uri_set_scheme (ws_uri, "wss");
  else
    soup_uri_set_scheme (ws_uri, "ws");
  soup_uri_set_port (ws_uri, uri->port);
  ws_uri_str = soup_uri_to_string (ws_uri, FALSE);
  soup_message_headers_replace (headers, "Sec-WebSocket-Location",
                                ws_uri_str);
  g_free (ws_uri_str);
  soup_uri_free (ws_uri);

  /* send handshake response */
  if (! evd_http_connection_write_response_headers (conn,
                                                    SOUP_HTTP_1_1,
                                                    SOUP_STATUS_SWITCHING_PROTOCOLS,
                                                    "WebSocket Upgrade",
                                                    headers,
                                                    &error) ||
      ! evd_http_connection_write_content (conn,
                                           (gchar *) md5,
                                           16,
                                           &error))
    {
      /* @TODO: do proper logging */
      g_debug ("Error responding WebSocket handshake: %s", error->message);
      g_error_free (error);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }
  else
    {
      ConnData *conn_data;
      GInputStream *stream;

      /* handshake completed */

      conn_data = g_slice_new0 (ConnData);

      conn_data->transport = EVD_TRANSPORT (data->self);
      g_object_ref (conn_data->transport);

      conn_data->peer = data->peer;
      g_object_set_data (G_OBJECT (data->peer), PEER_DATA_KEY, conn);
      g_object_ref (conn_data->peer);

      conn_data->conn = EVD_CONNECTION (conn);
      g_object_ref (conn_data->conn);

      conn_data->buf = g_string_new_len ("", BLOCK_SIZE);
      conn_data->status = STATUS_IDLE;
      conn_data->size = 0;
      conn_data->offset = 0;

      g_object_set_data (G_OBJECT (conn), CONN_DATA_KEY, conn_data);

      stream = g_io_stream_get_input_stream (G_IO_STREAM (conn));
      connection_read (stream, conn_data);
    }

  soup_message_headers_free (headers);
}

static void
on_handshake_payload_read (GObject      *obj,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);
  HandshakeData *data = user_data;
  gssize size;
  GError *error = NULL;

  size = evd_http_connection_read_content_finish (conn, res, NULL, &error);
  if (size < 0)
    {
      /* @TODO: do proper logging */
      g_debug ("Error reading WebSocket handshake payload: %s", error->message);
      g_error_free (error);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }
  else
    {
      handshake76_stage2 (conn, data);
    }

  g_object_unref (data->self);
  g_object_unref (data->request);
  g_slice_free (HandshakeData, data);
}

static guint32
get_value_from_key (const gchar *key)
{
  gchar digits[32] = { 0 };
  guint count = 0;
  gint i;
  guint32 result = 0;
  guint8 spaces = 0;

  for (i = 0; i < strlen (key); i++)
    if (key[i] >= '0' && key[i] <= '9')
      {
        digits[count] = key[i];
        count ++;
      }
    else if (key[i] == ' ')
      {
        spaces++;
      }

  if (count > 0 && spaces > 0)
    {
      guint64 num;

      num = g_ascii_strtoull (digits, NULL, 10);
      result = num / spaces;
    }

  return result;
}

static gboolean
handshake76_stage1 (EvdWebsocketServer *self,
                    EvdHttpConnection  *conn,
                    EvdHttpRequest     *request,
                    EvdPeer            *peer)
{
  SoupMessageHeaders *headers;
  const gchar *key;
  guint32 value1;
  guint32 value2;
  HandshakeData *data;

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  if (g_strcmp0 (soup_message_headers_get_one (headers, "Upgrade"),
                 "WebSocket") != 0 ||
      g_strcmp0 (soup_message_headers_get_one (headers, "Connection"),
                 "Upgrade") != 0)
    {
      return FALSE;
    }

  key = soup_message_headers_get_one (headers, "Sec-WebSocket-Key1");
  if (key == NULL)
    return FALSE;

  value1 = get_value_from_key (key);
  if (value1 == 0)
    return FALSE;

  key = soup_message_headers_get_one (headers, "Sec-WebSocket-Key2");
  if (key == NULL)
    return FALSE;

  value2 = get_value_from_key (key);
  if (value2 == 0)
    return FALSE;

  data = g_slice_new0 (HandshakeData);
  data->self = self;
  data->request = request;
  data->peer = peer;
  data->value1 = value1;
  data->value2 = value2;

  g_object_ref (self);
  g_object_ref (request);
  evd_http_connection_read_content (conn,
                                    data->payload,
                                    8,
                                    NULL,
                                    on_handshake_payload_read,
                                    data);

  return TRUE;
}

static void
evd_websocket_server_request_handler (EvdWebService     *web_service,
                                      EvdHttpConnection *conn,
                                      EvdHttpRequest    *request)
{
  EvdWebsocketServer *self = EVD_WEBSOCKET_SERVER (web_service);
  EvdPeer *peer = NULL;
  SoupURI *uri;

  uri = evd_http_request_get_uri (request);

  peer = evd_transport_lookup_peer (EVD_TRANSPORT (self), uri->query);
  if (peer == NULL)
    {
      evd_http_connection_respond_simple (conn, SOUP_STATUS_NOT_FOUND, NULL, 0);
      return;
    }

  evd_peer_touch (peer);

  if (! handshake76_stage1 (EVD_WEBSOCKET_SERVER (web_service),
                            conn,
                            request,
                            peer))
    {
      /* invalid handshake, close connection */
      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
      return;
    }
}

static gboolean
evd_websocket_server_peer_is_connected (EvdTransport *transport,
                                        EvdPeer      *peer)
{
  EvdConnection *conn;

  conn = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);

  return (conn != NULL && ! g_io_stream_is_closed (G_IO_STREAM (conn)));
}

static gboolean
write_text_msg (EvdConnection  *conn,
                const gchar    *buf,
                gsize           size,
                GError        **error)
{
  gchar frame_type = 0x00;
  gchar end_of_frame = 0xFF;
  GOutputStream *stream;

  stream = g_io_stream_get_output_stream (G_IO_STREAM (conn));

  if (g_output_stream_write (stream,
                             &frame_type,
                             1,
                             NULL,
                             error) > 0 &&
      g_output_stream_write (stream,
                             buf,
                             size,
                             NULL,
                             error) > 0 &&
      g_output_stream_write (stream,
                             &end_of_frame,
                             1,
                             NULL,
                             error) > 0)
    {
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static gboolean
evd_websocket_server_send (EvdTransport *transport,
                           EvdPeer       *peer,
                           const gchar   *buffer,
                           gsize          size,
                           GError       **error)
{
  EvdConnection *conn;

  conn = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);
  if (conn == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Send failed. Peer is not associated with WebSocket server transport, or is corrupted");
      return FALSE;
    }
  else
    {
      return write_text_msg (conn, buffer, size, error);
    }
}

static gboolean
evd_websocket_server_remove (EvdIoStreamGroup *io_stream_group,
                             GIOStream        *io_stream)
{
  ConnData *data;

  if (! EVD_IO_STREAM_GROUP_CLASS (evd_websocket_server_parent_class)->
      remove (io_stream_group, io_stream))
    {
      return FALSE;
    }

  data = g_object_get_data (G_OBJECT (io_stream), CONN_DATA_KEY);
  if (data == NULL)
    return FALSE;

  g_object_set_data (G_OBJECT (io_stream), CONN_DATA_KEY, NULL);

  g_object_set_data (G_OBJECT (data->peer), PEER_DATA_KEY, NULL);

  g_string_free (data->buf, TRUE);

  g_object_unref (data->transport);
  g_object_unref (data->peer);
  g_object_unref (data->conn);

  g_slice_free (ConnData, data);

  return TRUE;
}

static void
evd_websocket_server_peer_closed (EvdTransport *transport,
                                  EvdPeer      *peer,
                                  gboolean      gracefully)
{
  ConnData *conn;

  conn = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);

  if (conn == NULL)
    return;

  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);

  if (! g_io_stream_is_closed (G_IO_STREAM (conn)))
    {
      /* @TODO: perform a graceful web-socket close */
      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }
}

/* public methods */

EvdWebsocketServer *
evd_websocket_server_new (void)
{
  return g_object_new (EVD_TYPE_WEBSOCKET_SERVER, NULL);
}
