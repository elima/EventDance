/*
 * evd-jsonrpc.c
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

#include "evd-jsonrpc.h"

#include "evd-json-filter.h"

G_DEFINE_TYPE (EvdJsonrpc, evd_jsonrpc, G_TYPE_OBJECT)

#define EVD_JSONRPC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      EVD_TYPE_JSONRPC, \
                                      EvdJsonrpcPrivate))

#define DEFAULT_TIMEOUT_INTERVAL 15

struct _EvdJsonrpcPrivate
{
  guint invocation_counter;

  EvdJsonrpcTransportWriteCb write_cb;
  gpointer write_cb_user_data;

  EvdJsonrpcTransportSendCb send_cb;
  gpointer send_cb_user_data;
  GDestroyNotify send_cb_user_data_free_func;

  GHashTable *invocations;

  EvdJsonFilter *json_filter;

  gpointer context;

  EvdJsonrpcMethodCallCb method_call_cb;
  gpointer method_call_cb_user_data;

  GHashTable *transports;
};

typedef struct
{
  JsonNode *result;
  JsonNode *error;
} MethodResponse;

typedef struct
{
  GSimpleAsyncResult *result;
  JsonNode *remote_id;
  gpointer context;
} InvocationData;

static void     evd_jsonrpc_class_init           (EvdJsonrpcClass *class);
static void     evd_jsonrpc_init                 (EvdJsonrpc *self);

static void     evd_jsonrpc_finalize             (GObject *obj);

static void     evd_jsonrpc_on_json_packet       (EvdJsonFilter *self,
                                                  const gchar   *buffer,
                                                  gsize          size,
                                                  gpointer       user_data);

static void     evd_jsonrpc_transport_destroyed  (gpointer  data,
                                                  GObject  *where_the_object_was);
static void     evd_jsonrpc_transport_on_receive (EvdTransport *transport,
                                                  EvdPeer      *peer,
                                                  gpointer      user_data);

static void     free_invocation_data             (InvocationData *data);

static void
evd_jsonrpc_class_init (EvdJsonrpcClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_jsonrpc_finalize;

  g_type_class_add_private (obj_class, sizeof (EvdJsonrpcPrivate));
}

static void
evd_jsonrpc_init (EvdJsonrpc *self)
{
  EvdJsonrpcPrivate *priv;

  priv = EVD_JSONRPC_GET_PRIVATE (self);
  self->priv = priv;

  priv->invocation_counter = 0;

  priv->write_cb = NULL;
  priv->write_cb_user_data = NULL;

  priv->invocations = g_hash_table_new_full (g_str_hash,
                                             g_str_equal,
                                             g_free,
                                             (GDestroyNotify) free_invocation_data);

  priv->json_filter = evd_json_filter_new ();
  evd_json_filter_set_packet_handler (priv->json_filter,
                                      evd_jsonrpc_on_json_packet,
                                      self);

  priv->context = NULL;

  priv->method_call_cb = NULL;
  priv->method_call_cb_user_data = NULL;

  priv->transports = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
evd_jsonrpc_finalize (GObject *obj)
{
  EvdJsonrpc *self = EVD_JSONRPC (obj);
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, self->priv->transports);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      GObject *transport = value;

      g_object_weak_unref (transport,
                           evd_jsonrpc_transport_destroyed,
                           self);

      g_signal_handlers_disconnect_by_func (transport,
                                            evd_jsonrpc_transport_on_receive,
                                            self);
    }
  g_hash_table_unref (self->priv->transports);

  g_object_unref (self->priv->json_filter);

  g_hash_table_unref (self->priv->invocations);

  if (self->priv->send_cb_user_data != NULL &&
      self->priv->send_cb_user_data_free_func != NULL)
    {
      self->priv->send_cb_user_data_free_func (self->priv->send_cb_user_data);
    }

  G_OBJECT_CLASS (evd_jsonrpc_parent_class)->finalize (obj);
}

static void
evd_jsonrpc_transport_destroyed (gpointer  data,
                                 GObject  *where_the_object_was)
{
  EvdJsonrpc *self = EVD_JSONRPC (data);

  g_hash_table_remove (self->priv->transports, where_the_object_was);
}

static gchar *
evd_jsonrpc_build_message (EvdJsonrpc  *self,
                           gboolean     request,
                           const gchar *method_name,
                           JsonNode    *id,
                           JsonNode    *params,
                           JsonNode    *error)
{
  JsonNode *root;
  JsonObject *obj;
  gchar *msg;
  JsonGenerator *gen;
  JsonNode *id_node;
  JsonNode *error_node = NULL;
  JsonNode *params_node = NULL;

  root = json_node_new (JSON_NODE_OBJECT);
  obj = json_object_new ();
  json_node_set_object (root, obj);

  if (id == NULL)
    id_node = json_node_new (JSON_NODE_NULL);
  else
    id_node = json_node_copy (id);

  json_object_set_member (obj, "id", id_node);

  if (request)
    {
      JsonNode *method_node;

      if (params == NULL)
        {
          params_node = json_node_new (JSON_NODE_ARRAY);
          json_node_take_array (params_node, json_array_new ());
        }
      else
        {
          params_node = json_node_copy (params);
        }

      method_node = json_node_new (JSON_NODE_VALUE);
      json_node_set_string (method_node, method_name);
      json_object_set_member (obj, "method", method_node);

      json_object_set_member (obj, "params", params_node);
    }
  else
    {
      if (params == NULL)
        params_node = json_node_new (JSON_NODE_NULL);
      else
        params_node = json_node_copy (params);

      if (error == NULL)
        error_node = json_node_new (JSON_NODE_NULL);
      else
        error_node = json_node_copy (error);

      json_object_set_member (obj, "error", error_node);
      json_object_set_member (obj, "result", params_node);
    }

  gen = json_generator_new ();
  json_generator_set_root (gen, root);

  msg = json_generator_to_data (gen, NULL);

  g_object_unref (gen);

  json_object_unref (obj);
  json_node_free (root);

  return msg;
}

static gboolean
evd_jsonrpc_on_method_called (EvdJsonrpc  *self,
                              JsonObject  *msg,
                              gpointer     context,
                              GError     **error)
{
  JsonNode *node;
  JsonNode *args;
  const gchar *method_name;

  InvocationData *inv_data;
  guint id;
  gchar *id_st;
  JsonNode *id_node;

  node = json_object_get_member (msg, "method");
  method_name = json_node_get_string (node);
  if (! JSON_NODE_HOLDS_VALUE (node) ||
      method_name == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Method name in JSON-RPC must be a valid string");
      return FALSE;
    }

  args = json_object_get_member (msg, "params");
  if (! JSON_NODE_HOLDS_ARRAY (args))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Params in a JSON-RPC request must be an array");
      return FALSE;
    }

  id_node = json_object_dup_member (msg, "id");
  if (json_node_is_null (id_node))
    {
      /* @TODO: request is a notification */

      json_node_free (id_node);

      return TRUE;
    }

  inv_data = g_slice_new0 (InvocationData);
  inv_data->remote_id = id_node;
  inv_data->context = context;

  self->priv->invocation_counter++;
  id = self->priv->invocation_counter;
  id_st = g_strdup_printf ("%u", id);

  g_hash_table_insert (self->priv->invocations, id_st, inv_data);

  if (self->priv->method_call_cb != NULL)
    {
      g_print ("method called: %s\n", method_name);

      self->priv->method_call_cb (self,
                                  method_name,
                                  args,
                                  id,
                                  context,
                                  self->priv->method_call_cb_user_data);
    }

  return TRUE;
}

