/*
 * evd-websocket-common.c
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

#include "evd-websocket-common.h"

#include "evd-utils.h"
#include "evd-websocket00.h"
#include "evd-websocket08.h"

#define BLOCK_SIZE 0x0FFF

static void read_from_connection (EvdWebsocketData *data);

static void
free_data (EvdWebsocketData *data)
{
  g_return_if_fail (data != NULL);

  if (data->user_data != NULL && data->user_data_destroy_notify != NULL)
    data->user_data_destroy_notify (data->user_data);

  if (data->buf != NULL)
    g_string_free (data->buf, TRUE);

  g_slice_free (EvdWebsocketData, data);
}

static void
on_connection_read (GObject      *obj,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  EvdWebsocketData *data = user_data;
  EvdHttpConnection *conn;
  GError *error = NULL;
  gssize size;

  conn = data->conn;

  size = g_input_stream_read_finish (G_INPUT_STREAM (obj),
                                     res,
                                     &error);
  if (size < 0)
    {
      if (! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED))
        {
          /* @TODO: log error properly */
          g_debug ("Error reading from WebSocket: %s", error->message);

          g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);
        }

      g_error_free (error);
    }
  else if (size > 0)
    {
      data->buf_len += size;

      if (data->process_data_func (data))
        read_from_connection (data);
    }

  g_object_unref (conn);
}

static void
read_from_connection (EvdWebsocketData *data)
{
  GInputStream *stream;

  g_return_if_fail (data != NULL);

  if (data->buf_len + BLOCK_SIZE >= data->buf->len)
    g_string_set_size (data->buf, data->buf_len + BLOCK_SIZE);

  stream = g_io_stream_get_input_stream (G_IO_STREAM (data->conn));

  g_object_ref (data->conn);
  g_input_stream_read_async (stream,
                             data->buf->str + data->buf_len,
                             BLOCK_SIZE,
                             G_PRIORITY_DEFAULT,
                             NULL,
                             on_connection_read,
                             data);
}

static gboolean
close_in_idle (gpointer user_data)
{
  EvdWebsocketData *data = user_data;

  if (data->state != EVD_WEBSOCKET_STATE_CLOSED)
    {
      data->state = EVD_WEBSOCKET_STATE_CLOSED;

      evd_connection_flush_and_shutdown(EVD_CONNECTION (data->conn), NULL);

      data->close_cb (EVD_HTTP_CONNECTION (data->conn),
                      TRUE,
                      data->user_data);
    }

  g_object_unref (data->conn);

  return FALSE;
}

static guint8
get_version_from_request (EvdHttpRequest *request)
{
  SoupMessageHeaders *headers;
  const gchar *version_str;
  guint8 version;

  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (conn), 0);

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  version_str = soup_message_headers_get_one (headers, "Sec-Websocket-Version");
  if (version_str == NULL)
    return 0;

  version = g_ascii_strtoull (version_str, NULL, 10);

  return version;
}

/* public methods */

void
evd_websocket_common_handle_handshake_request (EvdWebService       *web_service,
                                               EvdHttpConnection   *conn,
                                               EvdHttpRequest      *request,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data)
{
  guint8 version;

  g_return_if_fail (EVD_IS_WEB_SERVICE (web_service));
  g_return_if_fail (EVD_IS_HTTP_CONNECTION (conn));
  g_return_if_fail (EVD_IS_HTTP_REQUEST (request));

  version = get_version_from_request (request);

  g_object_ref (conn);

  if (version == 0)
    {
      evd_websocket00_handle_handshake_request (web_service,
                                                conn,
                                                request,
                                                callback,
                                                user_data);
    }
  else if (version == 8)
    {
      evd_websocket08_handle_handshake_request (web_service,
                                                conn,
                                                request,
                                                callback,
                                                user_data);
    }
  else
    {
      GSimpleAsyncResult *res;

      res = g_simple_async_result_new (G_OBJECT (conn),
                                       callback,
                                       user_data,
                                       evd_websocket_common_handle_handshake_request);

      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_NOT_SUPPORTED,
                                       "Error: Websocket handshake failed, unsupported version");

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
    }
}

