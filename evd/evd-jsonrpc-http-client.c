/*
 * evd-jsonrpc-http-client.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2012, Igalia S.L.
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

#include "evd-jsonrpc-http-client.h"

#include "evd-jsonrpc.h"
#include "evd-http-connection.h"

G_DEFINE_TYPE (EvdJsonrpcHttpClient,
               evd_jsonrpc_http_client,
               EVD_TYPE_CONNECTION_POOL)

#define EVD_JSONRPC_HTTP_CLIENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                  EVD_TYPE_JSONRPC_HTTP_CLIENT, \
                                                  EvdJsonrpcHttpClientPrivate))

/* private data */
struct _EvdJsonrpcHttpClientPrivate
{
  gchar *url;

  EvdJsonrpc *rpc;
};

typedef struct
{
  EvdJsonrpcHttpClient *self;
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
  PROP_URL
};

static void     evd_jsonrpc_http_client_class_init         (EvdJsonrpcHttpClientClass *class);
static void     evd_jsonrpc_http_client_init               (EvdJsonrpcHttpClient *self);

static void     evd_jsonrpc_http_client_finalize           (GObject *obj);

static void     evd_jsonrpc_http_client_set_property       (GObject      *obj,
                                                            guint         prop_id,
                                                            const GValue *value,
                                                            GParamSpec   *pspec);
static void     evd_jsonrpc_http_client_get_property       (GObject    *obj,
                                                            guint       prop_id,
                                                            GValue     *value,
                                                            GParamSpec *pspec);

static GType    get_connection_type                        (EvdConnectionPool *conn_pool);

static void     jsonrpc_on_send                            (EvdJsonrpc  *rpc,
                                                            const gchar *buffer,
                                                            gpointer     user_context,
                                                            guint        invocation_id,
                                                            gpointer     user_data);

static void
evd_jsonrpc_http_client_class_init (EvdJsonrpcHttpClientClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdConnectionPoolClass *conn_pool_class = EVD_CONNECTION_POOL_CLASS (class);

  obj_class->finalize = evd_jsonrpc_http_client_finalize;
  obj_class->get_property = evd_jsonrpc_http_client_get_property;
  obj_class->set_property = evd_jsonrpc_http_client_set_property;

  conn_pool_class->get_connection_type = get_connection_type;

  g_object_class_install_property (obj_class, PROP_URL,
                                   g_param_spec_string ("url",
                                                        "URL",
                                                        "The target server URL",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdJsonrpcHttpClientPrivate));
}

static void
evd_jsonrpc_http_client_init (EvdJsonrpcHttpClient *self)
{
  EvdJsonrpcHttpClientPrivate *priv;

  priv = EVD_JSONRPC_HTTP_CLIENT_GET_PRIVATE (self);
  self->priv = priv;

  priv->rpc = evd_jsonrpc_new ();
  evd_jsonrpc_transport_set_send_callback (priv->rpc,
                                           jsonrpc_on_send,
                                           self,
                                           (GDestroyNotify) g_object_unref);
  g_object_ref (self);
}

static void
evd_jsonrpc_http_client_finalize (GObject *obj)
{
  EvdJsonrpcHttpClient *self = EVD_JSONRPC_HTTP_CLIENT (obj);

  g_free (self->priv->url);

  g_object_unref (self->priv->rpc);

  G_OBJECT_CLASS (evd_jsonrpc_http_client_parent_class)->finalize (obj);
}

