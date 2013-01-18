/*
 * evd-websocket-client.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2012-2013 Igalia S.L.
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

#include "evd-websocket-client.h"

#include "evd-transport.h"
#include "evd-connection-pool.h"
#include "evd-websocket-protocol.h"

#define EVD_WEBSOCKET_CLIENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                               EVD_TYPE_WEBSOCKET_CLIENT, \
                                               EvdWebsocketClientPrivate))

#define PEER_DATA_KEY "org.eventdance.lib.WebsocketClient.PEER_DATA"
#define CONN_DATA_KEY "org.eventdance.lib.WebsocketClient.CONN_DATA"

#define DEFAULT_STANDALONE TRUE

struct _EvdWebsocketClientPrivate
{
  gboolean standalone;

  EvdHttpConnection *peer_arg_conn;
  SoupMessageHeaders *peer_arg_headers;
};

typedef struct
{
  EvdWebsocketClient *self;
  gchar *address;
  EvdConnectionPool *pool;
  EvdPeer *peer;
  GSimpleAsyncResult *async_result;
  GCancellable *cancellable;
  gchar *handshake_key;
  SoupMessageHeaders *res_headers;
  gboolean validating_peer;
} ConnectionData;

static void     evd_websocket_client_class_init           (EvdWebsocketClientClass *class);
static void     evd_websocket_client_init                 (EvdWebsocketClient *self);
static void     evd_websocket_client_transport_iface_init (EvdTransportInterface *iface);
static void     evd_websocket_client_dispose              (GObject *obj);
static void     evd_websocket_client_finalize             (GObject *obj);

static gboolean io_stream_group_add                       (EvdIoStreamGroup *io_stream_group,
                                                           GIOStream        *io_stream);
static gboolean io_stream_group_remove                    (EvdIoStreamGroup *io_stream_group,
                                                           GIOStream        *io_stream);

static void     start_opening_handshake                   (EvdWebsocketClient *self,
                                                           EvdHttpConnection  *conn);

static void     resolve_peer_and_validate                 (EvdWebsocketClient *self,
                                                           EvdHttpConnection  *conn);

static void     on_connection_closed                      (EvdConnection *conn,
                                                           gpointer       user_data);

static gboolean evd_websocket_client_send                 (EvdTransport    *transport,
                                                           EvdPeer         *peer,
                                                           const gchar     *buffer,
                                                           gsize            size,
                                                           EvdMessageType   type,
                                                           GError         **error);

static gboolean peer_is_connected                         (EvdTransport *transport,
                                                           EvdPeer      *peer);

static void     peer_closed                               (EvdTransport *transport,
                                                           EvdPeer      *peer,
                                                           gboolean      gracefully);

static gboolean transport_accept_peer                     (EvdTransport *transport,
                                                           EvdPeer      *peer);
static gboolean transport_reject_peer                     (EvdTransport *transport,
                                                           EvdPeer      *peer);

static void     transport_open                            (EvdTransport       *self,
                                                           const gchar        *address,
                                                           GSimpleAsyncResult *async_result,
                                                           GCancellable       *cancellable);

static void     retry_connection                          (ConnectionData *data);

G_DEFINE_TYPE_WITH_CODE (EvdWebsocketClient, evd_websocket_client, EVD_TYPE_IO_STREAM_GROUP,
                         G_IMPLEMENT_INTERFACE (EVD_TYPE_TRANSPORT,
                                                evd_websocket_client_transport_iface_init));

static void
evd_websocket_client_class_init (EvdWebsocketClientClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdIoStreamGroupClass *io_stream_group_class
    = EVD_IO_STREAM_GROUP_CLASS (class);

  obj_class->dispose = evd_websocket_client_dispose;
  obj_class->finalize = evd_websocket_client_finalize;

  io_stream_group_class->add = io_stream_group_add;
  io_stream_group_class->remove = io_stream_group_remove;

  g_type_class_add_private (obj_class, sizeof (EvdWebsocketClientPrivate));
}

static void
evd_websocket_client_transport_iface_init (EvdTransportInterface *iface)
{
  iface->send = evd_websocket_client_send;
  iface->peer_is_connected = peer_is_connected;
  iface->peer_closed = peer_closed;
  iface->accept_peer = transport_accept_peer;
  iface->reject_peer = transport_reject_peer;
  iface->open = transport_open;
}

static void
evd_websocket_client_init (EvdWebsocketClient *self)
{
  EvdWebsocketClientPrivate *priv;

  priv = EVD_WEBSOCKET_CLIENT_GET_PRIVATE (self);
  self->priv = priv;

  priv->standalone = DEFAULT_STANDALONE;
}

static void
evd_websocket_client_dispose (GObject *obj)
{
  /*  EvdWebsocketClient *self = EVD_WEBSOCKET_CLIENT (obj); */

  G_OBJECT_CLASS (evd_websocket_client_parent_class)->dispose (obj);
}

