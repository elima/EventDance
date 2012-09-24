/*
 * evd-jsonrpc-http-server.c
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

#include "evd-jsonrpc-http-server.h"

#include <evd-jsonrpc.h>
#include <evd-http-connection.h>

G_DEFINE_TYPE (EvdJsonrpcHttpServer,
               evd_jsonrpc_http_server,
               EVD_TYPE_WEB_SERVICE)

#define EVD_JSONRPC_HTTP_SERVER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                  EVD_TYPE_JSONRPC_HTTP_SERVER, \
                                                  EvdJsonrpcHttpServerPrivate))

/* private data */
struct _EvdJsonrpcHttpServerPrivate
{
  EvdJsonrpc *rpc;

  EvdJsonrpcHttpServerMethodCallCb method_call_cb;
  gpointer method_call_user_data;
  GDestroyNotify method_call_user_data_free_func;

  SoupMessageHeaders *headers;
};

typedef struct
{
  EvdJsonrpcHttpServer *self;
  gchar *buf;
  gpointer context;
  guint invocation_id;
  GCancellable *cancellable;
  JsonNode *json_result;
  JsonNode *json_error;
} CallData;

/* properties */
enum
{
  PROP_0,
  PROP_RESPONSE_HEADERS
};

static void     evd_jsonrpc_http_server_class_init         (EvdJsonrpcHttpServerClass *class);
static void     evd_jsonrpc_http_server_init               (EvdJsonrpcHttpServer *self);

static void     evd_jsonrpc_http_server_finalize           (GObject *obj);

static void     evd_jsonrpc_http_server_set_property       (GObject      *obj,
                                                            guint         prop_id,
                                                            const GValue *value,
                                                            GParamSpec   *pspec);
static void     evd_jsonrpc_http_server_get_property       (GObject    *obj,
                                                            guint       prop_id,
                                                            GValue     *value,
                                                            GParamSpec *pspec);

static void     on_request_headers                         (EvdWebService     *self,
                                                            EvdHttpConnection *conn,
                                                            EvdHttpRequest    *request);

static void     jsonrpc_on_send                            (EvdJsonrpc  *rpc,
                                                            const gchar *buffer,
                                                            gpointer     context,
                                                            guint        invocation_id,
                                                            gpointer     user_data);
static void     jsonrpc_on_method_call                     (EvdJsonrpc  *rpc,
                                                            const gchar *method_name,
                                                            JsonNode    *params,
                                                            guint        invocation_id,
                                                            gpointer     context,
                                                            gpointer     user_data);

static void
evd_jsonrpc_http_server_class_init (EvdJsonrpcHttpServerClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdWebServiceClass *web_service_class = EVD_WEB_SERVICE_CLASS (class);

  obj_class->finalize = evd_jsonrpc_http_server_finalize;
  obj_class->get_property = evd_jsonrpc_http_server_get_property;
  obj_class->set_property = evd_jsonrpc_http_server_set_property;

  web_service_class->request_handler = on_request_headers;

  g_object_class_install_property (obj_class, PROP_RESPONSE_HEADERS,
                                   g_param_spec_boxed ("response-headers",
                                                       "Response headers",
                                                       "The object's internal response headers",
                                                       SOUP_TYPE_MESSAGE_HEADERS,
                                                       G_PARAM_READABLE |
                                                       G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdJsonrpcHttpServerPrivate));
}

static void
evd_jsonrpc_http_server_init (EvdJsonrpcHttpServer *self)
{
  EvdJsonrpcHttpServerPrivate *priv;

  priv = EVD_JSONRPC_HTTP_SERVER_GET_PRIVATE (self);
  self->priv = priv;

  priv->rpc = evd_jsonrpc_new ();
  evd_jsonrpc_set_method_call_callback (priv->rpc,
                                        jsonrpc_on_method_call,
                                        self);

  evd_jsonrpc_transport_set_send_callback (priv->rpc,
                                           jsonrpc_on_send,
                                           self,
                                           (GDestroyNotify) g_object_unref);

  priv->headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
  soup_message_headers_replace (priv->headers,
                                "Content-type",
                                "application/json; charset=utf-8");
}

static void
evd_jsonrpc_http_server_finalize (GObject *obj)
{
  EvdJsonrpcHttpServer *self = EVD_JSONRPC_HTTP_SERVER (obj);

  evd_jsonrpc_transport_set_send_callback (self->priv->rpc, NULL, NULL, NULL);
  g_object_unref (self->priv->rpc);

  soup_message_headers_free (self->priv->headers);

  if (self->priv->method_call_user_data != NULL &&
      self->priv->method_call_user_data_free_func)
    {
      self->priv->method_call_user_data_free_func
        (self->priv->method_call_user_data);
    }

  G_OBJECT_CLASS (evd_jsonrpc_http_server_parent_class)->finalize (obj);
}

