/*
 * evd-websocket-server.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011, 2012 Igalia S.L.
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
#include "evd-http-connection.h"
#include "evd-websocket-common.h"
#include "evd-websocket00.h"

#define EVD_WEBSOCKET_SERVER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                               EVD_TYPE_WEBSOCKET_SERVER, \
                                               EvdWebsocketServerPrivate))

#define CONN_DATA_KEY "org.eventdance.lib.WebsocketServer.CONN_DATA"
#define PEER_DATA_KEY "org.eventdance.lib.WebsocketServer.PEER_DATA"

#define DEFAULT_STANDALONE FALSE

struct _EvdWebsocketServerPrivate
{
  gboolean standalone;
};

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
  EvdTransportInterface *iface;
  EvdPeer *peer;
  EvdTransport *transport = EVD_TRANSPORT (user_data);

  iface = EVD_TRANSPORT_GET_INTERFACE (transport);

  peer = EVD_PEER (g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY));
  if (peer == NULL || evd_peer_is_closed (peer))
    return;

  iface->notify_peer_closed (transport, peer, gracefully);
}

static void
on_handshake_completed (GObject      *obj,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  EvdHttpConnection *conn;
  GError *error = NULL;

  conn = evd_websocket_common_handshake_finish (res, &error);
  if (conn == NULL)
    {
      /* @TODO: do proper error logging */
      g_print ("Error: Websocket handshake failed: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
      EvdWebsocketServer *self;

      self = EVD_WEBSOCKET_SERVER (user_data);

      g_object_ref (conn);
      g_object_ref (self);
      evd_websocket_common_bind (conn,
                                 on_frame_received,
                                 on_close_requested,
                                 self,
                                 g_object_unref);
    }

  g_object_unref (conn);
}

static void
evd_websocket_server_request_handler (EvdWebService     *web_service,
                                      EvdHttpConnection *conn,
                                      EvdHttpRequest    *request)
{
  EvdWebsocketServer *self = EVD_WEBSOCKET_SERVER (web_service);
  EvdPeer *peer = NULL;
  SoupURI *uri;
  guint8 version;

  uri = evd_http_request_get_uri (request);

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
          peer = evd_transport_create_new_peer (EVD_TRANSPORT (self));
        }
    }
  else
    {
      evd_peer_touch (peer);
    }

  g_object_ref (peer);
  g_object_set_data_full (G_OBJECT (conn),
                          CONN_DATA_KEY,
                          peer,
                          g_object_unref);

  g_object_ref (conn);
  g_object_set_data_full (G_OBJECT (peer),
                          PEER_DATA_KEY,
                          conn,
                          g_object_unref);

  version = evd_websocket_common_get_version_from_request (request);

  g_object_ref (conn);

  if (version == 0)
    {
      evd_websocket00_handle_handshake_request (EVD_WEB_SERVICE (self),
                                                conn,
                                                request,
                                                on_handshake_completed,
                                                self);
    }
}

static gboolean
evd_websocket_server_peer_is_connected (EvdTransport *transport, EvdPeer *peer)
{
  EvdConnection *conn;

  conn = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);

  return (conn != NULL && ! g_io_stream_is_closed (G_IO_STREAM (conn)));
}

static gboolean
evd_websocket_server_send (EvdTransport *transport,
                           EvdPeer       *peer,
                           const gchar   *buffer,
                           gsize          size,
                           GError       **error)
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
      guint8 version;

      version = evd_websocket_common_get_version (conn);

      if (version == 0)
        {
          return evd_websocket00_send (conn, buffer, size, FALSE, error);
        }
      else
        {
          g_assert_not_reached ();
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_SUPPORTED,
                               "Unsupported websocket protocol version");
          return FALSE;
        }
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

  peer = g_object_get_data (G_OBJECT (io_stream), CONN_DATA_KEY);
  if (peer != NULL)
    {
      g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);
      g_object_set_data (G_OBJECT (io_stream), CONN_DATA_KEY, NULL);
    }

  g_object_unref (io_stream);

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
      guint8 version;

      version = evd_websocket_common_get_version (conn);

      if (version == 0)
        evd_websocket00_close (conn, 0, NULL, NULL);
      else
        g_assert_not_reached ();
    }

  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);
  g_object_set_data (G_OBJECT (conn), CONN_DATA_KEY, NULL);
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
