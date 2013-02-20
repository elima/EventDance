/*
 * evd-web-transport-server.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2013, Igalia S.L.
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

#include <json-glib/json-glib.h>

#include "evd-web-transport-server.h"
#include "evd-transport.h"

#include "evd-utils.h"
#include "evd-error.h"
#include "evd-http-connection.h"
#include "evd-peer-manager.h"
#include "evd-web-dir.h"

#include "evd-longpolling-server.h"
#include "evd-websocket-server.h"

#define EVD_WEB_TRANSPORT_SERVER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                   EVD_TYPE_WEB_TRANSPORT_SERVER, \
                                                   EvdWebTransportServerPrivate))

#define DEFAULT_BASE_PATH "/transport"

#define MECHANISM_HEADER_NAME "X-Org-EventDance-WebTransport-Mechanism"
#define PEER_ID_HEADER_NAME   "X-Org-EventDance-WebTransport-Peer-Id"
#define URL_HEADER_NAME       "X-Org-EventDance-WebTransport-Url"

#define HANDSHAKE_TOKEN_NAME    "handshake"
#define LONG_POLLING_TOKEN_NAME "lp"
#define WEB_SOCKET_TOKEN_NAME   "ws"

#define LONG_POLLING_MECHANISM_NAME "long-polling"
#define WEB_SOCKET_MECHANISM_NAME   "websocket"

#define HANDSHAKE_DATA_KEY "org.eventdance.lib.WebTransport.HANDSHAKE_DATA"

#define PEER_DATA_KEY "org.eventdance.lib.WebTransportServer.PEER_DATA"

/* handshake data */
typedef struct
{
  EvdWebTransportServer *self;
  EvdHttpConnection *conn;
  EvdHttpRequest *request;
  JsonNode *request_data;
  JsonNode *response_data;
} HandshakeData;

/* private data */
struct _EvdWebTransportServerPrivate
{
  gchar *base_path;
  gchar *hs_base_path;

  EvdWebSelector *selector;

  EvdLongpollingServer *lp;
  gchar *lp_base_path;

  EvdWebsocketServer *ws;
  gchar *ws_base_path;

  gboolean enable_ws;

  HandshakeData *current_handshake_data;
};

/* properties */
enum
{
  PROP_0,
  PROP_BASE_PATH,
  PROP_SELECTOR,
  PROP_LP_SERVICE,
  PROP_WEBSOCKET_SERVICE
};

static void     evd_web_transport_server_class_init           (EvdWebTransportServerClass *class);
static void     evd_web_transport_server_init                 (EvdWebTransportServer *self);

static void     evd_web_transport_server_transport_iface_init (EvdTransportInterface *iface);

static void     evd_web_transport_server_finalize             (GObject *obj);
static void     evd_web_transport_server_dispose              (GObject *obj);

static void     evd_web_transport_server_set_property         (GObject      *obj,
                                                               guint         prop_id,
                                                               const GValue *value,
                                                               GParamSpec   *pspec);
static void     evd_web_transport_server_get_property         (GObject    *obj,
                                                               guint       prop_id,
                                                               GValue     *value,
                                                               GParamSpec *pspec);

static void     evd_web_transport_server_open                 (EvdTransport       *transport,
                                                               const gchar        *address,
                                                               GSimpleAsyncResult *async_result,
                                                               GCancellable       *cancellable);

static gboolean evd_web_transport_server_send                 (EvdTransport    *transport,
                                                               EvdPeer         *peer,
                                                               const gchar     *buffer,
                                                               gsize            size,
                                                               EvdMessageType   type,
                                                               GError         **error);

static gboolean evd_web_transport_server_peer_is_connected    (EvdTransport *transport,
                                                               EvdPeer      *peer);

static void     evd_web_transport_server_on_request           (EvdWebService     *self,
                                                               EvdHttpConnection *conn,
                                                               EvdHttpRequest    *request);

static void     evd_web_transport_server_set_base_path        (EvdWebTransportServer *self,
                                                               const gchar     *base_path);

static gboolean evd_web_transport_server_accept_peer          (EvdTransport *transport,
                                                               EvdPeer      *peer);