/**
 * evd_websocket_common_handle_handshake_request_finish:
 *
 * Returns: (transfer none):
 **/
gboolean
evd_websocket_common_handle_handshake_request_finish (GAsyncResult  *result,
                                                      GError       **error)
{
  GSimpleAsyncResult *res;

  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

  res = G_SIMPLE_ASYNC_RESULT (result);

  return ! g_simple_async_result_propagate_error (res, error);
}

void
evd_websocket_common_setup_connection (EvdHttpConnection              *conn,
                          guint8                          version,
                          gboolean                        is_server,
                          EvdWebsocketProcessDataFunc     process_data_func,
                          EvdWebsocketSendCloseFrameFunc  send_close_frame_func,
                          EvdWebsocketSendDataFrameFunc   send_data_frame_func)
{
  EvdWebsocketData *data;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (conn));

  data = g_slice_new0 (EvdWebsocketData);

  data->conn = conn;
  data->version = version;
  data->server = is_server;
  data->state = EVD_WEBSOCKET_STATE_IDLE;

  data->process_data_func = process_data_func;
  data->send_close_frame_func = send_close_frame_func;
  data->send_data_frame_func = send_data_frame_func;

  g_object_set_data_full (G_OBJECT (conn),
                          EVD_WEBSOCKET_DATA_KEY,
                          data,
                          (GDestroyNotify) free_data);
}

void
evd_websocket_common_bind (EvdHttpConnection   *conn,
                           EvdWebsocketFrameCb  frame_cb,
                           EvdWebsocketCloseCb  close_cb,
                           gpointer             user_data,
                           GDestroyNotify       user_data_destroy_notify)
{
  EvdWebsocketData *data;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (conn));
  g_return_if_fail (frame_cb != NULL);
  g_return_if_fail (close_cb != NULL);

  data = g_object_get_data (G_OBJECT (conn), EVD_WEBSOCKET_DATA_KEY);
  g_return_if_fail (data != NULL);

  data->conn = conn;

  data->frame_cb = frame_cb;
  data->close_cb = close_cb;

  data->user_data = user_data;
  data->user_data_destroy_notify = user_data_destroy_notify;

  /* start reading from websocket endpoint */
  data->buf = g_string_new_len ("", BLOCK_SIZE);

  read_from_connection (data);
}

gboolean
evd_websocket_common_is_bound (EvdHttpConnection *conn)
{
  EvdWebsocketData *data;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (conn));

  data = g_object_get_data (G_OBJECT (conn), EVD_WEBSOCKET_DATA_KEY);

  return (data != NULL && data->frame_cb != NULL);
}

gboolean
evd_websocket_common_close (EvdHttpConnection  *conn,
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

  if (data->state == EVD_WEBSOCKET_STATE_CLOSING ||
      data->state == EVD_WEBSOCKET_STATE_CLOSED)
    {
      return TRUE;
    }

  data->state = EVD_WEBSOCKET_STATE_CLOSING;

  if (! data->close_frame_sent)
    {
      result = data->send_close_frame_func (data, code, reason, error);
      data->close_frame_sent = TRUE;
    }

  if (data->server)
    {
      g_object_ref (data->conn);
      evd_timeout_add (NULL,
                       0,
                       G_PRIORITY_DEFAULT,
                       close_in_idle,
                       data);
    }

  return result;
}

gboolean
evd_websocket_common_send (EvdHttpConnection  *conn,
                           const gchar        *frame,
                           gsize               frame_len,
                           gboolean            binary,
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

  if (data->state == EVD_WEBSOCKET_STATE_CLOSING ||
      data->state == EVD_WEBSOCKET_STATE_CLOSED)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_CLOSED,
                   "Websocket connection is closed");
      return FALSE;
    }

  return data->send_data_frame_func (data, frame, frame_len, binary, error);
}
