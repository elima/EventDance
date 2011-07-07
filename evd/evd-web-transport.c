/*
 * evd-web-transport.c
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

#include "evd-web-transport.h"
#include "evd-transport.h"

#include "evd-utils.h"
#include "evd-error.h"
#include "evd-http-connection.h"
#include "evd-peer-manager.h"
#include "evd-web-dir.h"

#include "evd-long-polling.h"
#include "evd-websocket-server.h"

#define EVD_WEB_TRANSPORT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                            EVD_TYPE_WEB_TRANSPORT, \
                                            EvdWebTransportPrivate))

#define DEFAULT_BASE_PATH "/transport"

#define MECHANISM_HEADER_NAME "X-Org-EventDance-WebTransport-Mechanism"
#define PEER_ID_HEADER_NAME   "X-Org-EventDance-WebTransport-Peer-Id"
#define URL_HEADER_NAME       "X-Org-EventDance-WebTransport-Url"

#define HANDSHAKE_TOKEN_NAME    "handshake"
#define LONG_POLLING_TOKEN_NAME "lp"
#define WEB_SOCKET_TOKEN_NAME   "ws"

#define LONG_POLLING_MECHANISM_NAME "long-polling"
#define WEB_SOCKET_MECHANISM_NAME   "web-socket"

#define VALIDATE_PEER_ARGS_DATA_KEY "org.eventdance.lib.WebTransport.VALIDATE_PEER_ARGS"

/* private data */
struct _EvdWebTransportPrivate
{
  gchar *base_path;
  gchar *hs_base_path;

  EvdWebSelector *selector;

  EvdLongPolling *lp;
  gchar *lp_base_path;

  EvdWebsocketServer *ws;
  gchar *ws_base_path;

  gboolean enable_ws;

  EvdHttpConnection *peer_arg_conn;
  EvdHttpRequest *peer_arg_request;
  guint validate_peer_result;
};

/* validate-peer data */
typedef struct
{
  EvdHttpConnection *conn;
  EvdHttpRequest *request;
  const gchar *mechanism;
} ValidatePeerData;

/* properties */
enum
{
  PROP_0,
  PROP_BASE_PATH,
  PROP_SELECTOR,
  PROP_LP_SERVICE
};

static void     evd_web_transport_class_init           (EvdWebTransportClass *class);
static void     evd_web_transport_init                 (EvdWebTransport *self);

static void     evd_web_transport_transport_iface_init (EvdTransportInterface *iface);

static void     evd_web_transport_finalize             (GObject *obj);
static void     evd_web_transport_dispose              (GObject *obj);