static void
evd_websocket_client_finalize (GObject *obj)
{
  /*  EvdWebsocketClient *self = EVD_WEBSOCKET_CLIENT (obj); */

  G_OBJECT_CLASS (evd_websocket_client_parent_class)->finalize (obj);
}

static gboolean
io_stream_group_add (EvdIoStreamGroup *io_stream_group,
                     GIOStream        *io_stream)
{
  EvdWebsocketClient *self = EVD_WEBSOCKET_CLIENT (io_stream_group);
  EvdHttpConnection *conn;
  EvdWebsocketState state;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (io_stream), FALSE);

  conn = EVD_HTTP_CONNECTION (io_stream);

  if (! EVD_IO_STREAM_GROUP_CLASS
      (evd_websocket_client_parent_class)->add (io_stream_group, io_stream))
    {
      return FALSE;
    }

  g_signal_connect (io_stream,
                    "close",
                    G_CALLBACK (on_connection_closed),
                    io_stream_group);

  /* check state of the connection and act accordingly */
  state = evd_websocket_protocol_get_state (conn);

  if (state == EVD_WEBSOCKET_STATE_NONE)
    start_opening_handshake (self, conn);
  else
    resolve_peer_and_validate (self, conn);

  return TRUE;
}

static gboolean
io_stream_group_remove (EvdIoStreamGroup *io_stream_group,
                        GIOStream        *io_stream)
{
  ConnectionData *conn_data;

  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (io_stream), FALSE);

  if (! EVD_IO_STREAM_GROUP_CLASS
      (evd_websocket_client_parent_class)->remove (io_stream_group, io_stream))
    {
      return FALSE;
    }

  evd_websocket_protocol_unbind (EVD_HTTP_CONNECTION (io_stream));

  g_signal_handlers_disconnect_by_func (io_stream,
                                        on_connection_closed,
                                        io_stream_group);

  /* unlink peer and connection */
  conn_data = g_object_get_data (G_OBJECT (io_stream), CONN_DATA_KEY);
  if (conn_data != NULL)
    {
      EvdPeer *peer;

      peer = conn_data->peer;
      if (peer != NULL)
        {
          g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);
          conn_data->peer = NULL;
        }
    }

  return TRUE;
}

static void
on_frame_received (EvdHttpConnection *conn,
                   const gchar       *frame,
                   gsize              frame_len,
                   gboolean           is_binary,
                   gpointer           user_data)
{
  EvdTransportInterface *iface;
  EvdWebsocketClient *self = EVD_WEBSOCKET_CLIENT (user_data);
  EvdTransport *transport = EVD_TRANSPORT (self);
  ConnectionData *conn_data;

  conn_data = g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY);

  g_assert (conn_data->peer != NULL);

  evd_peer_touch (conn_data->peer);

  iface = EVD_TRANSPORT_GET_INTERFACE (transport);

  iface->receive (transport, conn_data->peer, frame, frame_len);
}

static void
on_connection_closed (EvdConnection *conn, gpointer user_data)
{
  EvdWebsocketClient *self = EVD_WEBSOCKET_CLIENT (user_data);

  evd_io_stream_group_remove (EVD_IO_STREAM_GROUP (self), G_IO_STREAM (conn));
}

static void
on_close_requested (EvdHttpConnection *conn,
                    gboolean           gracefully,
                    gpointer           user_data)
{
  EvdWebsocketClient *self = EVD_WEBSOCKET_CLIENT (user_data);
  ConnectionData *conn_data;

  conn_data = g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY);

  if (conn_data->peer != NULL)
    {
      if (gracefully)
        {
          evd_transport_close_peer (EVD_TRANSPORT (self),
                                    conn_data->peer,
                                    TRUE,
                                    NULL);
        }
      else
        {
          /* @TODO: retry */
          retry_connection (conn_data);
        }
    }

  g_object_unref (conn);
}