static void
free_method_response_data (gpointer _data)
{
  MethodResponse *data = _data;

  if (data->result != NULL)
    json_node_free (data->result);

  if (data->error != NULL)
    json_node_free (data->error);

  g_slice_free (MethodResponse, _data);
}

static void
evd_jsonrpc_on_method_result (EvdJsonrpc  *self,
                              JsonObject  *msg,
                              gpointer     context)
{
  const gchar *id;
  JsonNode *id_node;
  JsonNode *result_node;
  JsonNode *error_node;
  MethodResponse *data;
  InvocationData *inv_data;
  GSimpleAsyncResult *res;

  id_node = json_object_get_member (msg, "id");
  id = json_node_get_string (id_node);

  inv_data = g_hash_table_lookup (self->priv->invocations, id);
  if (inv_data == NULL)
    {
      /* @TODO: do proper logging */
      g_print ("Received unexpected JSON-RPC response message with id '%s'\n", id);

      return;
    }

  res = inv_data->result;
  g_object_ref (res);
  g_hash_table_remove (self->priv->invocations, id);

  result_node = json_object_get_member (msg, "result");
  error_node = json_object_get_member (msg, "error");

  if (! (json_node_is_null (result_node) || json_node_is_null (error_node)))
    {
      /* protocol error, one of 'result' or 'error' should be null */
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_INVALID_DATA,
                                       "Protocol error, invalid JSON-RPC response message: one or 'result' or 'error' must be null");
    }
  else
    {
      data = g_slice_new0 (MethodResponse);
      g_simple_async_result_set_op_res_gpointer (res,
                                                 data,
                                                 free_method_response_data);

      if (! json_node_is_null (result_node))
        data->result = json_node_copy (result_node);
      else
        data->error = json_node_copy (error_node);
    }

  g_simple_async_result_complete (res);
  g_object_unref (res);
}