static gboolean evd_web_transport_server_reject_peer          (EvdTransport *transport,
                                                               EvdPeer      *peer);

G_DEFINE_TYPE_WITH_CODE (EvdWebTransportServer, evd_web_transport_server, EVD_TYPE_WEB_DIR,
                         G_IMPLEMENT_INTERFACE (EVD_TYPE_TRANSPORT,
                                                evd_web_transport_server_transport_iface_init));

static void
evd_web_transport_server_class_init (EvdWebTransportServerClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdWebServiceClass *web_service_class = EVD_WEB_SERVICE_CLASS (class);

  obj_class->dispose = evd_web_transport_server_dispose;
  obj_class->finalize = evd_web_transport_server_finalize;
  obj_class->get_property = evd_web_transport_server_get_property;
  obj_class->set_property = evd_web_transport_server_set_property;

  web_service_class->request_handler = evd_web_transport_server_on_request;

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
                                                        "Internal Long-Polling service used by the transport",
                                                        EVD_TYPE_LONGPOLLING_SERVER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_WEBSOCKET_SERVICE,
                                   g_param_spec_object ("websocket-service",
                                                        "Websocket service",
                                                        "Internal Websocket service used by the transport",
                                                        EVD_TYPE_WEBSOCKET_SERVER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdWebTransportServerPrivate));
}

static void
evd_web_transport_server_transport_iface_init (EvdTransportInterface *iface)
{
  iface->send = evd_web_transport_server_send;
  iface->peer_is_connected = evd_web_transport_server_peer_is_connected;
  iface->accept_peer = evd_web_transport_server_accept_peer;
  iface->reject_peer = evd_web_transport_server_reject_peer;
  iface->open = evd_web_transport_server_open;
}

static void
evd_web_transport_server_init (EvdWebTransportServer *self)
{
  EvdWebTransportServerPrivate *priv;
  const gchar *js_path;

  priv = EVD_WEB_TRANSPORT_SERVER_GET_PRIVATE (self);
  self->priv = priv;

  priv->selector = NULL;

  priv->lp = evd_longpolling_server_new ();
  priv->ws = evd_websocket_server_new ();

  js_path = g_getenv ("JSLIBDIR");
  if (js_path == NULL)
    js_path = JSLIBDIR;

  evd_web_dir_set_root (EVD_WEB_DIR (self), js_path);

  priv->enable_ws = TRUE;

  priv->current_handshake_data = NULL;
}

static void
evd_web_transport_server_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_web_transport_server_parent_class)->dispose (obj);
}

static void
evd_web_transport_server_finalize (GObject *obj)
{
  EvdWebTransportServer *self = EVD_WEB_TRANSPORT_SERVER (obj);

  g_free (self->priv->lp_base_path);
  g_object_unref (self->priv->lp);

  g_free (self->priv->ws_base_path);
  g_object_unref (self->priv->ws);

  g_free (self->priv->hs_base_path);
  g_free (self->priv->base_path);

  G_OBJECT_CLASS (evd_web_transport_server_parent_class)->finalize (obj);
}