static gboolean
peer_is_connected (EvdTransport *transport, EvdPeer *peer)
{
  gpointer peer_data;
  EvdHttpConnection *conn;
  EvdWebsocketState state;

  peer_data = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);

  if (peer_data == NULL || ! EVD_IS_HTTP_CONNECTION (peer_data))
    return FALSE;

  conn = EVD_HTTP_CONNECTION (peer_data);
  state = evd_websocket_protocol_get_state (conn);

  return state == EVD_WEBSOCKET_STATE_OPENING ||
    state == EVD_WEBSOCKET_STATE_OPENED ||
    state == EVD_WEBSOCKET_STATE_CLOSING;
}

static gboolean
evd_websocket_client_send (EvdTransport    *transport,
                           EvdPeer         *peer,
                           const gchar     *buffer,
                           gsize            size,
                           EvdMessageType   type,
                           GError         **error)
{
  gpointer peer_data;

  peer_data = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);
  if (peer_data == NULL || ! EVD_IS_HTTP_CONNECTION (peer_data))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Peer has no WebSocket connection associated");
      return FALSE;
    }

  return evd_websocket_protocol_send (EVD_HTTP_CONNECTION (peer_data),
                                      buffer,
                                      size,
                                      type,
                                      error);
}

static void
peer_closed (EvdTransport *transport,
             EvdPeer      *peer,
             gboolean      gracefully)
{
  gpointer peer_data;
  EvdHttpConnection *conn;
  ConnectionData *conn_data;

  peer_data = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);
  if (peer_data == NULL)
    return;

  conn = EVD_HTTP_CONNECTION (peer_data);

  if (! g_io_stream_is_closed (G_IO_STREAM (conn)))
    {
      GError *error = NULL;
      guint16 code;

      /* @TODO: choose a proper closing code */
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

  conn_data = g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY);
  conn_data->peer = NULL;

  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);
}

static void
on_websocket_connection_ready (EvdWebsocketClient *self,
                               EvdHttpConnection  *conn,
                               ConnectionData     *conn_data)
{
  EvdTransportInterface *iface;

  g_assert (conn_data->peer != NULL);

  /* notify new peer */
  iface = EVD_TRANSPORT_GET_INTERFACE (self);

  evd_peer_manager_add_peer (iface->peer_manager, conn_data->peer);
  g_object_unref (conn_data->peer);

  iface->notify_new_peer (EVD_TRANSPORT (self), conn_data->peer);

  g_object_ref (self);
  evd_websocket_protocol_bind (conn,
                               on_frame_received,
                               on_close_requested,
                               self,
                               g_object_unref);
}

static gboolean
transport_accept_peer (EvdTransport *transport, EvdPeer *peer)
{
  EvdWebsocketClient *self = EVD_WEBSOCKET_CLIENT (transport);
  ConnectionData *conn_data;
  EvdHttpConnection *conn;

  conn = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);
  if (conn == NULL)
    return FALSE;

  conn_data = g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY);
  if (conn_data == NULL)
    return FALSE;

  if (! conn_data->validating_peer)
    return FALSE;

  conn_data->validating_peer = FALSE;

  on_websocket_connection_ready (self, conn, conn_data);

  return TRUE;
}

static gboolean
transport_reject_peer (EvdTransport *transport, EvdPeer *peer)
{
  ConnectionData *conn_data;
  EvdHttpConnection *conn;

  conn = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);
  if (conn == NULL)
    return FALSE;

  conn_data = g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY);
  if (conn_data == NULL)
    return FALSE;

  if (! conn_data->validating_peer)
    return FALSE;

  conn_data->validating_peer = FALSE;

  g_object_unref (conn_data->peer);
  conn_data->peer = NULL;

  evd_websocket_protocol_close (conn,
                                EVD_WEBSOCKET_CLOSE_POLICY_VIOLATION,
                                "Peer rejected",
                                NULL);

  return TRUE;
}