static void     evd_web_transport_set_property         (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void     evd_web_transport_get_property         (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static void     evd_web_transport_on_receive           (EvdTransport *transport,
                                                        EvdPeer      *peer,
                                                        gpointer      user_data);
static void     evd_web_transport_on_new_peer          (EvdTransport *transport,
                                                        EvdPeer      *peer,
                                                        gpointer      user_data);
static void     evd_web_transport_on_peer_closed       (EvdTransport *transport,
                                                        EvdPeer      *peer,
                                                        gboolean      gracefully,
                                                        gpointer      user_data);

static gboolean evd_web_transport_send                 (EvdTransport  *transport,
                                                        EvdPeer       *peer,
                                                        const gchar   *buffer,
                                                        gsize          size,
                                                        GError       **error);

static gboolean evd_web_transport_peer_is_connected    (EvdTransport *transport,
                                                        EvdPeer      *peer);

static void     evd_web_transport_on_request           (EvdWebService     *self,
                                                        EvdHttpConnection *conn,
                                                        EvdHttpRequest    *request);

static void     evd_web_transport_set_base_path        (EvdWebTransport *self,
                                                        const gchar     *base_path);

static guint    evd_web_transport_on_validate_peer     (EvdTransport      *transport,
                                                        EvdPeer           *peer,
                                                        gpointer           user_data);

static gboolean evd_web_transport_accept_peer          (EvdTransport *transport,
                                                        EvdPeer      *peer);
static gboolean evd_web_transport_reject_peer          (EvdTransport *transport,
                                                        EvdPeer      *peer);

static void     evd_web_transport_connect_signals      (EvdWebTransport *self,
                                                        EvdTransport    *transport);
static void     evd_web_transport_disconnect_signals   (EvdWebTransport *self,
                                                        EvdTransport    *transport);

G_DEFINE_TYPE_WITH_CODE (EvdWebTransport, evd_web_transport, EVD_TYPE_WEB_DIR,
                         G_IMPLEMENT_INTERFACE (EVD_TYPE_TRANSPORT,
                                                evd_web_transport_transport_iface_init));

static void
evd_web_transport_class_init (EvdWebTransportClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdWebServiceClass *web_service_class = EVD_WEB_SERVICE_CLASS (class);

  obj_class->dispose = evd_web_transport_dispose;
  obj_class->finalize = evd_web_transport_finalize;
  obj_class->get_property = evd_web_transport_get_property;
  obj_class->set_property = evd_web_transport_set_property;

  web_service_class->request_handler = evd_web_transport_on_request;

  g_object_class_install_property (obj_class, PROP_BASE_PATH,
                                   g_param_spec_string ("base-path",
                                                        "Base path",
                                                        "URL base path the transport handles",
                                                        DEFAULT_BASE_PATH,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_SELECTOR,
                                   g_param_spec_object ("selector",
                                                        "Transport's Web selector",
                                                        "Web selector object used by this transport to route its requests",
                                                        EVD_TYPE_WEB_SELECTOR,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_LP_SERVICE,
                                   g_param_spec_object ("lp-service",
                                                        "Long-Polling service",
                                                        "Internal Long-Polling service used by this transport",
                                                        EVD_TYPE_LONG_POLLING,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdWebTransportPrivate));
}

static void
evd_web_transport_transport_iface_init (EvdTransportInterface *iface)
{
  iface->send = evd_web_transport_send;
  iface->peer_is_connected = evd_web_transport_peer_is_connected;
  iface->accept_peer = evd_web_transport_accept_peer;
  iface->reject_peer = evd_web_transport_reject_peer;
}

static void
evd_web_transport_init (EvdWebTransport *self)
{
  EvdWebTransportPrivate *priv;
  const gchar *js_path;

  priv = EVD_WEB_TRANSPORT_GET_PRIVATE (self);
  self->priv = priv;

  priv->selector = NULL;

  priv->lp = evd_long_polling_new ();
  evd_web_transport_connect_signals (self, EVD_TRANSPORT (priv->lp));

  priv->ws = evd_websocket_server_new ();
  evd_web_transport_connect_signals (self, EVD_TRANSPORT (priv->ws));

  js_path = g_getenv ("JSLIBDIR");
  if (js_path == NULL)
    js_path = JSLIBDIR;

  evd_web_dir_set_root (EVD_WEB_DIR (self), js_path);

  priv->enable_ws = TRUE;

  priv->peer_arg_conn = NULL;
  priv->peer_arg_request = NULL;
}

static void
evd_web_transport_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_web_transport_parent_class)->dispose (obj);
}

static void
evd_web_transport_finalize (GObject *obj)
{
  EvdWebTransport *self = EVD_WEB_TRANSPORT (obj);

  g_free (self->priv->lp_base_path);
  evd_web_transport_disconnect_signals (self, EVD_TRANSPORT (self->priv->lp));
  g_object_unref (self->priv->lp);

  g_free (self->priv->ws_base_path);
  evd_web_transport_disconnect_signals (self, EVD_TRANSPORT (self->priv->ws));
  g_object_unref (self->priv->ws);

  g_free (self->priv->hs_base_path);
  g_free (self->priv->base_path);

  G_OBJECT_CLASS (evd_web_transport_parent_class)->finalize (obj);

  g_debug ("web transport finalized");
}

static void
evd_web_transport_set_property (GObject      *obj,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EvdWebTransport *self;

  self = EVD_WEB_TRANSPORT (obj);

  switch (prop_id)
    {
    case PROP_BASE_PATH:
      evd_web_transport_set_base_path (self, g_value_get_string (value));
      break;

    case PROP_SELECTOR:
      evd_web_transport_set_selector (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_web_transport_get_property (GObject    *obj,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EvdWebTransport *self;

  self = EVD_WEB_TRANSPORT (obj);

  switch (prop_id)
    {
    case PROP_BASE_PATH:
      g_value_set_string (value, evd_web_transport_get_base_path (self));
      break;

    case PROP_SELECTOR:
      g_value_set_object (value, evd_web_transport_get_selector (self));
      break;

    case PROP_LP_SERVICE:
      g_value_set_object (value, self->priv->lp);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_web_transport_connect_signals (EvdWebTransport *self,
                                   EvdTransport    *transport)
{
  g_signal_connect (transport,
                    "receive",
                    G_CALLBACK (evd_web_transport_on_receive),
                    self);
  g_signal_connect (transport,
                    "new-peer",
                    G_CALLBACK (evd_web_transport_on_new_peer),
                    self);
  g_signal_connect (transport,
                    "peer-closed",
                    G_CALLBACK (evd_web_transport_on_peer_closed),
                    self);
  g_signal_connect (transport,
                    "validate-peer",
                    G_CALLBACK (evd_web_transport_on_validate_peer),
                    self);
}

static void
evd_web_transport_disconnect_signals (EvdWebTransport *self,
                                      EvdTransport    *transport)
{
  g_signal_handlers_disconnect_by_func (transport,
                                        evd_web_transport_on_receive,
                                        self);
  g_signal_handlers_disconnect_by_func (transport,
                                        evd_web_transport_on_new_peer,
                                        self);
  g_signal_handlers_disconnect_by_func (transport,
                                        evd_web_transport_on_peer_closed,
                                        self);
  g_signal_handlers_disconnect_by_func (transport,
                                        evd_web_transport_on_validate_peer,
                                        self);
}

static guint
evd_web_transport_on_validate_peer (EvdTransport      *transport,
                                    EvdPeer           *peer,
                                    gpointer           user_data)
{
  EvdWebTransport *self = EVD_WEB_TRANSPORT (user_data);

  self->priv->validate_peer_result =
    EVD_TRANSPORT_GET_INTERFACE (self)->
    notify_validate_peer (EVD_TRANSPORT (self), peer);

   return self->priv->validate_peer_result;
}

static void
evd_web_transport_on_receive (EvdTransport *transport,
                              EvdPeer      *peer,
                              gpointer      user_data)
{
  EvdWebTransport *self = EVD_WEB_TRANSPORT (user_data);

  EVD_TRANSPORT_GET_INTERFACE (self)->
    notify_receive (EVD_TRANSPORT (self), peer);
}

static void
evd_web_transport_on_new_peer (EvdTransport *transport,
                               EvdPeer      *peer,
                               gpointer      user_data)
{
  EvdWebTransport *self = EVD_WEB_TRANSPORT (user_data);

  EVD_TRANSPORT_GET_INTERFACE (self)->
    notify_new_peer (EVD_TRANSPORT (self), peer);
}

static void
evd_web_transport_on_peer_closed (EvdTransport *transport,
                                  EvdPeer      *peer,
                                  gboolean      gracefully,
                                  gpointer      user_data)
{
  EvdWebTransport *self = EVD_WEB_TRANSPORT (user_data);

  EVD_TRANSPORT_GET_INTERFACE (self)->
    notify_peer_closed (EVD_TRANSPORT (self), peer, gracefully);
}

static gboolean
evd_web_transport_validate_peer_transport (EvdWebTransport  *self,
                                           EvdTransport     *peer_transport,
                                           GError          **error)
{
  if (peer_transport != EVD_TRANSPORT (self->priv->lp) &&
      peer_transport != EVD_TRANSPORT (self->priv->ws))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Invalid peer transport");

      return FALSE;
    }

  return TRUE;
}

static gboolean
evd_web_transport_send (EvdTransport  *transport,
                        EvdPeer       *peer,
                        const gchar   *buffer,
                        gsize          size,
                        GError       **error)
{
  EvdWebTransport *self = EVD_WEB_TRANSPORT (transport);
  EvdTransport *_transport;
  gssize result = 0;

  g_object_get (peer, "transport", &_transport, NULL);

  if (! evd_web_transport_validate_peer_transport (self, _transport, error)
      || ! evd_transport_send (_transport,
                               peer,
                               buffer,
                               size,
                               error))
    {
      result = FALSE;
    }
  else
    {
      result = TRUE;
    }

  g_object_unref (_transport);

  return result;
}

static gboolean
evd_web_transport_peer_is_connected (EvdTransport *transport,
                                     EvdPeer      *peer)
{
  EvdWebTransport *self = EVD_WEB_TRANSPORT (transport);
  EvdTransport *_transport;
  gboolean result = FALSE;

  g_object_get (peer, "transport", &_transport, NULL);

  if (! evd_web_transport_validate_peer_transport (self, transport, NULL))
    result = FALSE;
  else
    result = evd_transport_peer_is_connected (_transport, peer);

  g_object_unref (_transport);

  return result;
}

static void
evd_web_transport_associate_services (EvdWebTransport *self)
{
  if (self->priv->selector != NULL)
    evd_web_selector_add_service (self->priv->selector,
                                  NULL,
                                  self->priv->base_path,
                                  EVD_SERVICE (self),
                                  NULL);
}

static void
evd_web_transport_unassociate_services (EvdWebTransport *self)
{
  if (self->priv->selector != NULL)
    evd_web_selector_remove_service (self->priv->selector,
                                     NULL,
                                     self->priv->base_path,
                                     EVD_SERVICE (self));
}

static void
evd_web_transport_respond_handshake (EvdWebTransport   *self,
                                     EvdPeer           *peer,
                                     EvdHttpConnection *conn,
                                     EvdHttpRequest    *request,
                                     const gchar       *mechanism)
{
  SoupMessageHeaders *res_headers;
  gchar *mechanism_url;
  GError *error = NULL;
  SoupURI *uri;

  uri = evd_http_request_get_uri (request);

  if (g_strcmp0 (mechanism, WEB_SOCKET_MECHANISM_NAME) == 0)
    {
      SoupURI *ws_uri;

      ws_uri = soup_uri_copy (uri);
      if (evd_connection_get_tls_active (EVD_CONNECTION (conn)))
        soup_uri_set_scheme (ws_uri, "wss");
      else
        soup_uri_set_scheme (ws_uri, "ws");
      soup_uri_set_port (ws_uri, uri->port);
      soup_uri_set_path (ws_uri, self->priv->ws_base_path);
      mechanism_url = soup_uri_to_string (ws_uri, FALSE);
      soup_uri_free (ws_uri);
    }
  else
    {
      mechanism_url = g_strdup (self->priv->lp_base_path);
    }

  res_headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
  soup_message_headers_replace (res_headers, MECHANISM_HEADER_NAME, mechanism);
  soup_message_headers_replace (res_headers,
                                PEER_ID_HEADER_NAME,
                                evd_peer_get_id (peer));
  soup_message_headers_replace (res_headers, URL_HEADER_NAME, mechanism_url);
  g_free (mechanism_url);

  if (! EVD_WEB_SERVICE_GET_CLASS (self)->respond (EVD_WEB_SERVICE (self),
                                                   conn,
                                                   SOUP_STATUS_OK,
                                                   res_headers,
                                                   NULL,
                                                   0,
                                                   &error))
    {
      /* @TODO: do proper logging */
      g_debug ("Error responding handshake: %s", error->message);
      g_error_free (error);
    }

  soup_message_headers_free (res_headers);
}

static void
evd_web_transport_free_validate_peer_args (gpointer _data)
{
  ValidatePeerData *data = _data;

  g_object_unref (data->conn);
  g_object_unref (data->request);

  g_slice_free (ValidatePeerData, data);
}

static void
evd_web_transport_conn_on_close (EvdConnection *conn, gpointer user_data)
{
  EvdPeer *peer = user_data;

  g_object_set_data (G_OBJECT (peer), VALIDATE_PEER_ARGS_DATA_KEY, NULL);

  g_signal_handlers_disconnect_by_func (conn,
                                        evd_web_transport_conn_on_close,
                                        user_data);

  g_object_unref (peer);
}

static void
evd_web_transport_handshake (EvdWebTransport   *self,
                             EvdHttpConnection *conn,
                             EvdHttpRequest    *request)
{
  SoupMessageHeaders *req_headers;
  const gchar *mechanisms;
  EvdPeer *peer;
  const gchar *mechanism = NULL;

  req_headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  /* get list of supported mechanisms as sent by client */
  mechanisms = soup_message_headers_get_one (req_headers,
                                             MECHANISM_HEADER_NAME);
  if (mechanisms == NULL)
    {
      /* return 503 Service Unavailable, no mechanism can be negotiated */
      EVD_WEB_SERVICE_GET_CLASS (self)->
        respond (EVD_WEB_SERVICE (self),
                 conn,
                 SOUP_STATUS_SERVICE_UNAVAILABLE,
                 NULL,
                 NULL,
                 0,
                 NULL);
      return;
    }

  /* setup peer arguments for validation */
  self->priv->peer_arg_conn = conn;
  self->priv->peer_arg_request = request;

  /* choose among client's supported transports */
  if (self->priv->enable_ws &&
      g_strstr_len (mechanisms, -1, WEB_SOCKET_MECHANISM_NAME) != NULL)
    {
      mechanism = WEB_SOCKET_MECHANISM_NAME;
      peer = evd_transport_create_new_peer (EVD_TRANSPORT (self->priv->ws));
    }
  else if (g_strstr_len (mechanisms, -1, LONG_POLLING_MECHANISM_NAME) != NULL)
    {
      mechanism = LONG_POLLING_MECHANISM_NAME;
      peer = evd_transport_create_new_peer (EVD_TRANSPORT (self->priv->lp));
    }

  /* teardown peer arguments */
  self->priv->peer_arg_conn = NULL;
  self->priv->peer_arg_request = NULL;

  if (mechanism == NULL)
    {
      /* return 503 Service Unavailable, no mechanism can be negotiated */
      EVD_WEB_SERVICE_GET_CLASS (self)->
        respond (EVD_WEB_SERVICE (self),
                 conn,
                 SOUP_STATUS_SERVICE_UNAVAILABLE,
                 NULL,
                 NULL,
                 0,
                 NULL);
      return;
    }

  /* check result of peer validation (triggered by call to create_new_peer()) */
  if (self->priv->validate_peer_result == EVD_VALIDATE_ACCEPT)
    {
      /* accept peer */
      evd_web_transport_respond_handshake (self,
                                           peer,
                                           conn,
                                           request,
                                           mechanism);
    }
  else if (self->priv->validate_peer_result == EVD_VALIDATE_REJECT)
    {
      /* reject peer */
      EVD_WEB_SERVICE_GET_CLASS (self)->
        respond (EVD_WEB_SERVICE (self),
                 conn,
                 SOUP_STATUS_FORBIDDEN,
                 NULL,
                 NULL,
                 0,
                 NULL);
    }
  else
    {
      /* peer validation pending */

      ValidatePeerData *data;

      data = g_slice_new0 (ValidatePeerData);

      data->conn = conn;
      g_object_ref (conn);

      data->request = request;
      g_object_ref (request);

      data->mechanism = mechanism;

      g_object_set_data_full (G_OBJECT (peer),
                              VALIDATE_PEER_ARGS_DATA_KEY,
                              data,
                              evd_web_transport_free_validate_peer_args);

      g_signal_connect (conn,
                        "close",
                        G_CALLBACK (evd_web_transport_conn_on_close),
                        peer);
    }
}

static void
evd_web_transport_on_request (EvdWebService     *web_service,
                              EvdHttpConnection *conn,
                              EvdHttpRequest    *request)
{
  EvdWebTransport *self = EVD_WEB_TRANSPORT (web_service);
  SoupURI *uri;

  uri = evd_http_request_get_uri (request);

  /* handshake? */
  if (g_strcmp0 (uri->path, self->priv->hs_base_path) == 0)
    {
      evd_web_transport_handshake (self, conn, request);
    }
  /* long-polling? */
  else if (g_strstr_len (uri->path, -1, self->priv->lp_base_path) == uri->path)
    {
      evd_web_service_add_connection_with_request (EVD_WEB_SERVICE (self->priv->lp),
                                                   conn,
                                                   request,
                                                   NULL);
    }
  /* web-socket? */
  else if (self->priv->enable_ws &&
           g_strstr_len (uri->path, -1, self->priv->ws_base_path) == uri->path)
    {
      evd_web_service_add_connection_with_request (EVD_WEB_SERVICE (self->priv->ws),
                                                   conn,
                                                   request,
                                                   NULL);
    }
  /* transport's static content */
  else
    {
      EVD_WEB_SERVICE_CLASS (evd_web_transport_parent_class)->
        request_handler (web_service,
                         conn,
                         request);
    }
}

static void
evd_web_transport_set_base_path (EvdWebTransport *self,
                                 const gchar     *base_path)
{
  g_return_if_fail (base_path != NULL);

  self->priv->base_path = g_strdup (base_path);
  evd_web_transport_associate_services (self);

  self->priv->hs_base_path = g_strdup_printf ("%s/%s",
                                              self->priv->base_path,
                                              HANDSHAKE_TOKEN_NAME);
  self->priv->lp_base_path = g_strdup_printf ("%s/%s",
                                              self->priv->base_path,
                                              LONG_POLLING_TOKEN_NAME);
  self->priv->ws_base_path = g_strdup_printf ("%s/%s",
                                              self->priv->base_path,
                                              WEB_SOCKET_TOKEN_NAME);

  evd_web_dir_set_alias (EVD_WEB_DIR (self), base_path);
}

static gboolean
evd_web_transport_accept_peer (EvdTransport *transport, EvdPeer *peer)
{
  ValidatePeerData *data;

  data = g_object_get_data (G_OBJECT (peer), VALIDATE_PEER_ARGS_DATA_KEY);
  if (data == NULL)
    return FALSE;

  g_signal_handlers_disconnect_by_func (data->conn,
                                        evd_web_transport_conn_on_close,
                                        peer);

  evd_web_transport_respond_handshake (EVD_WEB_TRANSPORT (transport),
                                       peer,
                                       data->conn,
                                       data->request,
                                       data->mechanism);

  g_object_set_data (G_OBJECT (peer), VALIDATE_PEER_ARGS_DATA_KEY, NULL);

  return TRUE;
}

static gboolean
evd_web_transport_reject_peer (EvdTransport *transport, EvdPeer *peer)
{
  ValidatePeerData *data;

  data = g_object_get_data (G_OBJECT (peer), VALIDATE_PEER_ARGS_DATA_KEY);
  if (data == NULL)
    return FALSE;

  g_signal_handlers_disconnect_by_func (data->conn,
                                        evd_web_transport_conn_on_close,
                                        peer);

  EVD_WEB_SERVICE_GET_CLASS (transport)->respond (EVD_WEB_SERVICE (transport),
                                                  data->conn,
                                                  SOUP_STATUS_FORBIDDEN,
                                                  NULL,
                                                  NULL,
                                                  0,
                                                  NULL);

  g_object_set_data (G_OBJECT (peer), VALIDATE_PEER_ARGS_DATA_KEY, NULL);

  return TRUE;
}

/* public methods */

EvdWebTransport *
evd_web_transport_new (const gchar *base_path)
{
  if (base_path == NULL)
    base_path = DEFAULT_BASE_PATH;

  return g_object_new (EVD_TYPE_WEB_TRANSPORT,
                       "base-path", base_path,
                       NULL);
}

void
evd_web_transport_set_selector (EvdWebTransport *self,
                                EvdWebSelector  *selector)
{
  g_return_if_fail (EVD_IS_WEB_TRANSPORT (self));
  g_return_if_fail (EVD_IS_WEB_SELECTOR (selector));

  if (self->priv->selector != NULL)
    {
      evd_web_transport_unassociate_services (self);
      g_object_unref (self->priv->selector);
    }

  self->priv->selector = selector;

  evd_web_transport_associate_services (self);
  g_object_ref (selector);
}

EvdWebSelector *
evd_web_transport_get_selector (EvdWebTransport *self)
{
  g_return_val_if_fail (EVD_IS_WEB_TRANSPORT (self), NULL);

  return self->priv->selector;
}

const gchar *
evd_web_transport_get_base_path (EvdWebTransport *self)
{
  g_return_val_if_fail (EVD_IS_WEB_TRANSPORT (self), NULL);

  return self->priv->base_path;
}

void
evd_web_transport_set_enable_websocket (EvdWebTransport *self,
                                        gboolean         enabled)
{
  g_return_if_fail (EVD_IS_WEB_TRANSPORT (self));

  self->priv->enable_ws = enabled;
}

/**
 * evd_web_transport_get_validate_peer_arguments:
 * @conn: (out) (allow-none) (transfer none):
 * @request: (out) (allow-none) (transfer none):
 *
 **/
void
evd_web_transport_get_validate_peer_arguments (EvdWebTransport    *self,
                                               EvdPeer            *peer,
                                               EvdHttpConnection **conn,
                                               EvdHttpRequest    **request)
{
  g_return_if_fail (EVD_IS_WEB_TRANSPORT (self));

  if (conn != NULL)
    *conn = self->priv->peer_arg_conn;

  if (request != NULL)
    *request = self->priv->peer_arg_request;
}