static void
evd_web_transport_server_set_property (GObject      *obj,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  EvdWebTransportServer *self;

  self = EVD_WEB_TRANSPORT_SERVER (obj);

  switch (prop_id)
    {
    case PROP_BASE_PATH:
      evd_web_transport_server_set_base_path (self, g_value_get_string (value));
      break;

    case PROP_SELECTOR:
      self->priv->selector = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_web_transport_server_get_property (GObject    *obj,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  EvdWebTransportServer *self;

  self = EVD_WEB_TRANSPORT_SERVER (obj);

  switch (prop_id)
    {
    case PROP_BASE_PATH:
      g_value_set_string (value, evd_web_transport_server_get_base_path (self));
      break;

    case PROP_SELECTOR:
      g_value_set_object (value, self->priv->selector);
      break;

    case PROP_LP_SERVICE:
      g_value_set_object (value, self->priv->lp);
      break;

    case PROP_WEBSOCKET_SERVICE:
      g_value_set_object (value, self->priv->ws);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gboolean
evd_web_transport_server_send (EvdTransport    *transport,
                               EvdPeer         *peer,
                               const gchar     *buffer,
                               gsize            size,
                               EvdMessageType   type,
                               GError         **error)
{
  EvdTransport *_transport;

  _transport = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);
  if (_transport == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_INITIALIZED,
                   "Failed to send data, peer is not associated with a "
                   "sub-transport");
      return FALSE;
    }
  else
    {
      return EVD_TRANSPORT_GET_INTERFACE (_transport)->send (_transport,
                                                             peer,
                                                             buffer,
                                                             size,
                                                             type,
                                                             error);
    }
}

static gboolean
evd_web_transport_server_peer_is_connected (EvdTransport *transport,
                                            EvdPeer      *peer)
{
  EvdTransport *_transport;

  _transport = g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);

  return _transport != NULL &&
    evd_transport_peer_is_connected (_transport, peer);
}

static void
add_mechanism_to_response_list (JsonArray   *mech_list,
                                const gchar *mechanism_name,
                                const gchar *mechanism_url)
{
  JsonObject *obj;

  obj = json_object_new ();

  json_object_set_string_member (obj, "name", mechanism_name);
  json_object_set_string_member (obj, "url", mechanism_url);

  json_array_add_object_element (mech_list, obj);
}

static gboolean
has_mechanism (JsonArray *mech_list, const gchar *mech_name)
{
  gint i;
  JsonNode *element;

  for (i=0; i<json_array_get_length (mech_list); i++)
    {
      element = json_array_get_element (mech_list, i);

      if (g_strcmp0 (mech_name, json_node_get_string (element)) == 0)
        return TRUE;
    }

  return FALSE;
}

static void
free_handshake_data (HandshakeData *data)
{
  g_object_unref (data->self);
  g_object_unref (data->conn);
  g_object_unref (data->request);

  if (data->request_data != NULL)
    json_node_free (data->request_data);

  if (data->response_data != NULL)
    json_node_free (data->response_data);

  g_slice_free (HandshakeData, data);
}

static void
evd_web_transport_server_respond_handshake (HandshakeData *data,
                                            EvdPeer       *peer)
{
  EvdWebTransportServer *self;
  gchar *mechanism_url;
  GError *error = NULL;
  SoupURI *uri = NULL;

  JsonObject *request_obj;
  JsonObject *response_obj;

  JsonArray *request_mechs;
  JsonArray *response_mechs;

  JsonGenerator *generator;
  gchar *content;
  gsize content_size;

  SoupMessageHeaders *headers;

  self = data->self;

  /* setup the repsponse data JSON object */
  data->response_data = json_node_new (JSON_NODE_OBJECT);
  response_obj = json_object_new ();
  json_node_set_object (data->response_data, response_obj);

  /* set peer id in response data*/
  json_object_set_string_member (response_obj,
                                 "peer-id",
                                 evd_peer_get_id (peer));

  /* set the list of mechanisms in response data */
  response_mechs = json_array_new ();
  json_object_set_array_member (response_obj, "mechanisms", response_mechs);

  request_obj = json_node_get_object (data->request_data);
  request_mechs = json_object_get_array_member (request_obj, "mechanisms");

  /* resolve the transport url from peer's perspective */
  if (json_object_has_member (request_obj, "url"))
    {
      const gchar *uri_str;

      uri_str = json_object_get_string_member (request_obj, "url");
      uri = soup_uri_new (uri_str);

      /* @TODO: validate that uri is not null and fail the handshake if so */
    }

  if (uri == NULL)
    uri = soup_uri_copy (evd_http_request_get_uri (data->request));

  /* websocket? */
  if (self->priv->enable_ws &&
      has_mechanism (request_mechs, WEB_SOCKET_MECHANISM_NAME))
    {
      SoupURI *ws_uri;

      ws_uri = soup_uri_copy (uri);
      if (evd_connection_get_tls_active (EVD_CONNECTION (data->conn)))
        soup_uri_set_scheme (ws_uri, "wss");
      else
        soup_uri_set_scheme (ws_uri, "ws");
      soup_uri_set_port (ws_uri, uri->port);
      soup_uri_set_path (ws_uri, self->priv->ws_base_path);
      mechanism_url = soup_uri_to_string (ws_uri, FALSE);
      soup_uri_free (ws_uri);

      add_mechanism_to_response_list (response_mechs,
                                      WEB_SOCKET_MECHANISM_NAME,
                                      mechanism_url);
      g_free (mechanism_url);
    }

  /* long-polling? */
  if (has_mechanism (request_mechs, LONG_POLLING_MECHANISM_NAME))
    {
      SoupURI *lp_uri;

      lp_uri = soup_uri_copy (uri);
      soup_uri_set_path (lp_uri, self->priv->lp_base_path);
      soup_uri_set_query (lp_uri, NULL);
      mechanism_url = soup_uri_to_string (lp_uri, FALSE);
      soup_uri_free (lp_uri);

      add_mechanism_to_response_list (response_mechs,
                                      LONG_POLLING_MECHANISM_NAME,
                                      mechanism_url);
      g_free (mechanism_url);
    }

  /* generate JSON data for the response */
  generator = json_generator_new ();
  json_generator_set_root (generator, data->response_data);
  content = json_generator_to_data (generator, &content_size);
  json_object_unref (response_obj);
  g_object_unref (generator);

  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

  /* prevent HTTP caching */
  soup_message_headers_replace (headers,
                                "Cache-Control",
                                "Cache-Control: no-cache, must-revalidate");
  soup_message_headers_replace (headers,
                                "Expires",
                                "Sat, 01 Jan 2000 00:00:00 GMT");

  if (! EVD_WEB_SERVICE_GET_CLASS (self)->respond (EVD_WEB_SERVICE (self),
                                                   data->conn,
                                                   SOUP_STATUS_OK,
                                                   headers,
                                                   content,
                                                   content_size,
                                                   &error))
    {
      /* @TODO: do proper logging */
      g_debug ("Error responding handshake: %s", error->message);
      g_error_free (error);
    }

  soup_message_headers_free (headers);

  soup_uri_free (uri);
  g_free (content);
}

static void
evd_web_transport_server_conn_on_close (EvdConnection *conn, gpointer user_data)
{
  EvdPeer *peer = user_data;

  g_object_set_data (G_OBJECT (peer), HANDSHAKE_DATA_KEY, NULL);

  g_signal_handlers_disconnect_by_func (conn,
                                        evd_web_transport_server_conn_on_close,
                                        user_data);

  g_object_unref (peer);
}

static void
evd_web_transport_handshake (HandshakeData *data)
{
  EvdWebTransportServer *self;
  EvdPeer *peer;

  JsonObject *request_obj;
  JsonArray *request_mechs;

  EvdTransportInterface *iface;
  guint validate_result;

  self = data->self;

  /* check if at least one mechanism can be negotiated */
  request_obj = json_node_get_object (data->request_data);
  request_mechs = json_object_get_array_member (request_obj, "mechanisms");

  if (request_mechs == NULL ||
      (! has_mechanism (request_mechs, WEB_SOCKET_MECHANISM_NAME) &&
       ! has_mechanism (request_mechs, LONG_POLLING_MECHANISM_NAME)))
    {
      /* return 503 Service Unavailable, no mechanism can be negotiated */
      EVD_WEB_SERVICE_GET_CLASS (self)->
        respond (EVD_WEB_SERVICE (self),
                 data->conn,
                 SOUP_STATUS_SERVICE_UNAVAILABLE,
                 NULL,
                 NULL,
                 0,
                 NULL);
      return;
    }

  /* create peer */
  peer = g_object_new (EVD_TYPE_PEER, "transport", self, NULL);

  /* setup peer arguments for validation */
  self->priv->current_handshake_data = data;

  /* validate peer */
  iface = EVD_TRANSPORT_GET_INTERFACE (self);
  validate_result = iface->notify_validate_peer (EVD_TRANSPORT (self), peer);

  /* teardown peer arguments */
  self->priv->current_handshake_data = NULL;

  /* check result of peer validation */
  if (validate_result == EVD_VALIDATE_ACCEPT)
    {
      /* accept peer */
      evd_web_transport_server_accept_peer (EVD_TRANSPORT (self), peer);

      evd_web_transport_server_respond_handshake (data, peer);

      free_handshake_data (data);
    }
  else if (validate_result == EVD_VALIDATE_REJECT)
    {
      /* reject peer */
      EVD_WEB_SERVICE_GET_CLASS (self)->
        respond (EVD_WEB_SERVICE (self),
                 data->conn,
                 SOUP_STATUS_FORBIDDEN,
                 NULL,
                 NULL,
                 0,
                 NULL);

      free_handshake_data (data);
    }
  else
    {
      /* peer validation pending */
      g_object_set_data_full (G_OBJECT (peer),
                              HANDSHAKE_DATA_KEY,
                              data,
                              (GDestroyNotify) free_handshake_data);

      g_object_ref (peer);
      g_signal_connect (data->conn,
                        "close",
                        G_CALLBACK (evd_web_transport_server_conn_on_close),
                        peer);
    }

  g_object_unref (peer);
}

static void
evd_web_transport_on_handshake_data (GObject      *obj,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
  HandshakeData *data = user_data;
  gchar *content;
  gssize size;
  GError *error = NULL;

  g_assert (obj == G_OBJECT (data->conn));

  content = evd_http_connection_read_all_content_finish (data->conn,
                                                         res,
                                                         &size,
                                                         &error);
  if (content != NULL)
    {
      JsonParser *parser;

      parser = json_parser_new ();
      if (json_parser_load_from_data (parser,
                                      content,
                                      size,
                                      &error))
        {
          JsonNode *root;

          root = json_parser_get_root (parser);
          if (root == NULL)
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "No handshake data sent");
            }
          else if (! JSON_NODE_HOLDS_OBJECT (root))
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Handshake data must be a JSON object");
            }
          else
            {
              data->request_data = json_node_copy (json_parser_get_root (parser));
            }
        }

      g_object_unref (parser);
    }

  if (error == NULL)
    {
      evd_web_transport_handshake (data);
    }
  else
    {
      /* @TODO: do proper logging */
      g_debug ("Web transport handshake failed: %s", error->message);
      g_error_free (error);

      EVD_WEB_SERVICE_GET_CLASS (data->self)->
        respond (EVD_WEB_SERVICE (data->self),
                 data->conn,
                 SOUP_STATUS_INTERNAL_SERVER_ERROR,
                 NULL,
                 NULL,
                 0,
                 NULL);

      free_handshake_data (data);
    }

  g_free (content);
}

