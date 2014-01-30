/*
 * evd-websocket-protocol.c
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

#include <string.h>
#include <libsoup/soup-headers.h>

#include "evd-websocket-protocol.h"

#include "evd-utils.h"

#define EVD_WEBSOCKET_MAGIC_UUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define EVD_WEBSOCKET_DATA_KEY   "org.eventdance.lib.Websocket.CONN_DATA"

#define BLOCK_SIZE        0x00000FFF
#define MAX_FRAGMENT_SIZE 0x10000000
#define MAX_PAYLOAD_SIZE  0x40000000

/* websocket reading states */
typedef enum
{
  EVD_WEBSOCKET_READING_STATE_IDLE,
  EVD_WEBSOCKET_READING_STATE_PAYLOAD_LEN,
  EVD_WEBSOCKET_READING_STATE_HEADER,
  EVD_WEBSOCKET_READING_STATE_MASKING_KEY,
  EVD_WEBSOCKET_READING_STATE_PAYLOAD,

  /* padding for future expansion */
  EVD_WEBSOCKET_READING_STATE_PADDING0,
  EVD_WEBSOCKET_READING_STATE_PADDING1,
  EVD_WEBSOCKET_READING_STATE_PADDING2,
  EVD_WEBSOCKET_READING_STATE_PADDING3,
} EvdWebsocketReadingStates;

static const guint16 HEADER_MASK_FIN         = (1 << 15);
static const guint16 HEADER_MASK_OPCODE      = ((1 << 8) | (1 << 9) | (1 << 10) | (1 << 11));
static const guint16 HEADER_MASK_MASKED      = (1 << 7);
static const guint16 HEADER_MASK_PAYLOAD_LEN = (0x00FF & ~(1 << 7));

typedef enum
{
  OPCODE_CONTINUATION     = 0x00,
  OPCODE_TEXT_FRAME       = 0x01,
  OPCODE_BINARY_FRAME     = 0x02,
  OPCODE_NON_CONTROL_RSV0 = 0x03,
  OPCODE_NON_CONTROL_RSV1 = 0x04,
  OPCODE_NON_CONTROL_RSV2 = 0x05,
  OPCODE_NON_CONTROL_RSV3 = 0x06,
  OPCODE_NON_CONTROL_RSV4 = 0x07,
  OPCODE_CLOSE            = 0x08,
  OPCODE_PING             = 0x09,
  OPCODE_PONG             = 0x0A,
  OPCODE_CONTROL_RSV0     = 0x0B,
  OPCODE_CONTROL_RSV1     = 0x0C,
  OPCODE_CONTROL_RSV2     = 0x0D,
  OPCODE_CONTROL_RSV3     = 0x0E,
  OPCODE_CONTROL_RSV4     = 0x0F
} EvdWebsocketOpcodes;

typedef struct
{
  gboolean server;

  EvdHttpConnection *conn;

  EvdWebsocketFrameCb frame_cb;
  EvdWebsocketCloseCb close_cb;

  gpointer user_data;
  GDestroyNotify user_data_destroy_notify;

  EvdWebsocketState state;
  EvdWebsocketReadingStates reading_state;

  GString *buf;
  gsize buf_len;
  gsize offset;

  guint8 opcode;
  gsize payload_len;
  gchar *frame_data;
  gsize frame_len;

  gboolean close_frame_sent;
  gboolean close_frame_received;

  guint16 close_code;
  gchar *close_reason;

  gboolean fin;
  gboolean masked;
  guint8 masking_key[4];
  gchar *extensions_data;
  gsize extension_len;

  guint close_timeout_src_id;
} EvdWebsocketData;

static void read_from_connection    (EvdWebsocketData *data);

static void on_close_frame_received (EvdWebsocketData *data,
                                     guint16           code,
                                     const gchar      *reason);

static void
apply_masking (gchar *frame, gsize frame_len, guint8 masking_key[4])
{
  gsize i;
  guint8 j;

  for (i=0; i<frame_len; i++)
    {
      j = i % 4;
      frame[i] = frame[i] ^ masking_key[j];
    }
}

