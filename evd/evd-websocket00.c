/*
 * evd-websocket00.c
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

#include <string.h>
#include <libsoup/soup-headers.h>

#include "evd-websocket00.h"

#include "evd-websocket-common.h"
#include "evd-utils.h"

typedef enum
{
  STATE_IDLE,
  STATE_READING_PAYLOAD_LEN,
  STATE_READING_BINARY_FRAME,
  STATE_READING_TEXT_FRAME,
  STATE_CLOSING,
  STATE_CLOSED
} EvdWebsocket00States;

typedef struct
{
  EvdWebService *web_service;
  EvdHttpRequest *request;
  GSimpleAsyncResult *async_result;
  guint32 value1;
  guint32 value2;
  gchar payload[8];
} HandshakeData;

static gboolean
send_close_frame (EvdWebsocketData *data, GError **error)
{
  guint16 close_code = 0xFF00;
  GOutputStream *stream;

  stream = g_io_stream_get_output_stream (G_IO_STREAM (data->conn));

  return g_output_stream_write (stream,
                                &close_code,
                                2,
                                NULL,
                                error);
}

static gboolean
read_frame_type (EvdWebsocketData *data)
{
  if (data->buf_len - data->offset < 1)
    return FALSE;

  data->opcode = data->buf->str[data->offset];
  data->offset++;

  if (data->opcode == 0xFF)
    data->state = STATE_CLOSING;
  else if ((data->opcode & 0x80) == 0x80)
    data->state = STATE_READING_PAYLOAD_LEN;
  else
    {
      data->frame_data = data->buf->str + data->offset;
      data->state = STATE_READING_TEXT_FRAME;
    }

  return TRUE;
}

static gboolean
read_text_frame (EvdWebsocketData *data)
{
  while (data->offset < data->buf_len)
    {
      if (data->buf->str[data->offset] == (gchar) 0xFF)
        {
          data->frame_len =
            (gsize) ((data->buf->str + data->offset) - data->frame_data);

          data->buf->str[data->offset] = '\0';
          data->offset++;

          data->frame_cb (EVD_HTTP_CONNECTION (data->conn),
                          data->frame_data,
                          data->frame_len,
                          FALSE,
                          data->user_data);

          /* reset state */
          data->state = STATE_IDLE;
          g_string_erase (data->buf, 0, data->offset);
          data->buf_len -= data->offset;
          data->offset = 0;
          data->payload_len = 0;

          return TRUE;
        }
      else
        {
          data->offset++;
        }
    }

  return FALSE;
}

static gboolean
read_frame_len (EvdWebsocketData *data)
{
  guint8 b;
  guint8 b_v;

  while (data->offset < data->buf_len)
    {
      b = data->buf->str[data->offset];

      data->offset++;

      b_v = b & 0x7F;

      data->payload_len = data->payload_len * 128 + b_v;

      if ((b_v & 0x80) == 0)
        return TRUE;
    }

  return FALSE;
}

static gboolean
handle_close_handshake (EvdWebsocketData *data)
{
  if (! read_frame_len (data))
    return FALSE;

  if (data->payload_len != 0)
    {
      /* @TODO: handle error condition */
      g_print ("ERROR: invalid websocket close frame received\n");

      data->state = STATE_CLOSED;
      g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);
    }
  else
    {
      GError *error = NULL;

      if (! data->close_frame_sent)
        {
          if (! send_close_frame (data, &error))
            {
              /* @TODO: handle error */
              g_print ("ERROR sending websocket close frame: %s\n", error->message);
              g_error_free (error);
            }

          data->close_frame_sent = TRUE;
        }
    }

  evd_connection_flush_and_shutdown (data->conn, NULL);

  data->state = STATE_CLOSED;
  data->close_cb (EVD_HTTP_CONNECTION (data->conn),
                  TRUE,
                  data->user_data);

  return TRUE;
}

static gboolean
process_data (EvdWebsocketData *data)
{
  while (data->offset < data->buf_len && data->state != STATE_CLOSED)
    {
      switch (data->state)
        {
        case STATE_IDLE:
          if (! read_frame_type (data))
            return TRUE;
          break;

        case STATE_READING_TEXT_FRAME:
          if (! read_text_frame (data))
            return TRUE;
          break;

        case STATE_CLOSING:
          if (! handle_close_handshake (data))
            return TRUE;
          break;

        case STATE_READING_PAYLOAD_LEN:
          {
            if (! read_frame_len (data))
              return TRUE;
            else
              data->state = STATE_READING_BINARY_FRAME;

            break;
          }

        case STATE_READING_BINARY_FRAME:
          {
            /* @TODO: binary frames are not supported in this version,
               what shall we do? */
            g_warning ("Attempted to read a websocket binary frame, which is not supported.\n");

            data->state = STATE_CLOSED;
            g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);

            break;
          }
        }
    }

  return data->state != STATE_CLOSED;
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