static void
evd_jsonrpc_on_json_packet (EvdJsonFilter *filter,
                            const gchar   *buffer,
                            gsize          size,
                            gpointer       user_data)
{
  EvdJsonrpc *self = EVD_JSONRPC (user_data);
  JsonParser *parser;
  JsonNode *root;
  JsonObject *obj;
  GError *error = NULL;

  parser = json_parser_new ();

  json_parser_load_from_data (parser,
                              buffer,
                              size,
                              NULL);

  root = json_parser_get_root (parser);
  g_assert (root != NULL);

  if (! JSON_NODE_HOLDS_OBJECT (root))
    {
      error = g_error_new (G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "JSON-RPC message must be a JSON object");
      goto out;
    }

  obj = json_node_get_object (root);

  if (! json_object_has_member (obj, "id"))
    {
      error = g_error_new (G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "JSON-RPC message must have an 'id' member");
      goto out;
    }

  if (json_object_has_member (obj, "result") &&
      json_object_has_member (obj, "error"))
    {
      /* a method result */
      evd_jsonrpc_on_method_result (self,
                                    obj,
                                    self->priv->context);
    }
  else if (json_object_has_member (obj, "method") &&
           json_object_has_member (obj, "params"))
    {
      /* a method call */
      evd_jsonrpc_on_method_called (self,
                                    obj,
                                    self->priv->context,
                                    &error);
    }
  else
    {
      error = g_error_new (G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Invalid JSON-RPC message");
    }

 out:

  if (error != NULL)
    {
      /* @TODO: do proper debugging */
      g_print ("JSON-RPC ERROR: %s", error->message);
      g_error_free (error);
    }

  g_object_unref (parser);
}

static void
evd_jsonrpc_transport_write (EvdJsonrpc   *self,
                             const gchar  *msg,
                             gpointer      user_context,
                             guint         invocation_id)
{
  GError *error = NULL;

  if (user_context != NULL && EVD_IS_PEER (user_context))
    {
      if (! evd_peer_send_text (EVD_PEER (user_context), msg, &error))
        {
          evd_jsonrpc_transport_error (self, invocation_id, error);
          g_error_free (error);
        }
    }
  else if (self->priv->send_cb != NULL)
    {
      self->priv->send_cb (self,
                           msg,
                           user_context,
                           invocation_id,
                           self->priv->send_cb_user_data);
    }
  else
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   G_IO_ERROR_CLOSED,
                   "No JSON-RPC transport to deliver message");

      evd_jsonrpc_transport_error (self, invocation_id, error);
      g_error_free (error);
    }
}

