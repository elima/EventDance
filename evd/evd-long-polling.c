/*
 * evd-long-polling.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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

#include "evd-long-polling.h"
#include "evd-transport.h"

#include "evd-error.h"
#include "evd-http-connection.h"
#include "evd-peer-manager.h"

#define EVD_LONG_POLLING_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_LONG_POLLING, \
                                           EvdLongPollingPrivate))

#define PEER_ID_HEADER_NAME  "X-Org-EventDance-Peer-Id"

#define PEER_DATA_KEY       "org.eventdance.lib.LongPolling"
#define CONN_PEER_KEY_GET   PEER_DATA_KEY ".GET"
#define CONN_PEER_KEY_POST  PEER_DATA_KEY ".POST"

#define ACTION_HANDSHAKE "handshake"
#define ACTION_RECEIVE   "receive"
#define ACTION_SEND      "send"
#define ACTION_CLOSE     "close"

/* private data */
struct _EvdLongPollingPrivate
{
  EvdPeerManager *peer_manager;
};

typedef struct _EvdLongPollingPeerData EvdLongPollingPeerData;
struct _EvdLongPollingPeerData
{
  GQueue *conns;
};

static void     evd_long_polling_class_init          (EvdLongPollingClass *class);
static void     evd_long_polling_init                (EvdLongPolling *self);

static void     evd_long_polling_transport_iface_init (EvdTransportInterface *iface);

static void     evd_long_polling_finalize            (GObject *obj);
static void     evd_long_polling_dispose             (GObject *obj);

static void     evd_long_polling_request_handler     (EvdWebService     *web_service,
                                                      EvdHttpConnection *conn,
                                                      EvdHttpRequest    *request);

static gboolean evd_long_polling_remove              (EvdIoStreamGroup *io_stream_group,
                                                      GIOStream        *io_stream);

static gssize   evd_long_polling_send                (EvdTransport *transport,
                                                      EvdPeer       *peer,
                                                      const gchar   *buffer,
                                                      gsize          size,
                                                      GError       **error);

static gboolean evd_long_polling_actual_send         (EvdLongPolling     *self,
                                                      EvdPeer            *peer,
                                                      EvdHttpConnection *conn,
                                                      const gchar        *buffer,
                                                      gsize               size,
                                                      GError            **error);

static gboolean evd_long_polling_peer_is_connected   (EvdTransport *transport,
                                                      EvdPeer       *peer);

static void     evd_long_polling_peer_closed         (EvdTransport *transport,
                                                      EvdPeer      *peer,
                                                      gboolean      gracefully);

G_DEFINE_TYPE_WITH_CODE (EvdLongPolling, evd_long_polling, EVD_TYPE_WEB_SERVICE,
                         G_IMPLEMENT_INTERFACE (EVD_TYPE_TRANSPORT,
                                                evd_long_polling_transport_iface_init));

static void
evd_long_polling_class_init (EvdLongPollingClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdIoStreamGroupClass *io_stream_group_class =
    EVD_IO_STREAM_GROUP_CLASS (class);
  EvdWebServiceClass *web_service_class = EVD_WEB_SERVICE_CLASS (class);

  obj_class->dispose = evd_long_polling_dispose;
  obj_class->finalize = evd_long_polling_finalize;

  io_stream_group_class->remove = evd_long_polling_remove;

  web_service_class->request_handler = evd_long_polling_request_handler;

  g_type_class_add_private (obj_class, sizeof (EvdLongPollingPrivate));
}

static void
evd_long_polling_transport_iface_init (EvdTransportInterface *iface)
{
  iface->send = evd_long_polling_send;
  iface->peer_is_connected = evd_long_polling_peer_is_connected;
  iface->peer_closed = evd_long_polling_peer_closed;
}

static void
evd_long_polling_init (EvdLongPolling *self)
{
  EvdLongPollingPrivate *priv;

  priv = EVD_LONG_POLLING_GET_PRIVATE (self);
  self->priv = priv;

  priv->peer_manager = evd_peer_manager_get_default ();

  evd_service_set_io_stream_type (EVD_SERVICE (self), EVD_TYPE_HTTP_CONNECTION);
}

static void
evd_long_polling_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_long_polling_parent_class)->dispose (obj);
}

static void
evd_long_polling_finalize (GObject *obj)
{
  EvdLongPolling *self = EVD_LONG_POLLING (obj);

  g_object_unref (self->priv->peer_manager);

  G_OBJECT_CLASS (evd_long_polling_parent_class)->finalize (obj);
}