static void
complete_handshake (EvdWebService      *web_service,
                    EvdHttpConnection  *conn,
                    EvdHttpRequest     *request,
                    GSimpleAsyncResult *async_result,
                    guint32             value1,
                    guint32             value2,
                    const gchar        *payload)
{
  guchar st[16];
  guchar md5[16];
  gsize md5_len = 16;
  GChecksum *chksum;
  SoupMessageHeaders *res_headers;
  GError *error = NULL;
  const gchar *origin;
  SoupURI *uri;
  SoupURI *ws_uri;
  gchar *ws_uri_str;

  /* compute MD5 checksum */
  value1 = g_htonl (value1);
  value2 = g_htonl (value2);

  memmove (st, &value1, 4);
  memmove (st + 4, &value2, 4);
  memmove (st + 8, payload, 8);

  chksum = g_checksum_new (G_CHECKSUM_MD5);
  g_checksum_update (chksum, st, 16);
  g_checksum_get_digest (chksum, md5, &md5_len);
  g_checksum_free (chksum);

  /* build HTTP response */
  res_headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

  soup_message_headers_replace (res_headers, "Connection", "Upgrade");
  soup_message_headers_replace (res_headers, "Upgrade", "WebSocket");

  /* we assume that origin was already validated in EvdWebService */
  origin = evd_http_request_get_origin (request);
  soup_message_headers_replace (res_headers, "Sec-WebSocket-Origin", origin);

  uri = evd_http_request_get_uri (request);
  ws_uri = soup_uri_copy (uri);

  if (evd_connection_get_tls_active (EVD_CONNECTION (conn)))
    {
      soup_uri_set_scheme (ws_uri, "wss");
      if (uri->port == 443)
        soup_uri_set_port (ws_uri, 0);
      else
        soup_uri_set_port (ws_uri, uri->port);
    }
  else
    {
      soup_uri_set_scheme (ws_uri, "ws");
      if (uri->port == 80)
        soup_uri_set_port (ws_uri, 0);
      else
        soup_uri_set_port (ws_uri, uri->port);
    }

  ws_uri_str = soup_uri_to_string (ws_uri, FALSE);
  soup_message_headers_replace (res_headers, "Sec-WebSocket-Location", ws_uri_str);
  g_free (ws_uri_str);
  soup_uri_free (ws_uri);

  /* send handshake response */
  if (! evd_http_connection_write_response_headers (conn,
                                                    SOUP_HTTP_1_1,
                                                    SOUP_STATUS_SWITCHING_PROTOCOLS,
                                                    "WebSocket Upgrade",
                                                    res_headers,
                                                    &error) ||
      ! evd_http_connection_write_content (conn,
                                           (gchar *) md5,
                                           16,
                                           FALSE,
                                           &error))
    {
      g_simple_async_result_take_error (async_result, error);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }
  else
    {
      /* handshake completed */
      EvdWebsocketData *data;

      data = g_slice_new0 (EvdWebsocketData);
      data->version = 0;
      data->server = TRUE;
      data->state = STATE_IDLE;

      data->process_data_func = process_data;

      g_object_set_data_full (G_OBJECT (conn),
                              EVD_WEBSOCKET_DATA_KEY,
                              data,
                              (GDestroyNotify) evd_websocket_common_free_data);

      g_object_ref (conn);
      g_simple_async_result_set_op_res_gpointer (async_result,
                                                 conn,
                                                 g_object_unref);
    }

  g_simple_async_result_complete_in_idle (async_result);
  g_object_unref (async_result);

  soup_message_headers_free (res_headers);
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
      g_simple_async_result_take_error (data->async_result, error);
      g_simple_async_result_complete (data->async_result);
      g_object_unref (data->async_result);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }
  else
    {
      complete_handshake (data->web_service,
                          conn,
                          data->request,
                          data->async_result,
                          data->value1,
                          data->value2,
                          data->payload);
    }

   g_object_unref (data->request);
   g_object_unref (data->web_service);
   g_slice_free (HandshakeData, data);
}

