/*
 * evd-websocket-server.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011-2012, Igalia S.L.
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

#include <libsoup/soup-headers.h>

#include "evd-websocket-server.h"

#include "evd-transport.h"
#include "evd-websocket-protocol.h"

#define EVD_WEBSOCKET_SERVER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                               EVD_TYPE_WEBSOCKET_SERVER, \
                                               EvdWebsocketServerPrivate))

#define CONN_DATA_KEY      "org.eventdance.lib.WebsocketServer.CONN_DATA"
#define PEER_DATA_KEY      "org.eventdance.lib.WebsocketServer.PEER_DATA"
#define HANDSHAKE_DATA_KEY "org.eventdance.lib.WebsocketServer.HANDSHAKE_DATA"

#define DEFAULT_STANDALONE TRUE

struct _EvdWebsocketServerPrivate
{
  gboolean standalone;

  EvdHttpConnection *peer_arg_conn;
  EvdHttpRequest *peer_arg_request;
};

typedef struct
{
  EvdHttpConnection *conn;
  EvdHttpRequest *request;
  gboolean is_new_peer;
} HandshakeData;

static void     evd_websocket_server_class_init           (EvdWebsocketServerClass *class);
static void     evd_websocket_server_init                 (EvdWebsocketServer *self);

static void     evd_websocket_server_transport_iface_init (EvdTransportInterface *iface);

static void     evd_websocket_server_request_handler      (EvdWebService     *web_service,
                                                           EvdHttpConnection *conn,
                                                           EvdHttpRequest    *request);

static gboolean evd_websocket_server_remove               (EvdIoStreamGroup *io_stream_group,
                                                           GIOStream        *io_stream);

static gboolean evd_websocket_server_send                 (EvdTransport    *transport,
                                                           EvdPeer         *peer,
                                                           const gchar     *buffer,
                                                           gsize            size,
                                                           EvdMessageType   type,
                                                           GError         **error);

static gboolean evd_websocket_server_peer_is_connected    (EvdTransport *transport,
                                                           EvdPeer      *peer);

static void     evd_websocket_server_peer_closed          (EvdTransport *transport,
                                                           EvdPeer      *peer,
                                                           gboolean      gracefully);

static gboolean accept_peer                               (EvdTransport *transport,
                                                           EvdPeer      *peer);
static gboolean reject_peer                               (EvdTransport *transport,
                                                           EvdPeer      *peer);

G_DEFINE_TYPE_WITH_CODE (EvdWebsocketServer, evd_websocket_server, EVD_TYPE_WEB_SERVICE,
                         G_IMPLEMENT_INTERFACE (EVD_TYPE_TRANSPORT,
                                                evd_websocket_server_transport_iface_init));

static void
evd_websocket_server_class_init (EvdWebsocketServerClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdIoStreamGroupClass *io_stream_group_class =
    EVD_IO_STREAM_GROUP_CLASS (class);
  EvdWebServiceClass *web_service_class = EVD_WEB_SERVICE_CLASS (class);

  io_stream_group_class->remove = evd_websocket_server_remove;

  web_service_class->request_handler = evd_websocket_server_request_handler;

  g_type_class_add_private (obj_class, sizeof (EvdWebsocketServerPrivate));
}

static void
evd_websocket_server_transport_iface_init (EvdTransportInterface *iface)
{
  iface->send = evd_websocket_server_send;
  iface->peer_is_connected = evd_websocket_server_peer_is_connected;
  iface->peer_closed = evd_websocket_server_peer_closed;
  iface->accept_peer = accept_peer;
  iface->reject_peer = reject_peer;
}

static void
evd_websocket_server_init (EvdWebsocketServer *self)
{
  EvdWebsocketServerPrivate *priv;

  priv = EVD_WEBSOCKET_SERVER_GET_PRIVATE (self);
  self->priv = priv;

  priv->standalone = DEFAULT_STANDALONE;

  evd_service_set_io_stream_type (EVD_SERVICE (self), EVD_TYPE_HTTP_CONNECTION);
}

static void
on_frame_received (EvdHttpConnection *conn,
                   const gchar       *frame,
                   gsize              frame_len,
                   gboolean           is_binary,
                   gpointer           user_data)
{
  EvdTransportInterface *iface;
  EvdPeer *peer;
  EvdTransport *transport = EVD_TRANSPORT (user_data);

  iface = EVD_TRANSPORT_GET_INTERFACE (transport);

  peer = EVD_PEER (g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY));
  if (peer == NULL || evd_peer_is_closed (peer))
    return;

  iface->receive (transport, peer, frame, frame_len);
}

static void
on_close_requested (EvdHttpConnection *conn,
                    gboolean           gracefully,
                    gpointer           user_data)
{
  EvdPeer *peer;
  EvdWebsocketServer *self = EVD_WEBSOCKET_SERVER (user_data);

  peer = EVD_PEER (g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY));
  if (peer == NULL)
    return;

  if (gracefully)
    {
      evd_transport_close_peer (EVD_TRANSPORT (self),
                                peer,
                                TRUE,
                                NULL);
    }
  else
    {
      g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);
    }
}

static void
on_websocket_connection_ready (EvdWebsocketServer *self,
                               EvdPeer            *peer,
                               EvdHttpConnection  *conn,
                               gboolean            is_new_peer)
{
  if (is_new_peer)
    {
      EvdTransportInterface *iface;
      EvdPeerManager *peer_manager;

      iface = EVD_TRANSPORT_GET_INTERFACE (self);

      peer_manager = iface->peer_manager;

      if (evd_peer_manager_lookup_peer (peer_manager,
                                        evd_peer_get_id (peer)) == NULL)
        {
          evd_peer_manager_add_peer (peer_manager, peer);

          /* notify new peer */
          iface->notify_new_peer (EVD_TRANSPORT (self), peer);
        }
    }

  g_object_set_data (G_OBJECT (conn), CONN_DATA_KEY, peer);

  g_object_ref (conn);
  g_object_set_data_full (G_OBJECT (peer),
                          PEER_DATA_KEY,
                          conn,
                          g_object_unref);

  g_object_ref (self);
  evd_websocket_protocol_bind (conn,
                               on_frame_received,
                               on_close_requested,
                               self,
                               g_object_unref);

  /* send frames from Peer's backlog */
  while (evd_peer_backlog_get_length (peer) > 0)
    {
      gsize size;
      gchar *frame;
      EvdMessageType type;
      GError *error = NULL;

      frame = evd_peer_pop_message (peer, &size, &type);

      if (! evd_websocket_server_send (EVD_TRANSPORT (self),
                                       peer,
                                       frame,
                                       size,
                                       type,
                                       &error))
        {
          g_print ("Error, failed to send frame from peer's backlog: %s\n",
                   error->message);
          g_error_free (error);

          evd_peer_unshift_message (peer, frame, size, type, NULL);

          break;
        }

      g_free (frame);
    }
}