static EvdPeer *
evd_long_polling_create_new_peer (EvdLongPolling *self)
{
  EvdPeer *peer;
  EvdLongPollingPeerData *data;

  peer = EVD_TRANSPORT_GET_INTERFACE (self)->
    create_new_peer (EVD_TRANSPORT (self));

  data = g_new0 (EvdLongPollingPeerData, 1);
  data->conns = g_queue_new ();

  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, data);

  return peer;
}

static void
evd_long_polling_respond_with_cookies (EvdLongPolling    *self,
                                       EvdHttpConnection *conn,
                                       EvdPeer           *peer,
                                       EvdHttpRequest    *request)
{
  SoupMessageHeaders *headers;
  GError *error = NULL;
  const gchar *id;
  SoupHTTPVersion ver;

  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
  ver = evd_http_message_get_version (EVD_HTTP_MESSAGE (request));

  id = evd_peer_get_id (peer);
  soup_message_headers_replace (headers, PEER_ID_HEADER_NAME, id);

  if (! evd_http_connection_respond (conn,
                                     ver,
                                     200,
                                     NULL,
                                     headers,
                                     NULL,
                                     0,
                                     TRUE,
                                     NULL,
                                     &error))
    {
      g_debug ("error responding with cookies: %s", error->message);
      g_error_free (error);
    }

  soup_message_headers_free (headers);
}

static void
evd_long_polling_read_msg_header (const gchar *buf,
                                  gsize       *hdr_len,
                                  gsize       *msg_len,
                                  gboolean    *more_fragments)
{
  const gchar MORE_BIT = 0x80;

  gchar hdr;

  hdr = buf[0];

  if (more_fragments != NULL)
    *more_fragments = (hdr & MORE_BIT) > 0;
  hdr &= ~MORE_BIT;

  if (hdr <= 0x7F - 2)
    {
      if (hdr_len != NULL)
        *hdr_len = 1;

      if (msg_len != NULL)
        *msg_len = hdr;
    }
  else if (hdr == 0x7F - 1)
    {
      if (hdr_len != NULL)
        *hdr_len = 5;

      if (msg_len != NULL)
        sscanf (buf + 1, "%04x", (uint *) msg_len);
    }
  else
    {
      if (hdr_len != NULL)
        *hdr_len = 17;

      if (msg_len != NULL)
        sscanf (buf + 1, "%16x", (uint *) msg_len);
    }
}

static void
evd_long_polling_conn_on_content_read (GObject      *obj,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  EvdLongPolling *self = EVD_LONG_POLLING (user_data);
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);

  EvdPeer *peer;
  gchar *content;
  gssize size;
  GError *error = NULL;

  peer = g_object_get_data (G_OBJECT (conn), CONN_PEER_KEY_POST);

  if ( (content = evd_http_connection_read_all_content_finish (conn,
                                                               res,
                                                               &size,
                                                               &error)) != NULL)
    {
      if (size > 0)
        {
          EvdTransportInterface *iface;
          gint i;
          gsize hdr_len = 0;
          gsize msg_len = 0;

          iface = EVD_TRANSPORT_GET_INTERFACE (self);

          i = 0;
          while (i < size)
            {
              evd_long_polling_read_msg_header (content + i,
                                                &hdr_len,
                                                &msg_len,
                                                NULL);

              iface->receive (EVD_TRANSPORT (self),
                              peer,
                              content + i + hdr_len,
                              (gsize) msg_len);

              i += msg_len + hdr_len;
            }
        }

      g_free (content);
    }
  else
    {
      g_debug ("error reading content: %s", error->message);
      g_error_free (error);
    }

  evd_long_polling_actual_send (self,
                                peer,
                                conn,
                                NULL,
                                0,
                                NULL);
}

static gchar *
evd_long_polling_resolve_action (EvdLongPolling *self,
                                 EvdHttpRequest *request)
{
  SoupURI *uri;
  const gchar *path;
  gchar **tokens;
  gint i;
  gchar *action = NULL;

  uri = evd_http_request_get_uri (request);
  path = uri->path;

  tokens = g_strsplit (path, "/", 32);

  i = 0;
  while (tokens[i] != NULL)
    i++;

  action = g_strdup (tokens[i-1]);

  g_strfreev (tokens);

  return action;
}

static void
evd_long_polling_handshake (EvdLongPolling    *self,
                            EvdHttpConnection *conn,
                            EvdHttpRequest    *request)
{
  EvdPeer *peer;

  /* @TODO: validate handshake */

  peer = evd_long_polling_create_new_peer (self);
  evd_long_polling_respond_with_cookies (self, conn, peer, request);
}

static void
evd_long_polling_unbind_peer (gpointer  user_data,
                              GObject  *where_the_object_was)
{
  g_assert (EVD_IS_PEER (user_data));

  EvdPeer *peer = EVD_PEER (user_data);

  g_object_unref (peer);
}