static void
evd_jsonrpc_http_server_set_property (GObject      *obj,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  /*
  EvdJsonrpcHttpServer *self;

  self = EVD_JSONRPC_HTTP_SERVER (obj);
  */

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_jsonrpc_http_server_get_property (GObject    *obj,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  EvdJsonrpcHttpServer *self;

  self = EVD_JSONRPC_HTTP_SERVER (obj);

  switch (prop_id)
    {
    case PROP_RESPONSE_HEADERS:
      g_value_set_boxed (value, self->priv->headers);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
jsonrpc_on_send (EvdJsonrpc  *rpc,
                 const gchar *message,
                 gpointer     context,
                 guint        invocation_id,
                 gpointer     user_data)
{
  EvdJsonrpcHttpServer *self = EVD_JSONRPC_HTTP_SERVER (user_data);
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (context);
  GError *error = NULL;

  if (! evd_web_service_respond (EVD_WEB_SERVICE (self),
                                 conn,
                                 SOUP_STATUS_OK,
                                 self->priv->headers,
                                 message,
                                 strlen (message),
                                 NULL))
    {
      evd_jsonrpc_transport_error (self->priv->rpc, invocation_id, error);
      g_error_free (error);
    }

  g_object_unref (conn);
}

static void
jsonrpc_on_method_call (EvdJsonrpc  *rpc,
                        const gchar *method_name,
                        JsonNode    *params,
                        guint        invocation_id,
                        gpointer     context,
                        gpointer     user_data)
{
  EvdJsonrpcHttpServer *self = EVD_JSONRPC_HTTP_SERVER (user_data);
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (context);

  if (self->priv->method_call_cb != NULL)
    {
      EvdHttpRequest *req;

      req = evd_http_connection_get_current_request (conn);

      g_object_ref (conn);
      self->priv->method_call_cb (self,
                                  method_name,
                                  params,
                                  invocation_id,
                                  conn,
                                  req,
                                  self->priv->method_call_user_data);
    }
  else
    {
      const gchar *err_st = "No handler for method calls";

      evd_web_service_respond (EVD_WEB_SERVICE (self),
                               conn,
                               SOUP_STATUS_INTERNAL_SERVER_ERROR,
                               self->priv->headers,
                               err_st,
                               strlen (err_st),
                               NULL);
    }
}

static void
on_content_read (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  EvdJsonrpcHttpServer *self = EVD_JSONRPC_HTTP_SERVER (user_data);
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);
  GError *error = NULL;
  gchar *content;

  content = evd_http_connection_read_all_content_finish (conn,
                                                         result,
                                                         NULL,
                                                         &error);
  if (content == NULL)
    {
      g_print ("Error reading content: %s\n", error->message);
      g_error_free (error);

      goto out;
    }

  if (! evd_jsonrpc_transport_receive (self->priv->rpc, content, conn, 0, &error))
    {
      evd_web_service_respond (EVD_WEB_SERVICE (self),
                               conn,
                               SOUP_STATUS_INTERNAL_SERVER_ERROR,
                               self->priv->headers,
                               error->message,
                               strlen (error->message),
                               NULL);
      g_error_free (error);
    }

 out:
  g_free (content);
  g_object_unref (self);
}

static void
on_request_headers (EvdWebService     *web_service,
                    EvdHttpConnection *conn,
                    EvdHttpRequest    *request)
{
  EvdJsonrpcHttpServer *self = EVD_JSONRPC_HTTP_SERVER (web_service);
  const gchar *err_st;

  /* validate request */

  /* method must be POST */
  if (g_strcmp0 (evd_http_request_get_method (request), SOUP_METHOD_POST) != 0)
    {
      err_st = "Method must be POST";
      goto err;
    }

  /* read request content */
  g_object_ref (self);
  evd_http_connection_read_all_content (conn,
                                        NULL,
                                        on_content_read,
                                        self);
  return;

 err:
  evd_web_service_respond (web_service,
                           conn,
                           SOUP_STATUS_INTERNAL_SERVER_ERROR,
                           self->priv->headers,
                           err_st,
                           strlen (err_st),
                           NULL);
}

/* public methods */

/**
 * evd_jsonrpc_http_server_new: (constructor):
 *
 * Returns: (transfer full):
 **/
EvdJsonrpcHttpServer *
evd_jsonrpc_http_server_new (void)
{
  EvdJsonrpcHttpServer *self;

  self = g_object_new (EVD_TYPE_JSONRPC_HTTP_SERVER, NULL);

  return self;
}

/**
 * evd_jsonrpc_http_server_get_response_headers:
 *
 * Returns: (transfer none):
 **/
SoupMessageHeaders *
evd_jsonrpc_http_server_get_response_headers (EvdJsonrpcHttpServer *self)
{
  g_return_val_if_fail (EVD_IS_JSONRPC_HTTP_SERVER (self), NULL);

  return self->priv->headers;
}

void
evd_jsonrpc_http_server_set_method_call_callback
                         (EvdJsonrpcHttpServer             *self,
                          EvdJsonrpcHttpServerMethodCallCb  callback,
                          gpointer                          user_data,
                          GDestroyNotify                    user_data_free_func)
{
  g_return_if_fail (EVD_IS_JSONRPC_HTTP_SERVER (self));

  self->priv->method_call_cb = callback;
  self->priv->method_call_user_data = user_data;
  self->priv->method_call_user_data_free_func = user_data_free_func;
}

gboolean
evd_jsonrpc_http_server_respond (EvdJsonrpcHttpServer  *self,
                                 guint                  invocation_id,
                                 JsonNode              *result,
                                 GError               **error)
{
  g_return_val_if_fail (EVD_IS_JSONRPC_HTTP_SERVER (self), FALSE);

  return evd_jsonrpc_respond (self->priv->rpc,
                              invocation_id,
                              result,
                              NULL,
                              error);
}

gboolean
evd_jsonrpc_http_server_respond_error (EvdJsonrpcHttpServer  *self,
                                       guint                  invocation_id,
                                       JsonNode              *json_error,
                                       GError               **error)
{
  g_return_val_if_fail (EVD_IS_JSONRPC_HTTP_SERVER (self), FALSE);

  return evd_jsonrpc_respond_error (self->priv->rpc,
                                    invocation_id,
                                    json_error,
                                    NULL,
                                    error);
}