static void
evd_web_transport_server_read_handshake_data (EvdWebTransportServer *self,
                                              EvdHttpConnection     *conn,
                                              EvdHttpRequest        *request)
{
  HandshakeData *data;

  data = g_slice_new0 (HandshakeData);

  data->self = self;
  g_object_ref (self);

  data->conn = conn;
  g_object_ref (conn);

  data->request = request;
  g_object_ref (request);

  evd_http_connection_read_all_content (conn,
                                        NULL,
                                        evd_web_transport_on_handshake_data,
                                        data);
}

static EvdWebService *
get_actual_transport_from_path (EvdWebTransportServer *self,
                                const gchar           *path)
{
  if (g_strstr_len (path, -1, self->priv->lp_base_path) == path)
    return EVD_WEB_SERVICE (self->priv->lp);
  else if (self->priv->enable_ws &&
           g_strstr_len (path, -1, self->priv->ws_base_path) == path)
    return EVD_WEB_SERVICE (self->priv->ws);
  else
    return NULL;
}

static void
evd_web_transport_server_on_request (EvdWebService     *web_service,
                                     EvdHttpConnection *conn,
                                     EvdHttpRequest    *request)
{
  EvdWebTransportServer *self = EVD_WEB_TRANSPORT_SERVER (web_service);
  SoupURI *uri;
  EvdWebService *actual_service;

  uri = evd_http_request_get_uri (request);

  /* handshake? */
  if (g_strcmp0 (uri->path, self->priv->hs_base_path) == 0)
    {
      evd_web_transport_server_read_handshake_data (self, conn, request);
    }
  /* longpolling or websocket? */
  else if ((actual_service =
            get_actual_transport_from_path (self, uri->path)) != NULL)
    {
      EvdPeer *peer;
      EvdTransport *current_transport;

      if (uri->query != NULL &&
          (peer = evd_transport_lookup_peer (EVD_TRANSPORT (self),
                                             uri->query)) != NULL)
        {
          evd_peer_touch (peer);

          current_transport = EVD_TRANSPORT (g_object_get_data (G_OBJECT (peer),
                                                                PEER_DATA_KEY));
          if (current_transport != EVD_TRANSPORT (actual_service))
            {
              g_object_ref (actual_service);
              g_object_set_data_full (G_OBJECT (peer),
                                      PEER_DATA_KEY,
                                      actual_service,
                                      g_object_unref);
            }
        }

      evd_web_service_add_connection_with_request (EVD_WEB_SERVICE (actual_service),
                                                   conn,
                                                   request,
                                                   NULL);
    }
  else
    {
      EVD_WEB_SERVICE_CLASS (evd_web_transport_server_parent_class)->
        request_handler (web_service,
                         conn,
                         request);
    }
}