static void
evd_long_polling_request_handler (EvdWebService     *web_service,
                                  EvdHttpConnection *conn,
                                  EvdHttpRequest    *request)
{
  EvdLongPolling *self = EVD_LONG_POLLING (web_service);
  gchar *action;

  action = evd_long_polling_resolve_action (self, request);

  /* handshake? */
  if (g_strcmp0 (action, ACTION_HANDSHAKE) == 0)
    {
      evd_long_polling_handshake (self, conn, request);
    }
  else
    {
      SoupMessageHeaders *headers;
      const gchar *peer_id = NULL;
      EvdPeer *peer;

      headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

      /* resolve peer object */
      peer_id = soup_message_headers_get_one (headers, PEER_ID_HEADER_NAME);
      if (peer_id == NULL ||
          (peer = evd_peer_manager_lookup_peer (self->priv->peer_manager,
                                                peer_id)) == NULL)
        {
          /* respond with 404 Not Found, forcing client to re-handshake */
          EVD_WEB_SERVICE_GET_CLASS (self)->respond (EVD_WEB_SERVICE (self),
                                                     conn,
                                                     404,
                                                     NULL,
                                                     NULL,
                                                     0,
                                                     NULL);
        }
      else
        {
          evd_peer_touch (peer);

          g_object_ref (peer);
          g_object_weak_ref (G_OBJECT (conn),
                             evd_long_polling_unbind_peer,
                             peer);

          /* receive? */
          if (g_strcmp0 (action, ACTION_RECEIVE) == 0)
            {
              EvdLongPollingPeerData *data;

              data =
                (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer),
                                                              PEER_DATA_KEY);

              g_object_ref (conn);
              g_object_set_data (G_OBJECT (conn), CONN_PEER_KEY_GET, peer);

              /* send Peer's backlogged frames */
              if (evd_peer_backlog_get_length (peer) > 0)
                evd_long_polling_actual_send (self,
                                              peer,
                                              conn,
                                              NULL,
                                              0,
                                              NULL);
              else
                g_queue_push_tail (data->conns, conn);
            }

          /* send? */
          else if (g_strcmp0 (action, ACTION_SEND) == 0)
            {
              g_object_set_data (G_OBJECT (conn), CONN_PEER_KEY_POST, peer);

              evd_http_connection_read_all_content_async (conn,
                                          NULL,
                                          evd_long_polling_conn_on_content_read,
                                          self);
            }

          /* close? */
          else if (g_strcmp0 (action, ACTION_CLOSE) == 0)
            {
              EVD_WEB_SERVICE_GET_CLASS (self)->respond (EVD_WEB_SERVICE (self),
                                                         conn,
                                                         200,
                                                         NULL,
                                                         NULL,
                                                         0,
                                                         NULL);

              evd_transport_close_peer (EVD_TRANSPORT (self),
                                        peer,
                                        TRUE,
                                        NULL);
            }
        }
    }

  g_free (action);
}

static gboolean
evd_long_polling_write_frame_delivery (EvdLongPolling     *self,
                                       EvdHttpConnection  *conn,
                                       const gchar        *buf,
                                       gsize               size,
                                       GError            **error)
{
  gboolean result = TRUE;
  guint8 hdr[17];
  gsize hdr_len = 1;
  gchar *len_st;

  if (size <= 0x7F - 2)
    {
      hdr_len = 1;
      hdr[0] = (guint8) size;
    }
  else if (size <= 0xFFFF)
    {
      hdr[0] = 0x7F - 1;
      hdr_len = 5;

      len_st = g_strdup_printf ("%04x", (uint) size);
      g_memmove (hdr + 1, len_st, 4);
      g_free (len_st);
    }
  else
    {
      hdr[0] = 0x7F;
      hdr_len = 17;

      len_st = g_strdup_printf ("%16x", (uint) size);
      g_memmove (hdr + 1, len_st, 16);
      g_free (len_st);
    }

  if (! evd_http_connection_write_content (conn,
                                           (gchar *) hdr,
                                           hdr_len,
                                           NULL,
                                           error) ||
      ! evd_http_connection_write_content (conn,
                                           buf,
                                           size,
                                           NULL,
                                           error))
    {
      result = FALSE;
    }

  return result;
}

static gboolean
evd_long_polling_peer_is_connected (EvdTransport *transport,
                                    EvdPeer       *peer)
{
  EvdLongPollingPeerData *data;

  data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer),
                                                       PEER_DATA_KEY);

  if (data == NULL || g_queue_get_length (data->conns) == 0)
    return FALSE;
  else
    return TRUE;
}