static void
evd_jsonrpc_http_client_set_property (GObject      *obj,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  EvdJsonrpcHttpClient *self;

  self = EVD_JSONRPC_HTTP_CLIENT (obj);

  switch (prop_id)
    {
    case PROP_URL:
      {
        self->priv->url = g_value_dup_string (value);
        break;
      }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_jsonrpc_http_client_get_property (GObject    *obj,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  EvdJsonrpcHttpClient *self;

  self = EVD_JSONRPC_HTTP_CLIENT (obj);

  switch (prop_id)
    {
    case PROP_URL:
      g_value_set_string (value, self->priv->url);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static GType
get_connection_type (EvdConnectionPool *conn_pool)
{
  return EVD_TYPE_HTTP_CONNECTION;
}

static void
free_call_data (gpointer _data)
{
  CallData *data = _data;

  g_object_unref (data->self);

  g_free (data->buf);

  if (data->json_result != NULL)
    json_node_free (data->json_result);

  if (data->json_error != NULL)
    json_node_free (data->json_error);

  g_slice_free (CallData, data);
}

static void
on_content_read (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);
  GError *error = NULL;
  gchar *content;
  gssize size;
  CallData *data;

  data = g_simple_async_result_get_op_res_gpointer (res);

  content = evd_http_connection_read_all_content_finish (conn,
                                                         result,
                                                         &size,
                                                         &error);

  /* recycle connection if keep-alive */
  if (evd_http_connection_get_keepalive (conn))
    evd_connection_pool_recycle (EVD_CONNECTION_POOL (data->self),
                                 EVD_CONNECTION (conn));

  if (content == NULL)
    {
      /* notify JSON-RPC of transport error */
      evd_jsonrpc_transport_error (data->self->priv->rpc,
                                   data->invocation_id,
                                   error);
      g_error_free (error);
    }
  else
    {
      if (! evd_jsonrpc_transport_receive (data->self->priv->rpc,
                                           content,
                                           res,
                                           data->invocation_id,
                                           NULL))
        {
          /* Server responded with invalid JSON-RPC data. EvdJsonrpc already
             handles the transport error internally, within transport_receive(). */
        }

      g_free (content);
    }
}

static void
on_response_headers (GObject      *obj,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);
  GError *error = NULL;
  guint status_code;
  gchar *reason;
  SoupMessageHeaders *headers;

  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
  CallData *data;

  data = g_simple_async_result_get_op_res_gpointer (res);

  headers = evd_http_connection_read_response_headers_finish (conn,
                                                              result,
                                                              NULL,
                                                              &status_code,
                                                              &reason,
                                                              &error);

  if (headers == NULL)
    {
      /* notify JSON-RPC of transport error */
      evd_jsonrpc_transport_error (data->self->priv->rpc,
                                   data->invocation_id,
                                   error);
      g_error_free (error);
    }
  else
    {
      if (status_code == SOUP_STATUS_OK)
        {
          evd_http_connection_read_all_content (conn,
                                                NULL,
                                                on_content_read,
                                                user_data);
        }
      else
        {
          /* notify JSON-RPC of transport error */
          g_set_error (&error,
                       G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       "HTTP error from server: %u %s",
                       status_code,
                       reason);

          evd_jsonrpc_transport_error (data->self->priv->rpc,
                                       data->invocation_id,
                                       error);
          g_error_free (error);
        }

      soup_message_headers_free (headers);
      g_free (reason);
    }

  evd_connection_unlock_close (EVD_CONNECTION (conn));
}

static void
on_request_sent (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);
  GError *error = NULL;
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
  CallData *data;

  data = g_simple_async_result_get_op_res_gpointer (res);

  if (! evd_http_connection_write_request_headers_finish (conn,
                                                          result,
                                                          &error))
    {
      /* notify JSON-RPC of transport error */
      evd_jsonrpc_transport_error (data->self->priv->rpc,
                                   data->invocation_id,
                                   error);
      g_error_free (error);

      return;
    }

  /* write content */
  if (! evd_http_connection_write_content (conn,
                                           data->buf,
                                           strlen (data->buf),
                                           FALSE,
                                           &error))
    {
      /* notify JSON-RPC of transport error */
      evd_jsonrpc_transport_error (data->self->priv->rpc,
                                   data->invocation_id,
                                   error);
      g_error_free (error);
    }
  else
    {
      evd_http_connection_read_response_headers (conn,
                                                 data->cancellable,
                                                 on_response_headers,
                                                 res);
    }
}

static void
do_request (EvdHttpConnection *conn, gpointer user_data)
{
  SoupMessageHeaders *headers;
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
  CallData *data;

  EvdHttpRequest *request;
  SoupURI *uri;
  gchar *sock_addr;

  data = g_simple_async_result_get_op_res_gpointer (res);

  request = evd_http_request_new (EVD_CONNECTION (conn),
                                  SOUP_METHOD_POST,
                                  data->self->priv->url);

  uri = evd_http_request_get_uri (request);
  sock_addr = g_strdup_printf ("%s:%u", uri->host, uri->port);

  g_object_set (data->self,
                "address", sock_addr,
                NULL);
  g_free (sock_addr);

  headers =
    evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));
  soup_message_headers_set_content_length (headers, strlen (data->buf));

  evd_connection_lock_close (EVD_CONNECTION (conn));
  evd_http_connection_write_request_headers (conn,
                                             request,
                                             NULL,
                                             on_request_sent,
                                             res);
  g_object_unref (request);
}