static void
build_frame (GString     *frame,
             gboolean     fin,
             guint8       opcode,
             gboolean     masked,
             const gchar *payload,
             gsize        payload_len)
{
  guint16 header = 0;
  gsize payload_len_hbo; /* host byte order */
  guint8 payload_len_len; /* length of the extra bytes for payload length */
  guint32 masking_key;

  /* @TODO: by now this method assumes no extension data */

  payload_len_hbo = 0;
  payload_len_len = 0;

  header = fin ? HEADER_MASK_FIN : 0;
  header |= opcode << 8;
  header |= masked ? HEADER_MASK_MASKED : 0;

  if (payload_len <= 125)
    {
      header |= (guint16) payload_len;
    }
  else if (payload_len < G_MAXUINT16)
    {
      header |= (guint16) 126;
      payload_len_hbo = GUINT16_FROM_BE ((guint16) payload_len);
      payload_len_len = 2;
    }
  else
    {
      header |= (guint16) 127;
      payload_len_hbo = GUINT64_FROM_BE ((guint64) payload_len);
      payload_len_len = 8;
    }

  g_string_set_size (frame, frame->len + 2);
  frame->str[0] = (gchar) ((header & 0xFF00) >> 8);
  frame->str[1] = (gchar) (header & 0x00FF);

  if (payload_len_len > 0)
    g_string_append_len (frame,
                         (const gchar *) &payload_len_hbo,
                         payload_len_len);

  if (masked)
    {
      masking_key = g_random_int ();
      g_string_append_len (frame,
                           (const gchar *) &masking_key,
                           4);
    }

  g_string_append_len (frame, payload, payload_len);

  if (masked)
    {
      apply_masking (frame->str + (frame->len - payload_len),
                     payload_len,
                     (guint8 *) &masking_key);
    }
}

static gboolean
send_close_frame (EvdWebsocketData  *data,
                  guint16            code,
                  const gchar       *reason,
                  GError           **error)
{
  gboolean result = TRUE;
  GString *frame;
  GOutputStream *stream;

  /* @TODO: send the code and reason. By now send no payload */
  data->frame_data = NULL;
  data->frame_len = 0;

  frame = g_string_new ("");
  build_frame (frame,
               TRUE,
               OPCODE_CLOSE,
               (! data->server),
               data->frame_data,
               data->frame_len);

  stream = g_io_stream_get_output_stream (G_IO_STREAM (data->conn));
  if (g_output_stream_write (stream,
                             frame->str,
                             frame->len,
                             NULL,
                             error) < 0)
    {
      result = FALSE;
    }

  g_string_free (frame, TRUE);

  return result;
}

static gboolean
send_data_frame (EvdWebsocketData  *data,
                 const gchar       *frame,
                 gsize              frame_len,
                 EvdMessageType     frame_type,
                 GError           **error)
{
  gsize bytes_sent;
  gsize bytes_left;
  GString *frag;
  GOutputStream *stream;
  gboolean result = TRUE;

  frag = g_string_new ("");

  stream = g_io_stream_get_output_stream (G_IO_STREAM (data->conn));

  bytes_sent = 0;
  bytes_left = frame_len;
  while (bytes_left > 0)
    {
      gsize frag_len;
      gboolean fin;
      guint8 opcode;
      gboolean masked;

      frag_len = MIN (MAX_FRAGMENT_SIZE, bytes_left);

      fin = frag_len >= bytes_left;

      opcode = bytes_sent == 0 ?
        (frame_type == EVD_MESSAGE_TYPE_TEXT ? OPCODE_TEXT_FRAME : OPCODE_BINARY_FRAME) :
        OPCODE_CONTINUATION;

      masked = ! data->server;

      build_frame (frag,
                   fin,
                   opcode,
                   masked,
                   frame + bytes_sent,
                   frag_len);

      if (! g_output_stream_write (stream,
                                   frag->str,
                                   frag->len,
                                   NULL,
                                   error) < 0)
        {
          result = FALSE;
          break;
        }

      bytes_sent += frag_len;
      bytes_left -= frag_len;

      g_string_set_size (frag, 0);
    }

  g_string_free (frag, TRUE);

  return result;
}

static gboolean
handle_control_frame (EvdWebsocketData *data)
{
  switch (data->opcode)
    {
    case OPCODE_CLOSE:
      /* @TODO: load code and reason from payload */
      on_close_frame_received (data, EVD_WEBSOCKET_CLOSE_NO_STATUS, NULL);
      break;

    default:
      /* @TODO: handle 'ping' and 'pong' control frames */
      data->state = EVD_WEBSOCKET_STATE_CLOSED;
      g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);
      g_warning ("Error, handling 'ping' and/or 'pong' control frames is not"
                 " yet implemented in websocket version 08");
      break;
    }

  return TRUE;
}