static gboolean
evd_long_polling_actual_send (EvdLongPolling     *self,
                              EvdPeer            *peer,
                              EvdHttpConnection  *conn,
                              const gchar        *buffer,
                              gsize               size,
                              GError            **error)
{
  SoupMessageHeaders *headers;
  gboolean result = TRUE;

  /* build and send HTTP headers */
  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
  soup_message_headers_replace (headers, "Content-type", "text/plain; charset=utf-8");
  soup_message_headers_replace (headers, "Connection", "close");

  if (evd_http_connection_write_response_headers (conn,
                                                  SOUP_HTTP_1_1,
                                                  SOUP_STATUS_OK,
                                                  "OK",
                                                  headers,
                                                  NULL,
                                                  error))
    {
      gchar *frame;
      gsize frame_size;

      /* send frames in peer's backlog first */
      while ( result &&
              (frame = evd_peer_backlog_pop_frame (peer, &frame_size)) != NULL)
        {
          if (! evd_long_polling_write_frame_delivery (self,
                                                       conn,
                                                       frame,
                                                       frame_size,
                                                       NULL))
            {
              evd_peer_backlog_unshift_frame (peer, frame, frame_size, NULL);

              result = FALSE;
            }

          g_free (frame);
        }

      /* then send the requested frame */
      if (result && buffer != NULL &&
          ! evd_long_polling_write_frame_delivery (self,
                                                   conn,
                                                   buffer,
                                                   size,
                                                   NULL))
        {
          result = FALSE;
        }

      /* flush connection's buffer, and shutdown connection after */
      evd_connection_flush_and_shutdown (EVD_CONNECTION (conn), NULL);
    }

  soup_message_headers_free (headers);

  return result;
}

static gssize
evd_long_polling_select_conn_and_send (EvdLongPolling  *self,
                                       EvdPeer         *peer,
                                       const gchar     *buffer,
                                       gsize            size,
                                       GError         **error)
{
  EvdLongPollingPeerData *data;
  EvdHttpConnection *conn;

  data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer),
                                                       PEER_DATA_KEY);
  if (data == NULL)
    {
      g_set_error (error,
                   EVD_ERROR,
                   EVD_ERROR_INVALID_DATA,
                   "Unable to associate peer with long-polling transport");

      return -1;
    }

  if (g_queue_get_length (data->conns) == 0)
    return 0;

  conn = EVD_HTTP_CONNECTION (g_queue_pop_head (data->conns));

  if (evd_long_polling_actual_send (self,
                                    peer,
                                    conn,
                                    buffer,
                                    size,
                                    error))
    return size;
  else
    return -1;
}

static gssize
evd_long_polling_send (EvdTransport *transport,
                       EvdPeer       *peer,
                       const gchar   *buffer,
                       gsize          size,
                       GError       **error)
{
  EvdLongPolling *self = EVD_LONG_POLLING (transport);

  return evd_long_polling_select_conn_and_send (self,
                                                peer,
                                                buffer,
                                                size,
                                                error);
}

static gboolean
evd_long_polling_remove (EvdIoStreamGroup *io_stream_group,
                         GIOStream        *io_stream)
{
  EvdConnection *conn = EVD_CONNECTION (io_stream);
  EvdPeer *peer;

  if (! EVD_IO_STREAM_GROUP_CLASS (evd_long_polling_parent_class)->
      remove (io_stream_group, io_stream))
    {
      return FALSE;
    }

  /* remove conn from Peer's list of conns */
  peer = g_object_get_data (G_OBJECT (conn), CONN_PEER_KEY_GET);
  if (peer != NULL)
    {
      EvdLongPollingPeerData *data;

      data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer),
                                                           PEER_DATA_KEY);
      if (data != NULL)
        {
          g_queue_remove (data->conns, conn);
          g_object_unref (conn);
        }
    }

  return TRUE;
}

static void
evd_long_polling_peer_closed (EvdTransport *transport,
                              EvdPeer      *peer,
                              gboolean      gracefully)
{
  EvdLongPollingPeerData *data;

  data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer),
                                                       PEER_DATA_KEY);

  while (g_queue_get_length (data->conns) > 0)
    {
      EvdHttpConnection *conn;

      conn = EVD_HTTP_CONNECTION (g_queue_pop_head (data->conns));

      g_object_set_data (G_OBJECT (conn), CONN_PEER_KEY_GET, NULL);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
      g_object_unref (conn);
    }
  g_queue_free (data->conns);

  g_free (data);

  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);
}

/* public methods */

EvdLongPolling *
evd_long_polling_new (void)
{
  EvdLongPolling *self;

  self = g_object_new (EVD_TYPE_LONG_POLLING, NULL);

  return self;
}