static void
evd_jsonrpc_transport_on_receive (EvdTransport *transport,
                                  EvdPeer      *peer,
                                  gpointer      user_data)
{
  EvdJsonrpc *self = EVD_JSONRPC (user_data);
  const gchar *data;

  data = evd_transport_receive (transport, peer, NULL);

  evd_jsonrpc_transport_receive (self, data, peer, 0, NULL);
}

static gboolean
evd_jsonrpc_respond_full (EvdJsonrpc  *self,
                          guint        invocation_id,
                          JsonNode    *result_node,
                          JsonNode    *error_node,
                          GError     **error)
{
  gchar *id_st;
  JsonNode *id_node;
  gchar *msg;
  gboolean res = TRUE;
  InvocationData *inv_data;
  gpointer context;

  g_return_val_if_fail (EVD_IS_JSONRPC (self), FALSE);
  g_return_val_if_fail (invocation_id > 0, FALSE);

  id_st = g_strdup_printf ("%u", invocation_id);
  inv_data = g_hash_table_lookup (self->priv->invocations, id_st);

  if (inv_data == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "No method invocation found with such id");
      res = FALSE;
    }
  else if (inv_data->context == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_CLOSED,
                           "Failed to respond method, no transport associated");
      res = FALSE;
    }
  else
    {
      id_node = inv_data->remote_id;
      inv_data->remote_id = NULL;
      context = inv_data->context;

      g_hash_table_remove (self->priv->invocations, id_st);

      msg = evd_jsonrpc_build_message (self,
                                       FALSE,
                                       NULL,
                                       id_node,
                                       result_node,
                                       error_node);

      json_node_free (id_node);

      evd_jsonrpc_transport_write (self, msg, context, invocation_id);

      g_free (msg);
    }

  g_free (id_st);

  return res;
}

static void
free_invocation_data (InvocationData *data)
{
  if (data->result != NULL)
    g_object_unref (data->result);

  if (data->remote_id != NULL)
    json_node_free (data->remote_id);

  g_slice_free (InvocationData, data);
}

/* public methods */

EvdJsonrpc *
evd_jsonrpc_new ()
{
  return g_object_new (EVD_TYPE_JSONRPC, NULL);
}

void
evd_jsonrpc_transport_set_write_callback (EvdJsonrpc                 *self,
                                          EvdJsonrpcTransportWriteCb  callback,
                                          gpointer                    user_data)
{
  g_return_if_fail (EVD_IS_JSONRPC (self));

  self->priv->write_cb = callback;
  self->priv->write_cb_user_data = user_data;
}

/**
 * evd_jsonrpc_transport_set_send_callback:
 * @callback: (scope notified) (allow-none):
 * @user_data: (allow-none):
 * @user_data_free_func: (allow-none):
 *
 **/
void
evd_jsonrpc_transport_set_send_callback (EvdJsonrpc                *self,
                                         EvdJsonrpcTransportSendCb  callback,
                                         gpointer                   user_data,
                                         GDestroyNotify             user_data_free_func)
{
  g_return_if_fail (EVD_IS_JSONRPC (self));

  self->priv->send_cb = callback;
  self->priv->send_cb_user_data = user_data;
  self->priv->send_cb_user_data_free_func = user_data_free_func;
}