static gboolean
read_payload (EvdWebsocketData *data)
{
  if (data->buf_len - data->offset < data->payload_len)
    return FALSE;

  data->extensions_data = data->buf->str + data->offset;

  data->frame_len = data->payload_len - data->extension_len;
  data->frame_data = data->buf->str + data->offset + data->extension_len;

  if (data->masked)
    apply_masking (data->frame_data, data->frame_len, data->masking_key);

  if (data->opcode >= OPCODE_CLOSE)
    {
      /* control frame */
      handle_control_frame (data);
    }
  else
    {
      /* data frame */

      if (data->fin)
        {
          data->frame_cb (EVD_HTTP_CONNECTION (data->conn),
                          data->frame_data,
                          data->frame_len,
                          data->opcode == OPCODE_BINARY_FRAME,
                          data->user_data);
        }
      else
        {
          /* @TODO: handle fragmented frames */
          g_warning ("Error, receiving fragmented frames is not yet implemented"
                     " in websocket version 08");
          data->state = EVD_WEBSOCKET_STATE_CLOSED;
          g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);
        }
    }

  /* reset state */
  data->offset += data->payload_len;
  data->reading_state = EVD_WEBSOCKET_READING_STATE_IDLE;

  g_string_erase (data->buf, 0, data->offset);
  data->buf_len -= data->offset;
  data->offset = 0;

  return TRUE;
}

static gboolean
read_masking_key (EvdWebsocketData *data)
{
  if (data->buf_len - data->offset < 4)
    return FALSE;

  memcpy (&data->masking_key, data->buf->str + data->offset, 4);

  data->offset += 4;

  data->reading_state = EVD_WEBSOCKET_READING_STATE_PAYLOAD;
  if (data->payload_len == 0)
    return read_payload (data);

  return TRUE;
}

static gboolean
read_payload_len (EvdWebsocketData *data)
{
  if (data->payload_len == 126)
    {
      guint16 len;

      if (data->buf_len - data->offset < 2)
        return FALSE;

      memcpy (&len, data->buf->str + data->offset, 2);
      data->offset += 2;

      data->payload_len = (gsize) GUINT16_FROM_BE(len);
    }
  else
    {
      guint64 len;

      if (data->buf_len - data->offset < 8)
        return FALSE;

      memcpy (&len, data->buf->str + data->offset, 8);
      data->offset += 8;

      data->payload_len = (gsize) GUINT64_FROM_BE(len);
    }

  /* @TODO: validated payload length against MAX_PAYLOAD_SIZE */

  if (data->masked)
    data->reading_state = EVD_WEBSOCKET_READING_STATE_MASKING_KEY;
  else
    data->reading_state = EVD_WEBSOCKET_READING_STATE_PAYLOAD;

  return TRUE;
}

static gboolean
read_header (EvdWebsocketData *data)
{
  guint16 header = 0;

  if (data->buf_len - data->offset < 2)
    return FALSE;

  header = ( (guint8) data->buf->str[data->offset] << 8) +
    (guint8) data->buf->str[data->offset + 1];

  data->offset += 2;

  /* fin flag */
  data->fin = (guint16) (header & HEADER_MASK_FIN);

  /* opcode */
  data->opcode = (guint8) ((header & HEADER_MASK_OPCODE) >> 8);

  /* masking */
  data->masked = (guint16) (header & HEADER_MASK_MASKED);

  /* payload len */
  data->payload_len = header & HEADER_MASK_PAYLOAD_LEN;

  /* @TODO: validate header values */

  if (data->payload_len > 125)
    data->reading_state = EVD_WEBSOCKET_READING_STATE_PAYLOAD_LEN;
  else if (data->masked)
    data->reading_state = EVD_WEBSOCKET_READING_STATE_MASKING_KEY;
  else
    data->reading_state = EVD_WEBSOCKET_READING_STATE_PAYLOAD;

  return TRUE;
}