static gboolean
accept_peer (EvdTransport *transport, EvdPeer *peer)
{
  EvdWebsocketServer *self = EVD_WEBSOCKET_SERVER (transport);
  HandshakeData *data;

  data = g_object_get_data (G_OBJECT (peer), HANDSHAKE_DATA_KEY);
  if (data == NULL)
    return FALSE;

  on_websocket_connection_ready (self, peer, data->conn, data->is_new_peer);

  g_object_set_data (G_OBJECT (peer), HANDSHAKE_DATA_KEY, NULL);
  g_object_unref (peer);

  return FALSE;
}

static gboolean
reject_peer (EvdTransport *transport, EvdPeer *peer)
{
  EvdWebsocketServer *self = EVD_WEBSOCKET_SERVER (transport);
  HandshakeData *data;

  data = g_object_get_data (G_OBJECT (peer), HANDSHAKE_DATA_KEY);
  if (data == NULL)
    return FALSE;

  evd_web_service_respond (EVD_WEB_SERVICE (self),
                           data->conn,
                           SOUP_STATUS_FORBIDDEN,
                           NULL,
                           NULL,
                           0,
                           NULL);

  g_object_set_data (G_OBJECT (peer), HANDSHAKE_DATA_KEY, NULL);
  g_object_unref (peer);

  return FALSE;
}

static void
free_handshake_data (gpointer _data)
{
  HandshakeData *data = _data;

  g_object_unref (data->conn);
  g_object_unref (data->request);

  g_slice_free (HandshakeData, data);
}

