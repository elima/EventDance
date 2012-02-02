/*
 * evd-websocket08.c
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

#include "evd-websocket08.h"

#include "evd-websocket-common.h"
#include "evd-utils.h"

#define EVD_WEBSOCKET_MAGIC_UUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define EVD_WEBSOCKET_MAX_FRAGMENT_SIZE 0x10000000

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

      frag_len = MIN (EVD_WEBSOCKET_MAX_FRAGMENT_SIZE, bytes_left);

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
      {
        if (! data->close_frame_sent)
          {
            GError *error = NULL;

            if (! send_close_frame (data, 0, NULL, &error))
              {
                /* @TODO: handle error condition */
                g_warning ("ERROR sending websocket close frame: %s\n", error->message);
                g_error_free (error);

                data->state = EVD_WEBSOCKET_STATE_CLOSED;
                g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);
              }

            data->close_frame_sent = TRUE;
          }

        evd_connection_flush_and_shutdown (EVD_CONNECTION (data->conn), NULL);

        data->state = EVD_WEBSOCKET_STATE_CLOSED;
        data->close_cb (EVD_HTTP_CONNECTION (data->conn),
                        TRUE,
                        data->user_data);

        break;
      }

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
    data->state = EVD_WEBSOCKET_STATE_READING_PAYLOAD_LEN;
  else if (data->masked)
    data->state = EVD_WEBSOCKET_STATE_READING_MASKING_KEY;
  else
    data->state = EVD_WEBSOCKET_STATE_READING_PAYLOAD;

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

  /* @TODO: validated payload length against EVD_WEBSOCKET_MAX_PAYLOAD_SIZE */

  if (data->masked)
    data->state = EVD_WEBSOCKET_STATE_READING_MASKING_KEY;
  else
    data->state = EVD_WEBSOCKET_STATE_READING_PAYLOAD;

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
  data->state = EVD_WEBSOCKET_STATE_IDLE;

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

  data->state = EVD_WEBSOCKET_STATE_READING_PAYLOAD;
  if (data->payload_len == 0)
    return read_payload (data);

  return TRUE;
}

static gboolean
process_data (EvdWebsocketData *data)
{
  while (data->offset < data->buf_len &&
         data->state != EVD_WEBSOCKET_STATE_CLOSED)
    {
      switch (data->state)
        {
        case EVD_WEBSOCKET_STATE_IDLE:
          if (! read_header (data))
            return TRUE;
          break;

        case EVD_WEBSOCKET_STATE_READING_PAYLOAD_LEN:
          if (! read_payload_len (data))
            return TRUE;
          break;

        case EVD_WEBSOCKET_STATE_READING_MASKING_KEY:
          if (! read_masking_key (data))
            return TRUE;
          break;

        case EVD_WEBSOCKET_STATE_READING_PAYLOAD:
          if (! read_payload (data))
            return TRUE;
          break;
        }
    }

  return TRUE;
}

/* public methods */

void
evd_websocket08_handle_handshake_request (EvdWebService       *web_service,
                                          EvdHttpConnection   *conn,
                                          EvdHttpRequest      *request,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
  GSimpleAsyncResult *res;

  SoupMessageHeaders *req_headers;
  SoupMessageHeaders *res_headers = NULL;
  GError *error = NULL;

  const gchar *key;
  gchar *accept_key;

  res = g_simple_async_result_new (G_OBJECT (conn),
                                   callback,
                                   user_data,
                                   evd_websocket08_handle_handshake_request);

  req_headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  if (g_strcmp0 (soup_message_headers_get_one (req_headers, "Upgrade"),
                 "websocket") != 0 ||
      g_strcmp0 (soup_message_headers_get_one (req_headers, "Connection"),
                 "Upgrade") != 0)
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Invalid Websocket handshake request");
      goto finish;
    }

  key = soup_message_headers_get_one (req_headers, "Sec-WebSocket-Key");
  if (key == NULL)
    {
      g_set_error (&error,
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
  evd_http_connection_write_response_headers (conn,
                                              SOUP_HTTP_1_1,
                                              SOUP_STATUS_SWITCHING_PROTOCOLS,
                                              NULL,
                                              res_headers,
                                              &error);

 finish:

  if (error != NULL)
    {
      g_simple_async_result_take_error (res, error);
    }
  else
    {
      /* setup websocket data on connection */
      evd_websocket_common_setup_connection (conn,
                                             8,
                                             TRUE,
                                             process_data,
                                             send_close_frame,
                                             send_data_frame);
    }

  g_simple_async_result_complete_in_idle (res);
  g_object_unref (res);

  if (res_headers != NULL)
    soup_message_headers_free (res_headers);
}