static gboolean
process_data (EvdWebsocketData *data)
{
  while (data->offset < data->buf_len &&
         data->state != EVD_WEBSOCKET_STATE_CLOSED)
    {
      if (data->reading_state == EVD_WEBSOCKET_READING_STATE_IDLE)
        if (! read_header (data))
          return TRUE;

      switch (data->reading_state)
        {
        case EVD_WEBSOCKET_READING_STATE_PAYLOAD_LEN:
          if (! read_payload_len (data))
            return TRUE;
          break;

        case EVD_WEBSOCKET_READING_STATE_MASKING_KEY:
          if (! read_masking_key (data))
            return TRUE;
          break;

        case EVD_WEBSOCKET_READING_STATE_PAYLOAD:
          if (! read_payload (data))
            return TRUE;
          break;

        default:
          {
            /* @TODO */
          }
        }
    }

  return data->state != EVD_WEBSOCKET_STATE_CLOSED;
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
          g_print ("Error reading from WebSocket: %s", error->message);

          g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);
        }

      g_error_free (error);
    }
  else if (size > 0)
    {
      data->buf_len += size;

      if (data->state != EVD_WEBSOCKET_STATE_CLOSED &&
          process_data (data))
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
close_timeout (gpointer user_data)
{
  EvdWebsocketData *data = user_data;

  data->close_timeout_src_id = 0;

  data->state = EVD_WEBSOCKET_STATE_CLOSED;

  data->close_cb (EVD_HTTP_CONNECTION (data->conn),
                  FALSE,
                  data->user_data);

  g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);

  g_object_unref (data->conn);

  return FALSE;
}

static guint8
get_version_from_request (EvdHttpRequest *request)
{
  SoupMessageHeaders *headers;
  const gchar *version_str;
  guint8 version;

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  version_str = soup_message_headers_get_one (headers, "Sec-Websocket-Version");
  if (version_str == NULL)
    return 0;

  version = g_ascii_strtoull (version_str, NULL, 10);

  return version;
}

static gchar *
get_accept_key (const gchar *key)
{
  gchar *concat;
  GChecksum *checksum;
  guint8 *digest;
  gsize digest_len;
  gchar *accept;

  concat = g_strconcat (key, EVD_WEBSOCKET_MAGIC_UUID, NULL);

  checksum = g_checksum_new (G_CHECKSUM_SHA1);

  g_checksum_update (checksum, (guchar *) concat, -1);
  digest_len = g_checksum_type_get_length (G_CHECKSUM_SHA1);
  digest = g_slice_alloc (digest_len);
  g_checksum_get_digest (checksum, digest, &digest_len);

  accept = g_base64_encode (digest, digest_len);

  g_slice_free1 (digest_len, digest);
  g_checksum_free (checksum);
  g_free (concat);

  return accept;
}

static void
free_websocket_connection_data (EvdWebsocketData *data)
{
  g_return_if_fail (data != NULL);

  if (data->user_data != NULL && data->user_data_destroy_notify != NULL)
    data->user_data_destroy_notify (data->user_data);

  if (data->buf != NULL)
    g_string_free (data->buf, TRUE);

  if (data->close_timeout_src_id != 0)
    {
      g_source_remove (data->close_timeout_src_id);
      data->close_timeout_src_id = 0;
      g_object_unref (data->conn);
    }

  g_free (data->close_reason);

  g_slice_free (EvdWebsocketData, data);
}

static void
setup_connection (EvdHttpConnection *conn,
                  gboolean           is_server,
                  EvdWebsocketState  state)
{
  EvdWebsocketData *data;

  data = g_slice_new0 (EvdWebsocketData);

  data->conn = conn;
  data->server = is_server;
  data->state = state;
  data->reading_state = EVD_WEBSOCKET_READING_STATE_IDLE;

  g_object_set_data_full (G_OBJECT (conn),
                          EVD_WEBSOCKET_DATA_KEY,
                          data,
                          (GDestroyNotify) free_websocket_connection_data);
}

static void
finish_close_handshake (EvdWebsocketData *data,
                        guint16           code,
                        const gchar      *reason)
{
  data->state = EVD_WEBSOCKET_STATE_CLOSED;

  if (data->close_cb != NULL)
    {
      gboolean gracefully;

      gracefully =
        code == EVD_WEBSOCKET_CLOSE_NORMAL ||
        code == EVD_WEBSOCKET_CLOSE_NO_STATUS;

      data->close_cb (EVD_HTTP_CONNECTION (data->conn),
                      gracefully,
                      data->user_data);
    }

  g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);

  if (data->close_timeout_src_id != 0)
    {
      g_source_remove (data->close_timeout_src_id);
      data->close_timeout_src_id = 0;
      g_object_unref (data->conn);
    }
}

static void
on_close_frame_received (EvdWebsocketData *data,
                         guint16           code,
                         const gchar      *reason)
{
  data->close_frame_received = TRUE;

  if (data->state == EVD_WEBSOCKET_STATE_CLOSING)
    {
      finish_close_handshake (data, code, reason);
    }
  else
    {
      evd_websocket_protocol_close (data->conn, code, NULL, NULL);
    }
}