static void
evd_web_transport_server_set_base_path (EvdWebTransportServer *self,
                                        const gchar           *base_path)
{
  g_return_if_fail (base_path != NULL);

  if (base_path[strlen (base_path) - 1] == '/')
    self->priv->base_path = g_strdup (base_path);
  else
    self->priv->base_path = g_strdup_printf ("%s/", base_path);

  self->priv->hs_base_path = g_strdup_printf ("%s%s",
                                              self->priv->base_path,
                                              HANDSHAKE_TOKEN_NAME);
  self->priv->lp_base_path = g_strdup_printf ("%s%s",
                                              self->priv->base_path,
                                              LONG_POLLING_TOKEN_NAME);
  self->priv->ws_base_path = g_strdup_printf ("%s%s",
                                              self->priv->base_path,
                                              WEB_SOCKET_TOKEN_NAME);

  evd_web_dir_set_alias (EVD_WEB_DIR (self), base_path);
}

static gboolean
evd_web_transport_server_accept_peer (EvdTransport *transport, EvdPeer *peer)
{
  EvdPeerManager *peer_manager;
  HandshakeData *data;

  peer_manager = evd_transport_get_peer_manager (transport);

  if (evd_peer_manager_lookup_peer (peer_manager,
                                    evd_peer_get_id (peer)) == NULL)
    {
      evd_peer_manager_add_peer (peer_manager, peer);

      EVD_TRANSPORT_GET_INTERFACE (transport)->
        notify_new_peer (transport, peer);
    }

  data = g_object_get_data (G_OBJECT (peer), HANDSHAKE_DATA_KEY);
  if (data == NULL)
    return TRUE;

  g_signal_handlers_disconnect_by_func (data->conn,
                                        evd_web_transport_server_conn_on_close,
                                        peer);

  evd_web_transport_server_respond_handshake (data, peer);

  g_object_set_data (G_OBJECT (peer), HANDSHAKE_DATA_KEY, NULL);
  g_object_unref (peer);

  return TRUE;
}