void
evd_jsonrpc_transport_error (EvdJsonrpc *self,
                             guint       invocation_id,
                             GError     *error)
{
  InvocationData *inv_data;
  gchar *id_st;

  g_return_if_fail (EVD_IS_JSONRPC (self));
  g_return_if_fail (error != NULL);

  id_st = g_strdup_printf ("%u", invocation_id);

  inv_data = g_hash_table_lookup (self->priv->invocations, id_st);
  if (inv_data == NULL)
    {
      /* @TODO: do proper logging */
      g_debug ("Transport error for unknown invocation id");
      g_free (id_st);

      return;
    }

  if (inv_data->result != NULL)
    {
      GSimpleAsyncResult *res = inv_data->result;

      g_simple_async_result_set_from_error (res, error);

      g_simple_async_result_complete_in_idle (res);
    }
  else
    {
      /* In case of a remote call, there is nothing we can do about a transport
         error. We can only hope for the remote endpoint to timeout. */
    }

  g_hash_table_remove (self->priv->invocations, id_st);

  g_free (id_st);
}

/**
 * evd_jsonrpc_call_method:
 * @params: (type Json.Node):
 **/
void
evd_jsonrpc_call_method (EvdJsonrpc          *self,
                         const gchar         *method_name,
                         JsonNode            *params,
                         gpointer             context,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  GSimpleAsyncResult *res;
  gchar *msg;
  guint id;
  gchar *id_st;
  JsonNode *id_node;
  InvocationData *inv_data;

  g_return_if_fail (EVD_IS_JSONRPC (self));
  g_return_if_fail (method_name != NULL);

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_jsonrpc_call_method);

  if ((context == NULL || ! EVD_IS_PEER (context)) &&
      self->priv->send_cb == NULL)
    {
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_CLOSED,
                                       "Failed to call method, no transport associated");
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
      return;
    }

  self->priv->invocation_counter++;
  id = self->priv->invocation_counter;
  id_st = g_strdup_printf ("%u", id);

  inv_data = g_slice_new0 (InvocationData);
  inv_data->result = res;
  inv_data->context = context;

  g_hash_table_insert (self->priv->invocations, id_st, inv_data);

  id_node = json_node_new (JSON_NODE_VALUE);
  json_node_set_string (id_node, id_st);

  msg = evd_jsonrpc_build_message (self,
                                   TRUE,
                                   method_name,
                                   id_node,
                                   params,
                                   NULL);

  json_node_free (id_node);

  evd_jsonrpc_transport_write (self,
                               msg,
                               context,
                               id);

  g_free (msg);
}

/**
 * evd_jsonrpc_call_method_finish:
 * @result_json: (out) (allow-none):
 * @error_json: (out) (allow-none):
 * @error: (out) (allow-none):
 *
 **/
gboolean
evd_jsonrpc_call_method_finish (EvdJsonrpc    *self,
                                GAsyncResult  *result,
                                JsonNode     **result_json,
                                JsonNode     **error_json,
                                GError       **error)
{
  GSimpleAsyncResult *res;

  g_return_val_if_fail (EVD_IS_JSONRPC (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_jsonrpc_call_method),
                        FALSE);

  res = G_SIMPLE_ASYNC_RESULT (result);

  if (! g_simple_async_result_propagate_error (res, error))
    {
      MethodResponse *data;

      data = g_simple_async_result_get_op_res_gpointer (res);

      if (result_json != NULL)
        *result_json = data->result;
      data->result = NULL;

      if (error_json != NULL)
        *error_json = data->error;
      data->error = NULL;

      return TRUE;
    }
  else
      return FALSE;
}

gboolean
evd_jsonrpc_transport_read (EvdJsonrpc   *self,
                            const gchar  *buffer,
                            gsize         size,
                            gpointer      context,
                            GError      **error)
{
  gboolean result;

  g_return_val_if_fail (EVD_IS_JSONRPC (self), FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);

  self->priv->context = context;
  result = evd_json_filter_feed_len (self->priv->json_filter,
                                     buffer,
                                     size,
                                     error);
  self->priv->context = NULL;

  return result;
}