static void
on_connection_flushed (GObject      *obj,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  GError *error = NULL;
  EvdWebsocketData *data = user_data;

  if (! g_output_stream_flush_finish (G_OUTPUT_STREAM (obj),
                                      res,
                                      &error))
    {
      /* @TODO: do proper logging */
      g_print ("Error flushing WebSocket connection: %s\n", error->message);
      g_error_free (error);
    }

  finish_close_handshake (user_data, data->close_code, data->close_reason);

  g_object_unref (data->conn);
  g_object_unref (obj);
}

/* public methods */

gboolean
evd_websocket_protocol_handle_handshake_request (EvdHttpConnection  *conn,
                                                 EvdHttpRequest     *request,
                                                 GError            **error)
{
  guint8 version;
  gboolean result = FALSE;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (request), FALSE);

  version = get_version_from_request (request);

  if (version != 13)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "The WebSocket protocol version MUST be 13 [RFC 6455, 4.1.9]");
      return FALSE;
    }

  SoupMessageHeaders *req_headers;
  SoupMessageHeaders *res_headers = NULL;

  const gchar *key;
  gchar *accept_key;
  const gchar *conn_header;

  req_headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  conn_header = soup_message_headers_get_one (req_headers, "Connection");

  if (g_strcmp0 (soup_message_headers_get_one (req_headers, "Upgrade"),
                 "websocket") != 0 || conn_header == NULL ||
      g_strstr_len (conn_header, -1, "Upgrade") == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Invalid Websocket handshake request");
      goto finish;
    }

  key = soup_message_headers_get_one (req_headers, "Sec-WebSocket-Key");
  if (key == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Invalid Websocket handshake request, missing 'Sec-Websocket-Key' header");
      goto finish;
    }

  accept_key = get_accept_key (key);

  /* build HTTP response */
  res_headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

  soup_message_headers_replace (res_headers, "Connection", "Upgrade");
  soup_message_headers_replace (res_headers, "Upgrade", "websocket");
  soup_message_headers_replace (res_headers, "Sec-WebSocket-Accept", accept_key);

  g_free (accept_key);

  /* send handshake response headers */
  if (! evd_http_connection_write_response_headers (conn,
                                                    SOUP_HTTP_1_1,
                                                    SOUP_STATUS_SWITCHING_PROTOCOLS,
                                                    NULL,
                                                    res_headers,
                                                    error))
    {
      goto finish;
    }

  /* success, setup the WebSocket connection */
  setup_connection (conn, TRUE, EVD_WEBSOCKET_STATE_OPENED);

  result = TRUE;

 finish:

  if (res_headers != NULL)
    soup_message_headers_free (res_headers);

  return result;
}

/**
 * evd_websocket_protocol_create_handshake_request:
 *
 * Returns: (transfer full):
 **/
EvdHttpRequest *
evd_websocket_protocol_create_handshake_request (EvdConnection  *conn,
                                                 const gchar    *url,
                                                 const gchar    *sub_protocol,
                                                 const gchar    *origin,
                                                 gchar         **key_base64)
{
  EvdHttpRequest *request;
  SoupMessageHeaders *headers;

  guchar key[16];
  gchar *key_b64;
  gint i;

  request = evd_http_request_new (conn, SOUP_METHOD_GET, url);

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));
  soup_message_headers_replace (headers, "Upgrade", "websocket");
  soup_message_headers_replace (headers, "Connection", "Upgrade");
  soup_message_headers_replace (headers, "Sec-WebSocket-Version", "13");

  if (sub_protocol != NULL)
    soup_message_headers_replace (headers,
                                  "Sec-WebSocket-Protocol",
                                  sub_protocol);

  if (origin != NULL)
    soup_message_headers_replace (headers,
                                  "Sec-WebSocket-Origin",
                                  origin);

  for (i=0; i<4; i++)
    {
      guint32 rnd;

      rnd = g_random_int ();

      key[i*4 + 0] = rnd >> 24 & 0xFF;
      key[i*4 + 1] = rnd >> 16 & 0xFF;
      key[i*4 + 2] = rnd >>  8 & 0xFF;
      key[i*4 + 3] = rnd       & 0xFF;
    }

  key_b64 = g_base64_encode (key, 16);
  soup_message_headers_replace (headers, "Sec-WebSocket-Key", key_b64);

  if (key_base64 != NULL)
    *key_base64 = key_b64;
  else
    g_free (key_b64);

  return request;
}