static void
on_connection (GObject      *obj,
               GAsyncResult *result,
               gpointer      user_data)
{
  EvdHttpConnection *conn;
  GError *error = NULL;

  conn = EVD_HTTP_CONNECTION
    (evd_connection_pool_get_connection_finish (EVD_CONNECTION_POOL (obj),
                                                result,
                                                &error));
  if (conn == NULL)
    {
      GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
      CallData *data;

      data = g_simple_async_result_get_op_res_gpointer (res);

      /* notify JSON-RPC of transport error */
      evd_jsonrpc_transport_error (data->self->priv->rpc,
                                   data->invocation_id,
                                   error);
      g_error_free (error);
    }
  else
    {
      do_request (conn, user_data);

      g_object_unref (conn);
    }
}

static void
jsonrpc_on_send (EvdJsonrpc  *rpc,
                 const gchar *buffer,
                 gpointer     user_context,
                 guint        invocation_id,
                 gpointer     user_data)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_context);
  CallData *data;

  data = g_simple_async_result_get_op_res_gpointer (res);

  data->buf = g_strdup (buffer);
  data->invocation_id = invocation_id;

  evd_connection_pool_get_connection (EVD_CONNECTION_POOL (data->self),
                                      data->cancellable,
                                      on_connection,
                                      user_context);
}

static void
jsonrpc_on_method_call_result (GObject      *obj,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
  CallData *data;
  GError *error = NULL;

  data = g_simple_async_result_get_op_res_gpointer (res);

  if (! evd_jsonrpc_call_method_finish (EVD_JSONRPC (obj),
                                        result,
                                        &data->json_result,
                                        &data->json_error,
                                        &error))
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (res);
  g_object_unref (res);
}

/* public methods */

/**
 * evd_jsonrpc_http_client_new:
 *
 * Returns: (transfer full):
 **/
EvdJsonrpcHttpClient *
evd_jsonrpc_http_client_new (const gchar *url)
{
  EvdJsonrpcHttpClient *self;

  self = g_object_new (EVD_TYPE_JSONRPC_HTTP_CLIENT,
                       "url", url,
                       NULL);

  return self;
}

/**
 * evd_jsonrpc_http_client_call_method:
 * @params: (allow-none):
 * @cancellable: (allow-none):
 * @callback: (allow-none):
 * @user_data: (allow-none):
 *
 **/
void
evd_jsonrpc_http_client_call_method (EvdJsonrpcHttpClient *self,
                                     const gchar          *method,
                                     JsonNode             *params,
                                     GCancellable         *cancellable,
                                     GAsyncReadyCallback   callback,
                                     gpointer              user_data)
{
  CallData *data;
  GSimpleAsyncResult *res;

  g_return_if_fail (EVD_IS_JSONRPC_HTTP_CLIENT (self));
  g_return_if_fail (method != NULL);

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_jsonrpc_http_client_call_method);

  data = g_slice_new0 (CallData);
  data->cancellable = cancellable;
  data->self = self;
  g_object_ref (self);

  g_simple_async_result_set_op_res_gpointer (res,
                                             data,
                                             free_call_data);

  evd_jsonrpc_call_method (self->priv->rpc,
                           method,
                           params,
                           res,
                           cancellable,
                           jsonrpc_on_method_call_result,
                           res);
}

/**
 * evd_jsonrpc_http_client_call_method_finish:
 * @json_result: (out) (allow-none):
 * @json_error: (out) (allow-none):
 * @error: (allow-none):
 **/
gboolean
evd_jsonrpc_http_client_call_method_finish (EvdJsonrpcHttpClient  *self,
                                            GAsyncResult          *result,
                                            JsonNode             **json_result,
                                            JsonNode             **json_error,
                                            GError               **error)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (result);

  g_return_val_if_fail (EVD_IS_JSONRPC_HTTP_CLIENT (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                           G_OBJECT (self),
                                           evd_jsonrpc_http_client_call_method),
                        FALSE);

  if (! g_simple_async_result_propagate_error (res, error))
    {
      CallData *data;

      data = g_simple_async_result_get_op_res_gpointer (res);

      if (json_result != NULL)
        {
          *json_result = data->json_result;
          data->json_result = NULL;
        }

      if (json_error != NULL)
        {
          *json_error = data->json_error;
          data->json_error = NULL;
        }

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}