gboolean
evd_jsonrpc_transport_receive (EvdJsonrpc    *self,
                               const gchar   *message,
                               gpointer       context,
                               guint          invocation_id,
                               GError       **error)
{
  gsize size;
  GError *_error = NULL;
  gboolean result = TRUE;

  g_return_val_if_fail (EVD_IS_JSONRPC (self), FALSE);

  size = strlen (message);

  if (message == NULL || size == 0)
    return TRUE;

  self->priv->context = context;

  if (! evd_json_filter_feed_len (self->priv->json_filter,
                                  message,
                                  size,
                                  &_error))
    {
      evd_jsonrpc_transport_error (self, invocation_id, _error);
      g_propagate_error (error, _error);

      result = FALSE;
    }

  self->priv->context = NULL;

  return result;
}

/**
 * evd_jsonrpc_set_method_call_callback:
 * @callback: (scope notified):
 * @user_data:
 *
 **/
void
evd_jsonrpc_set_method_call_callback (EvdJsonrpc             *self,
                                      EvdJsonrpcMethodCallCb  callback,
                                      gpointer                user_data)
{
  g_return_if_fail (EVD_IS_JSONRPC (self));

  self->priv->method_call_cb = callback;
  self->priv->method_call_cb_user_data = user_data;
}

/**
 * evd_jsonrpc_respond:
 * @result: (allow-none):
 * @context: (allow-none) (type GObject):
 * @error: (allow-none):
 *
 **/
gboolean
evd_jsonrpc_respond (EvdJsonrpc  *self,
                     guint        invocation_id,
                     JsonNode    *result,
                     gpointer     context,
                     GError     **error)
{
  return evd_jsonrpc_respond_full (self,
                                   invocation_id,
                                   result,
                                   NULL,
                                   error);
}

gboolean
evd_jsonrpc_respond_error (EvdJsonrpc  *self,
                           guint        invocation_id,
                           JsonNode    *json_error,
                           gpointer     context,
                           GError     **error)
{
  return evd_jsonrpc_respond_full (self,
                                   invocation_id,
                                   NULL,
                                   json_error,
                                   error);
}

void
evd_jsonrpc_use_transport (EvdJsonrpc *self, EvdTransport *transport)
{
  g_return_if_fail (EVD_IS_JSONRPC (self));
  g_return_if_fail (EVD_IS_TRANSPORT (transport));

  g_signal_connect (transport,
                    "receive",
                    G_CALLBACK (evd_jsonrpc_transport_on_receive),
                    self);

  g_object_weak_ref (G_OBJECT (transport),
                     evd_jsonrpc_transport_destroyed,
                     self);

  g_hash_table_insert (self->priv->transports, transport, transport);
}

void
evd_jsonrpc_unuse_transport (EvdJsonrpc *self, EvdTransport *transport)
{
  g_return_if_fail (EVD_IS_JSONRPC (self));
  g_return_if_fail (EVD_IS_TRANSPORT (transport));

  if (g_hash_table_remove (self->priv->transports, transport))
    {
      g_object_weak_unref (G_OBJECT (transport),
                           evd_jsonrpc_transport_destroyed,
                           self);

      g_signal_handlers_disconnect_by_func (transport,
                                            evd_jsonrpc_transport_on_receive,
                                            self);
    }
}

/**
 * evd_jsonrpc_send_notification:
 * @params: (allow-none):
 * @context: (allow-none):
 * @error: (allow-none):
 *
 * Returns:
 **/
gboolean
evd_jsonrpc_send_notification (EvdJsonrpc   *self,
                               const gchar  *notification_name,
                               JsonNode     *params,
                               gpointer      context,
                               GError      **error)
{
  gchar *msg;

  g_return_val_if_fail (EVD_IS_JSONRPC (self), FALSE);
  g_return_val_if_fail (notification_name != NULL, FALSE);

  if ((context == NULL || ! EVD_IS_PEER (context)) &&
      self->priv->send_cb == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_CLOSED,
                   "Failed to send notificaton, no transport associated");
      return FALSE;
    }

  msg = evd_jsonrpc_build_message (self,
                                   TRUE,
                                   notification_name,
                                   NULL,
                                   params,
                                   NULL);

  evd_jsonrpc_transport_write (self, msg, context, 0);

  g_free (msg);

  return TRUE;
}