static void
free_connection_data (ConnectionData *data)
{
  g_object_unref (data->self);
  if (data->cancellable != NULL)
    g_object_unref (data->cancellable);
  g_object_unref (data->pool);

  g_free (data->address);
  g_free (data->handshake_key);

  if (data->res_headers != NULL)
    soup_message_headers_free (data->res_headers);

  g_slice_free (ConnectionData, data);
}

static void
resolve_peer_and_validate (EvdWebsocketClient *self, EvdHttpConnection *conn)
{
  ConnectionData *conn_data;
  EvdTransportInterface *iface;
  guint validate_result;

  conn_data = g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY);
  g_assert (conn_data != NULL);

  if (conn_data->peer == NULL)
    conn_data->peer = g_object_new (EVD_TYPE_PEER, "transport", self, NULL);
  else
    g_object_ref (conn_data->peer);

  g_object_ref (conn);
  g_object_set_data_full (G_OBJECT (conn_data->peer),
                          PEER_DATA_KEY,
                          conn,
                          g_object_unref);

  /* validate peer */
  iface = EVD_TRANSPORT_GET_INTERFACE (self);

  self->priv->peer_arg_conn = conn;
  self->priv->peer_arg_headers = conn_data->res_headers;

  validate_result = iface->notify_validate_peer (EVD_TRANSPORT (self), conn_data->peer);

  self->priv->peer_arg_conn = NULL;
  self->priv->peer_arg_headers = NULL;

  if (validate_result == EVD_VALIDATE_ACCEPT)
    {
      /* peer accepted */
      on_websocket_connection_ready (self, conn, conn_data);
    }
  else if (validate_result == EVD_VALIDATE_REJECT)
    {
      /* peer rejected */
      g_object_unref (conn_data->peer);
      conn_data->peer = NULL;

      evd_websocket_protocol_close (conn,
                                    EVD_WEBSOCKET_CLOSE_POLICY_VIOLATION,
                                    "Peer rejected",
                                    NULL);
    }
  else
    {
      conn_data->validating_peer = TRUE;
    }
}

static void
on_handshake_response (GObject      *obj,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  ConnectionData *conn_data = user_data;
  GError *error = NULL;
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);

  SoupMessageHeaders *res_headers = NULL;
  SoupHTTPVersion http_version;
  guint status_code;
  gchar *reason;

  res_headers = evd_http_connection_read_response_headers_finish (conn,
                                                                  res,
                                                                  &http_version,
                                                                  &status_code,
                                                                  &reason,
                                                                  &error);
  if (res_headers == NULL)
    {
      goto out;
    }

  /* validate handshake response */
  if (! evd_websocket_protocol_handle_handshake_response (conn,
                                                          http_version,
                                                          status_code,
                                                          res_headers,
                                                          conn_data->handshake_key,
                                                          &error))
    {
      goto out;
    }

  /* handshake succeeded */
  conn_data->res_headers = res_headers;
  resolve_peer_and_validate (conn_data->self, conn);

 out:
  if (conn_data->async_result != NULL)
    {
      if (error != NULL)
        g_simple_async_result_take_error (conn_data->async_result, error);

      g_simple_async_result_complete (conn_data->async_result);
      g_object_unref (conn_data->async_result);
      conn_data->async_result = NULL;
    }

  g_free (reason);

  g_free (conn_data->handshake_key);
  conn_data->handshake_key = NULL;
}

static void
retry_connection (ConnectionData *data)
{
  /* @TODO: retry websocket connection */
  g_print ("Retry websocket connection\n");
}

static void
on_handshake_sent (GObject      *obj,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  ConnectionData *data = user_data;
  GError *error = NULL;
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);

  if (! evd_http_connection_write_request_headers_finish (conn,
                                                          res,
                                                          &error))
    {
      /* @TODO: do proper debugging */
      g_print ("WebSocket connection error: %s\n", error->message);
      g_error_free (error);

      retry_connection (data);
      return;
    }

  /* read response headers */
  evd_http_connection_read_response_headers (conn,
                                             data->cancellable,
                                             on_handshake_response,
                                             data);
}