static gboolean
evd_web_transport_server_reject_peer (EvdTransport *transport, EvdPeer *peer)
{
  HandshakeData *data;

  data = g_object_get_data (G_OBJECT (peer), HANDSHAKE_DATA_KEY);
  if (data == NULL)
    return TRUE;

  g_signal_handlers_disconnect_by_func (data->conn,
                                        evd_web_transport_server_conn_on_close,
                                        peer);

  EVD_WEB_SERVICE_GET_CLASS (transport)->respond (EVD_WEB_SERVICE (transport),
                                                  data->conn,
                                                  SOUP_STATUS_FORBIDDEN,
                                                  NULL,
                                                  NULL,
                                                  0,
                                                  NULL);

  g_object_set_data (G_OBJECT (peer), HANDSHAKE_DATA_KEY, NULL);
  g_object_unref (peer);

  return TRUE;
}

static void
evd_web_transport_server_on_open (GObject      *obj,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  GError *error = NULL;
  GSimpleAsyncResult *orig_res = G_SIMPLE_ASYNC_RESULT (user_data);

  if (! evd_service_listen_finish (EVD_SERVICE (obj), res, &error))
    {
      g_simple_async_result_set_from_error (orig_res, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (orig_res);
  g_object_unref (orig_res);
}

static void
evd_web_transport_server_open (EvdTransport       *transport,
                               const gchar        *address,
                               GSimpleAsyncResult *async_result,
                               GCancellable       *cancellable)
{
  evd_service_listen (EVD_SERVICE (transport),
                      address,
                      cancellable,
                      evd_web_transport_server_on_open,
                      async_result);
}

/* public methods */

EvdWebTransportServer *
evd_web_transport_server_new (const gchar *base_path)
{
  if (base_path == NULL)
    base_path = DEFAULT_BASE_PATH;

  return g_object_new (EVD_TYPE_WEB_TRANSPORT_SERVER,
                       "base-path", base_path,
                       NULL);
}

void
evd_web_transport_server_set_selector (EvdWebTransportServer *self,
                                       EvdWebSelector        *selector)
{
  g_return_if_fail (EVD_IS_WEB_TRANSPORT_SERVER (self));
  g_return_if_fail (EVD_IS_WEB_SELECTOR (selector));

  self->priv->selector = selector;

  evd_web_transport_server_use_selector (self, selector);
}

/**
 * evd_web_transport_server_get_selector:
 *
 * Returns: (transfer none):
 **/
EvdWebSelector *
evd_web_transport_server_get_selector (EvdWebTransportServer *self)
{
  g_return_val_if_fail (EVD_IS_WEB_TRANSPORT_SERVER (self), NULL);

  return self->priv->selector;
}

void
evd_web_transport_server_use_selector (EvdWebTransportServer *self,
                                       EvdWebSelector        *selector)
{
  g_return_if_fail (EVD_IS_WEB_TRANSPORT_SERVER (self));
  g_return_if_fail (EVD_IS_WEB_SELECTOR (selector));

  evd_web_selector_add_service (selector,
                                NULL,
                                self->priv->base_path,
                                EVD_SERVICE (self),
                                NULL);
}

void
evd_web_transport_server_unuse_selector (EvdWebTransportServer *self,
                                         EvdWebSelector        *selector)
{
  g_return_if_fail (EVD_IS_WEB_TRANSPORT_SERVER (self));
  g_return_if_fail (EVD_IS_WEB_SELECTOR (selector));

  evd_web_selector_remove_service (selector,
                                   NULL,
                                   self->priv->base_path,
                                   EVD_SERVICE (self));
}

const gchar *
evd_web_transport_server_get_base_path (EvdWebTransportServer *self)
{
  g_return_val_if_fail (EVD_IS_WEB_TRANSPORT_SERVER (self), NULL);

  return self->priv->base_path;
}

void
evd_web_transport_server_set_enable_websocket (EvdWebTransportServer *self,
                                               gboolean               enabled)
{
  g_return_if_fail (EVD_IS_WEB_TRANSPORT_SERVER (self));

  self->priv->enable_ws = enabled;
}

/**
 * evd_web_transport_server_get_validate_peer_arguments:
 * @conn: (out) (allow-none) (transfer none):
 * @request: (out) (allow-none) (transfer none):
 *
 **/
void
evd_web_transport_server_get_validate_peer_arguments (EvdWebTransportServer  *self,
                                                      EvdPeer                *peer,
                                                      EvdHttpConnection     **conn,
                                                      EvdHttpRequest        **request)
{
  g_return_if_fail (EVD_IS_WEB_TRANSPORT_SERVER (self));

  if (self->priv->current_handshake_data == NULL)
    return;

  if (conn != NULL)
    *conn = self->priv->current_handshake_data->conn;

  if (request != NULL)
    *request = self->priv->current_handshake_data->request;
}