static gboolean
write_text_frame (EvdHttpConnection  *conn,
                  const gchar        *buf,
                  gsize               size,
                  GError            **error)
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
close_connection_id_idle (gpointer _data)
{
  EvdWebsocketData *data = _data;

  if (data->state != STATE_CLOSED)
    {

      data->state = STATE_CLOSED;

      if (! g_io_stream_is_closed (G_IO_STREAM (data->conn)))
        evd_connection_flush_and_shutdown(data->conn, NULL);

      data->close_cb (EVD_HTTP_CONNECTION (data->conn),
                      TRUE,
                      data->user_data);
    }

  g_object_unref (data->conn);

  return FALSE;
}

/* public methods */

void
evd_websocket00_handle_handshake_request (EvdWebService       *web_service,
                                          EvdHttpConnection   *conn,
                                          EvdHttpRequest      *request,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
  GSimpleAsyncResult *res;

  SoupMessageHeaders *req_headers;
  GError *error = NULL;

  const gchar *key1, *key2;
  guint32 value1 = 0, value2 = 0;

  g_return_if_fail (EVD_IS_WEB_SERVICE (web_service));
  g_return_if_fail (EVD_IS_HTTP_CONNECTION (conn));
  g_return_if_fail (EVD_IS_HTTP_REQUEST (request));

  res = g_simple_async_result_new (NULL,
                                   callback,
                                   user_data,
                                   evd_websocket00_handle_handshake_request);

  req_headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  if (g_strcmp0 (soup_message_headers_get_one (req_headers, "Upgrade"),
                 "WebSocket") != 0 ||
      g_strcmp0 (soup_message_headers_get_one (req_headers, "Connection"),
                 "Upgrade") != 0)
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Invalid Websocket handshake request");
      goto out;
    }

  key1 = soup_message_headers_get_one (req_headers, "Sec-WebSocket-Key1");
  key2 = soup_message_headers_get_one (req_headers, "Sec-WebSocket-Key2");
  if (key1 == NULL || key2 == NULL)
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Invalid Websocket handshake request");
      goto out;
    }

  value1 = get_value_from_key (key1);
  value2 = get_value_from_key (key2);
  if (value1 == 0 || value2 == 0)
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Invalid Websocket handshake request");
      goto out;
    }

 out:

  if (error != NULL)
    {
      g_simple_async_result_take_error (res, error);
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }
  else
    {
      HandshakeData *data;

      /* bind websocket data to connection */
      data = g_slice_new0 (HandshakeData);

      data->web_service = web_service;
      data->request = request;
      data->value1 = value1;
      data->value2 = value2;
      data->async_result = res;

      g_object_ref (request);
      g_object_ref (web_service);
      evd_http_connection_read_content (conn,
                                        data->payload,
                                        8,
                                        NULL,
                                        on_handshake_payload_read,
                                        data);
    }
}

gboolean
evd_websocket00_send (EvdHttpConnection  *conn,
                      const gchar        *frame,
                      gsize               frame_len,
                      gboolean            is_binary,
                      GError            **error)
{
  EvdWebsocketData *data;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);
  g_return_val_if_fail (frame != NULL, FALSE);

  data = g_object_get_data (G_OBJECT (conn), EVD_WEBSOCKET_DATA_KEY);
  if (data == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_INITIALIZED,
                   "Given HTTP connection doesn't appear to be initialized for Websocket");
      return FALSE;
    }

  if (data->state == STATE_CLOSING || data->state == STATE_CLOSED)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_CLOSED,
                   "Websocket connection is closed");
      return FALSE;
    }

  if (! is_binary)
    {
      return write_text_frame (conn, frame, frame_len, error);
    }
  else
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "Sending binary frames is not supported in version 00 of the Websocket protocol");
      return FALSE;
    }
}

gboolean
evd_websocket00_close (EvdHttpConnection  *conn,
                       guint16             code,
                       const gchar        *reason,
                       GError            **error)
{
  EvdWebsocketData *data;
  gboolean result = TRUE;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);

  data = g_object_get_data (G_OBJECT (conn), EVD_WEBSOCKET_DATA_KEY);
  if (data == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_INITIALIZED,
                   "Given HTTP connection doesn't appear to be initialized for Websocket");
      return FALSE;
    }

  if (data->state == STATE_CLOSED)
    return TRUE;

  if (! data->close_frame_sent)
    {
      result = send_close_frame (data, error);
      data->close_frame_sent = TRUE;
    }

  data->state = STATE_CLOSING;

  if (data->server)
    {
      g_object_ref (data->conn);
      evd_timeout_add (NULL,
                       0,
                       G_PRIORITY_DEFAULT,
                       close_connection_id_idle,
                       data);
    }

  return result;
}