static void
start_opening_handshake (EvdWebsocketClient *self, EvdHttpConnection *conn)
{
  EvdHttpRequest *request;
  ConnectionData *data;

  data = g_object_get_data (G_OBJECT (conn), CONN_DATA_KEY);
  g_assert (data != NULL);

  request =
    evd_websocket_protocol_create_handshake_request (data->address,
                                                     NULL,
                                                     NULL,
                                                     &data->handshake_key);

  evd_http_connection_write_request_headers (conn,
                                             request,
                                             data->cancellable,
                                             on_handshake_sent,
                                             data);
  g_object_unref (request);
}

static void
on_connection (GObject      *obj,
               GAsyncResult *res,
               gpointer      user_data)
{
  ConnectionData *data = user_data;
  GError *error = NULL;
  EvdHttpConnection *conn;

  conn = EVD_HTTP_CONNECTION (evd_connection_pool_get_connection_finish
                              (EVD_CONNECTION_POOL (obj),
                               res,
                               &error));
  if (conn == NULL)
    {
      g_print ("Websocket connection failed: %s\n", error->message);
      g_error_free (error);

      retry_connection (data);
      return;
    }

  /* got a connection */
  g_object_set_data_full (G_OBJECT (conn),
                          CONN_DATA_KEY,
                          data,
                          (GDestroyNotify) free_connection_data);

  /* add it to my group */
  evd_io_stream_group_add (EVD_IO_STREAM_GROUP (data->self), G_IO_STREAM (conn));
}

static void
get_connection (EvdConnectionPool *pool,
                GCancellable      *cancellable,
                gpointer           user_data)
{
  evd_connection_pool_get_connection (pool,
                                      cancellable,
                                      on_connection,
                                      user_data);
}

static void
transport_open (EvdTransport       *transport,
                const gchar        *address,
                GSimpleAsyncResult *async_result,
                GCancellable       *cancellable)
{
  SoupURI *uri;
  EvdWebsocketClient *self = EVD_WEBSOCKET_CLIENT (transport);
  gchar *addr;
  ConnectionData *data;

  uri = soup_uri_new (address);
  if (uri == NULL)
    {
      g_simple_async_result_set_error (async_result,
                                       G_IO_ERROR,
                                       G_IO_ERROR_INVALID_ARGUMENT,
                                       "Websocket URI is invalid");
      g_simple_async_result_complete_in_idle (async_result);
      g_object_unref (async_result);

      return;
    }

  /* validate URI scheme */
  if (g_strcmp0 (uri->scheme, "ws") != 0 && g_strcmp0 (uri->scheme, "wss") != 0)
    {
      g_simple_async_result_set_error (async_result,
                                       G_IO_ERROR,
                                       G_IO_ERROR_INVALID_ARGUMENT,
                                       "Websocket URI scheme is invalid");
      g_simple_async_result_complete_in_idle (async_result);
      g_object_unref (async_result);

      goto out;
    }

  data = g_slice_new0 (ConnectionData);

  data->self = g_object_ref (self);
  data->address = g_strdup (address);
  data->async_result = async_result;
  if (data->cancellable)
    data->cancellable = g_object_ref (cancellable);

  /* connection pool */
  addr = g_strdup_printf ("%s:%d",
                          soup_uri_get_host (uri),
                          soup_uri_get_port (uri));

  data->pool = evd_connection_pool_new (addr, EVD_TYPE_HTTP_CONNECTION);

  g_free (addr);

  /* get a connection */
  get_connection (data->pool, cancellable, data);

 out:
  soup_uri_free (uri);
}

/* public methods */

EvdWebsocketClient *
evd_websocket_client_new (void)
{
  return g_object_new (EVD_TYPE_WEBSOCKET_CLIENT, NULL);
}

/**
 * evd_websocket_client_get_validate_peer_arguments:
 * @conn: (out) (allow-none) (transfer none):
 * @response_headers: (out) (allow-none) (transfer none):
 *
 **/
void
evd_websocket_client_get_validate_peer_arguments (EvdWebsocketClient  *self,
                                          EvdPeer             *peer,
                                          EvdHttpConnection  **conn,
                                          SoupMessageHeaders **response_headers)
{
  g_return_if_fail (EVD_IS_WEBSOCKET_CLIENT (self));

  if (conn != NULL)
    *conn = self->priv->peer_arg_conn;

  if (response_headers != NULL)
    *response_headers = self->priv->peer_arg_headers;
}