gboolean
evd_websocket_protocol_handle_handshake_response (EvdHttpConnection   *conn,
                                                  SoupHTTPVersion      http_version,
                                                  guint                status_code,
                                                  SoupMessageHeaders  *headers,
                                                  const gchar         *handshake_key,
                                                  GError             **error)
{
  const gchar *accept_key;
  gchar *expected_accept_key;
  gboolean result = TRUE;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);

  if (http_version != SOUP_HTTP_1_1)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Invalid HTTP version received, expected 1.1");
      return FALSE;
    }

  if (status_code != SOUP_STATUS_SWITCHING_PROTOCOLS)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Invalid status code received: %u", status_code);
      return FALSE;
    }

  accept_key = soup_message_headers_get_one (headers, "Sec-WebSocket-Accept");
  if (accept_key == NULL || strlen (accept_key) == 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Received invalid 'Sec-WebSocket-Accept' header");
      return FALSE;
    }

  expected_accept_key = get_accept_key (handshake_key);

  if (g_strcmp0 (accept_key, expected_accept_key) != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Received invalid accept key");
      result = FALSE;
    }
  else
    {
      /* setup websocket data on connection */
      setup_connection (conn, FALSE, EVD_WEBSOCKET_STATE_OPENED);
    }

  g_free (expected_accept_key);

  return result;
}

/**
 * evd_websocket_protocol_bind:
 * @frame_cb: (scope notified):
 * @close_cb: (scope notified):
 *
 **/
void
evd_websocket_protocol_bind (EvdHttpConnection   *conn,
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
  g_return_if_fail (data->conn == conn);

  data->frame_cb = frame_cb;
  data->close_cb = close_cb;

  data->user_data = user_data;
  data->user_data_destroy_notify = user_data_destroy_notify;

  data->buf = g_string_new_len ("", BLOCK_SIZE);

  /* start reading from websocket endpoint */
  read_from_connection (data);
}

void
evd_websocket_protocol_unbind (EvdHttpConnection *conn)
{
  EvdWebsocketData *data;

  g_return_if_fail (EVD_IS_HTTP_CONNECTION (conn));

  data = g_object_get_data (G_OBJECT (conn), EVD_WEBSOCKET_DATA_KEY);
  if (data == NULL)
    return;

  data->frame_cb = NULL;
  data->close_cb = NULL;

  if (data->user_data != NULL && data->user_data_destroy_notify != NULL)
    data->user_data_destroy_notify (data->user_data);

  data->user_data = NULL;
  data->user_data_destroy_notify = NULL;

  /* stop reading from websocket endpoint */
  /* @TODO: we need to cancel any ongoing read operation */
}

gboolean
evd_websocket_protocol_close (EvdHttpConnection  *conn,
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
                   "Not a WebSocket connection");
      return FALSE;
    }

  if (data->state == EVD_WEBSOCKET_STATE_CLOSING ||
      data->state == EVD_WEBSOCKET_STATE_CLOSED)
    {
      return TRUE;
    }

  /* start the WebSocket Closing Handshake */

  data->state = EVD_WEBSOCKET_STATE_CLOSING;

  g_assert (! data->close_frame_sent);

  result = send_close_frame (data, code, reason, error);
  data->close_frame_sent = TRUE;

  if (data->close_frame_received)
    {
      GOutputStream *stream;

      data->close_code = code;
      data->close_reason = g_strdup (reason);

      stream = g_io_stream_get_output_stream (G_IO_STREAM (data->conn));

      g_object_ref (data->conn);
      g_object_ref (stream);
      g_output_stream_flush_async (stream,
                      evd_connection_get_priority (EVD_CONNECTION (data->conn)),
                      NULL,
                      on_connection_flushed,
                      data);
    }
  else
    {
      /* force closing the WebSocket Connection after a grace period */
      g_object_ref (data->conn);
      data->close_timeout_src_id = evd_timeout_add (NULL,
                                                    3000,
                                                    G_PRIORITY_DEFAULT,
                                                    close_timeout,
                                                    data);
    }

  return result;
}

gboolean
evd_websocket_protocol_send (EvdHttpConnection  *conn,
                             const gchar        *frame,
                             gsize               frame_len,
                             EvdMessageType      frame_type,
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

  return send_data_frame (data, frame, frame_len, frame_type, error);
}

EvdWebsocketState
evd_websocket_protocol_get_state (EvdHttpConnection *conn)
{
  EvdWebsocketData *data;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);

  data = g_object_get_data (G_OBJECT (conn), EVD_WEBSOCKET_DATA_KEY);
  if (data == NULL)
    return EVD_WEBSOCKET_STATE_NONE;
  else
    return data->state;
}