static void
evd_websocket_server_request_handler (EvdWebService     *web_service,
                                      EvdHttpConnection *conn,
                                      EvdHttpRequest    *request)
{
  EvdWebsocketServer *self = EVD_WEBSOCKET_SERVER (web_service);
  EvdPeer *peer = NULL;
  SoupURI *uri;
  guint validate_result;
  EvdTransportInterface *iface;
  GError *error = NULL;
  gboolean is_new_peer = FALSE;

  uri = evd_http_request_get_uri (request);

  /* resolve peer */
  peer = evd_transport_lookup_peer (EVD_TRANSPORT (self), uri->query);
  if (peer == NULL)
    {
      if (! self->priv->standalone)
        {
          evd_web_service_respond (web_service,
                                   conn,
                                   SOUP_STATUS_NOT_FOUND,
                                   NULL,
                                   NULL,
                                   0,
                                   NULL);
          return;
        }
      else
        {
          peer = g_object_new (EVD_TYPE_PEER, "transport", self, NULL);
          is_new_peer = TRUE;
        }
    }
  else
    {
      evd_peer_touch (peer);
      g_object_ref (peer);
    }

  /* let WebSocket protocol handle request */
  if (! evd_websocket_protocol_handle_handshake_request (conn,
                                                         request,
                                                         &error))
    {
      g_print ("%s\n", error->message);
      g_error_free (error);

      evd_web_service_respond (web_service,
                               conn,
                               SOUP_STATUS_BAD_REQUEST,
                               NULL,
                               NULL,
                               0,
                               NULL);
      goto out;
    }

  /* validate peer */
  iface = EVD_TRANSPORT_GET_INTERFACE (self);

  self->priv->peer_arg_conn = conn;
  self->priv->peer_arg_request = request;

  validate_result = iface->notify_validate_peer (EVD_TRANSPORT (self), peer);

  self->priv->peer_arg_conn = NULL;
  self->priv->peer_arg_request = NULL;

  if (validate_result == EVD_VALIDATE_ACCEPT)
    {
      /* peer accepted */
      if (! g_io_stream_is_closed (G_IO_STREAM (conn)))
        {
          on_websocket_connection_ready (self, peer, conn, is_new_peer);
        }
    }
  else if (validate_result == EVD_VALIDATE_REJECT)
    {
      /* peer rejected */
      evd_web_service_respond (web_service,
                               conn,
                               SOUP_STATUS_FORBIDDEN,
                               NULL,
                               NULL,
                               0,
                               NULL);
    }
  else
    {
      /* validation pending */

      HandshakeData *data;

      data = g_slice_new (HandshakeData);
      data->is_new_peer = is_new_peer;
      data->conn = g_object_ref (conn);
      data->request = g_object_ref (request);

      g_object_ref (peer);
      g_object_set_data_full (G_OBJECT (peer),
                              HANDSHAKE_DATA_KEY,
                              data,
                              free_handshake_data);
    }

 out:
  g_object_unref (peer);
}

static gboolean
evd_websocket_server_peer_is_connected (EvdTransport *transport, EvdPeer *peer)
{
  EvdConnection *conn;

  conn = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);

  return (conn != NULL && ! g_io_stream_is_closed (G_IO_STREAM (conn)));
}

static gboolean
evd_websocket_server_send (EvdTransport    *transport,
                           EvdPeer         *peer,
                           const gchar     *buffer,
                           gsize            size,
                           EvdMessageType   type,
                           GError         **error)
{
  EvdHttpConnection *conn;

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
      return evd_websocket_protocol_send (conn, buffer, size, type, error);
    }
}

static gboolean
evd_websocket_server_remove (EvdIoStreamGroup *io_stream_group,
                             GIOStream        *io_stream)
{
  EvdPeer *peer;

  if (! EVD_IO_STREAM_GROUP_CLASS (evd_websocket_server_parent_class)->
      remove (io_stream_group, io_stream))
    {
      return FALSE;
    }

  evd_websocket_protocol_unbind (EVD_HTTP_CONNECTION (io_stream));

  peer = g_object_get_data (G_OBJECT (io_stream), CONN_DATA_KEY);
  if (peer != NULL)
    g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);

  return TRUE;
}

static void
evd_websocket_server_peer_closed (EvdTransport *transport,
                                  EvdPeer      *peer,
                                  gboolean      gracefully)
{
  EvdHttpConnection *conn;

  conn = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);

  if (conn == NULL)
    return;

  if (! g_io_stream_is_closed (G_IO_STREAM (conn)))
    {
      GError *error = NULL;
      guint16 code;

      /* @TODO: Choose a proper closing code */
      code = gracefully ?
        EVD_WEBSOCKET_CLOSE_NORMAL :
        EVD_WEBSOCKET_CLOSE_ABNORMAL;

      if (! evd_websocket_protocol_close (conn, code, NULL, &error))
        {
          /* @TODO: do proper error logging */
          g_print ("Error closing websocket connection: %s\n", error->message);
          g_error_free (error);
        }
    }

  g_object_set_data (G_OBJECT (conn), CONN_DATA_KEY, NULL);
  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);
}

/* public methods */

EvdWebsocketServer *
evd_websocket_server_new (void)
{
  return g_object_new (EVD_TYPE_WEBSOCKET_SERVER, NULL);
}

void
evd_websocket_server_set_standalone (EvdWebsocketServer *self,
                                     gboolean            standalone)
{
  g_return_if_fail (EVD_IS_WEBSOCKET_SERVER (self));

  self->priv->standalone = standalone;
}

gboolean
evd_websocket_server_get_standalone (EvdWebsocketServer *self)
{
  g_return_val_if_fail (EVD_IS_WEBSOCKET_SERVER (self), FALSE);

  return self->priv->standalone;
}

/**
 * evd_websocket_server_get_validate_peer_arguments:
 * @conn: (out) (allow-none) (transfer none):
 * @request: (out) (allow-none) (transfer none):
 *
 **/
void
evd_websocket_server_get_validate_peer_arguments (EvdWebsocketServer  *self,
                                                  EvdPeer             *peer,
                                                  EvdHttpConnection  **conn,
                                                  EvdHttpRequest     **request)
{
  g_return_if_fail (EVD_IS_WEBSOCKET_SERVER (self));

  if (conn != NULL)
    *conn = self->priv->peer_arg_conn;

  if (request != NULL)
    *request = self->priv->peer_arg_request;
}
