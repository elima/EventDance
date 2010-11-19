/*
 * evd-dbus-agent.c
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

#include "evd-dbus-agent.h"

#include "evd-utils.h"

#define OBJECT_DATA_KEY "org.eventdance.lib.DBusAgent.OBJ_DATA"

typedef struct
{
  GObject *obj;
  GHashTable *conns;
  guint conn_counter;
  GHashTable *proxies;
  guint proxy_counter;
  GHashTable *reg_objs;
  GHashTable *reg_objs_by_id;
  GHashTable *addr_aliases;
  gchar *tmp_addr;
  gboolean reuse_connection;
  EvdDBusAgentVTable *vtable;
  gpointer vtable_user_data;
} ObjectData;

typedef struct
{
  GDBusProxy *proxy;
  EvdDBusAgentProxyPropertiesChangedCb props_changed_cb;
  gpointer props_changed_user_data;
  EvdDBusAgentProxySignalCb signal_cb;
  gpointer signal_user_data;
} ProxyData;

typedef struct
{
  gchar *reg_str_id;
  guint reg_id;
  guint64 serial;
  GHashTable *invocations;
} RegObjData;

static void     evd_dbus_agent_on_object_connection_closed   (GDBusConnection *connection,
                                                              gboolean         remote_peer_vanished,
                                                              GError          *error,
                                                              gpointer         user_data);

static void     evd_dbus_agent_unbind_connection_from_object (ObjectData      *data,
                                                              GDBusConnection *conn);

static void     evd_dbus_agent_on_proxy_signal               (GDBusProxy *proxy,
                                                              gchar      *sender_name,
                                                              gchar      *signal_name,
                                                              GVariant   *parameters,
                                                              gpointer    user_data);
static void     evd_dbus_agent_on_proxy_properties_changed   (GDBusProxy *proxy,
                                                              GVariant   *changed_properties,
                                                              GStrv      *invalidated_properties,
                                                              gpointer    user_data);

static void     evd_dbus_agent_method_called                 (GDBusConnection *connection,
                                                              const gchar *sender,
                                                              const gchar *object_path,
                                                              const gchar *interface_name,
                                                              const gchar *method_name,
                                                              GVariant *parameters,
                                                              GDBusMethodInvocation *invocation,
                                                              gpointer user_data);

static const GDBusInterfaceVTable evd_dbus_agent_iface_vtable =
  {
    evd_dbus_agent_method_called,
    NULL,
    NULL
  };

static ObjectData *
evd_dbus_agent_get_object_data (GObject *obj)
{
  return (ObjectData *) g_object_get_data (G_OBJECT (obj),
                                           OBJECT_DATA_KEY);
}

static void
evd_dbus_agent_free_proxy_data (gpointer data)
{
  ProxyData *proxy_data = (ProxyData *) data;

  g_object_unref (proxy_data->proxy);

  g_slice_free (ProxyData, proxy_data);
}

static void
evd_dbus_agent_free_reg_obj_data (gpointer data)
{
  RegObjData *reg_obj_data = (RegObjData *) data;

  g_hash_table_destroy (reg_obj_data->invocations);

  g_slice_free (RegObjData, reg_obj_data);
}

static gboolean
evd_dbus_agent_remove_object_connection (gpointer key,
                                         gpointer value,
                                         gpointer user_data)
{
  GDBusConnection *conn = G_DBUS_CONNECTION (value);

  evd_dbus_agent_unbind_connection_from_object (user_data, conn);

  return TRUE;
}

static void
evd_dbus_agent_on_object_destroyed (gpointer  user_data,
                                    GObject  *where_the_object_was)
{
  ObjectData *data = (ObjectData *) user_data;

  g_assert (data != NULL);

  g_hash_table_foreach_remove (data->conns,
                               evd_dbus_agent_remove_object_connection,
                               data);
  g_hash_table_unref (data->conns);

  g_hash_table_destroy (data->proxies);
  g_hash_table_destroy (data->addr_aliases);
  g_hash_table_destroy (data->reg_objs);
  g_hash_table_destroy (data->reg_objs_by_id);

  g_slice_free (ObjectData, data);
}

static ObjectData *
evd_dbus_agent_setup_object_data (GObject *obj)
{
  ObjectData *data;

  data = g_slice_new0 (ObjectData);

  g_object_weak_ref (obj, evd_dbus_agent_on_object_destroyed, data);
  g_object_set_data (G_OBJECT (obj), OBJECT_DATA_KEY, data);

  data->obj = obj;

  data->conns = g_hash_table_new_full (g_int_hash,
                                       g_int_equal,
                                       g_free,
                                       NULL);
  data->conn_counter = 0;

  data->proxies = g_hash_table_new_full (g_int_hash,
                                         g_int_equal,
                                         g_free,
                                         evd_dbus_agent_free_proxy_data);
  data->proxy_counter = 0;

  data->reg_objs = g_hash_table_new_full (g_str_hash,
                                          g_str_equal,
                                          g_free,
                                          evd_dbus_agent_free_reg_obj_data);
  data->reg_objs_by_id = g_hash_table_new_full (g_int_hash,
                                                g_int_equal,
                                                NULL,
                                                NULL);

  data->addr_aliases = g_hash_table_new_full (g_str_hash,
                                              g_str_equal,
                                              g_free,
                                              g_free);

  return data;
}

static void
evd_dbus_agent_on_object_connection_closed (GDBusConnection *conn,
                                            gboolean         remote_peer_vanished,
                                            GError          *error,
                                            gpointer         user_data)
{
  ObjectData *data;
  GHashTableIter iter;
  gpointer key, value;

  data = (ObjectData *) user_data;

  g_hash_table_iter_init (&iter, data->conns);
  while (g_hash_table_iter_next (&iter, &key, &value))
    if (value == conn)
      {
        evd_dbus_agent_unbind_connection_from_object (data,
                                                       G_DBUS_CONNECTION (conn));

        break;
      }

  g_hash_table_remove (data->conns, key);

  /* @TODO: notify object that one of its connections has closed */
}

static void
evd_dbus_agent_unbind_connection_from_object (ObjectData      *data,
                                              GDBusConnection *conn)
{
  g_signal_handlers_disconnect_by_func (conn,
                       G_CALLBACK (evd_dbus_agent_on_object_connection_closed),
                       data);

  g_object_unref (conn);
}

static guint *
evd_dbus_agent_bind_connection_to_object (ObjectData      *data,
                                          GDBusConnection *conn)
{
  guint *conn_id;

  data->conn_counter ++;

  conn_id = g_new (guint, 1);
  *conn_id = data->conn_counter;

  g_hash_table_insert (data->conns, conn_id, conn);

  g_signal_connect (conn,
                    "closed",
                    G_CALLBACK (evd_dbus_agent_on_object_connection_closed),
                    data);

  g_object_ref (conn);

  return conn_id;
}

static void
evd_dbus_agent_on_new_dbus_connection (GObject      *obj,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  GDBusConnection *dbus_conn;
  GError *error = NULL;
  GSimpleAsyncResult *result;
  ObjectData *data;

  result = G_SIMPLE_ASYNC_RESULT (user_data);

  data =
    (ObjectData *) g_simple_async_result_get_op_res_gpointer (result);

  if ( (dbus_conn = g_dbus_connection_new_for_address_finish (res,
                                                              &error)) == NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }
  else
    {
      guint *conn_id;

      conn_id = evd_dbus_agent_bind_connection_to_object (data, dbus_conn);

      if (data->reuse_connection)
        {
          /* @TODO: cache the connection globally */
        }
      else
        {
          g_object_unref (dbus_conn);
        }

      g_simple_async_result_set_op_res_gpointer (result, conn_id, NULL);
    }

  g_free (data->tmp_addr);

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static guint *
evd_dbus_agent_bind_proxy_to_object (ObjectData *data,
                                     GDBusProxy *proxy)
{
  guint *proxy_id;
  ProxyData *proxy_data;

  data->proxy_counter++;

  proxy_id = g_new (guint, 1);
  *proxy_id = data->proxy_counter;

  proxy_data = g_slice_new0 (ProxyData);

  proxy_data->proxy = proxy;

  g_hash_table_insert (data->proxies, proxy_id, proxy_data);

  return proxy_id;
}

static void
evd_dbus_agent_unbind_proxy_from_object (ObjectData *data,
                                         ProxyData  *proxy_data)
{
  if (proxy_data->signal_cb != NULL)
    {
      g_signal_handlers_disconnect_by_func (proxy_data->proxy,
                                            evd_dbus_agent_on_proxy_signal,
                                            data);
    }

  if (proxy_data->props_changed_cb != NULL)
    {
      g_signal_handlers_disconnect_by_func (proxy_data->proxy,
                                            evd_dbus_agent_on_proxy_properties_changed,
                                            data);
    }
}

static void
evd_dbus_agent_on_new_dbus_proxy (GObject      *obj,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  GSimpleAsyncResult *result;
  GDBusProxy *proxy;
  GError *error = NULL;

  result = G_SIMPLE_ASYNC_RESULT (user_data);

  if ( (proxy = g_dbus_proxy_new_finish (res, &error)) != NULL)
    {
      ObjectData *data;
      guint *proxy_id;

      data = (ObjectData *)
        g_simple_async_result_get_op_res_gpointer (result);

      proxy_id = evd_dbus_agent_bind_proxy_to_object (data, proxy);

      g_simple_async_result_set_op_res_gpointer (result, proxy_id, NULL);
    }
  else
    {
      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static ProxyData *
evd_dbus_agent_find_proxy_data_by_proxy (ObjectData *data,
                                         GDBusProxy *proxy,
                                         guint      *proxy_id)
{
  ProxyData *proxy_data;
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, data->proxies);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      proxy_data = (ProxyData *) value;

      if (proxy_data->proxy == proxy)
        {
          *proxy_id = * (guint *) key;
          return proxy_data;
        }
    }

  return NULL;
}

static void
evd_dbus_agent_on_proxy_signal (GDBusProxy *proxy,
                                gchar      *sender_name,
                                gchar      *signal_name,
                                GVariant   *parameters,
                                gpointer    user_data)
{
  ObjectData *data = (ObjectData *) user_data;
  ProxyData *proxy_data;
  guint proxy_id = 0;

  proxy_data = evd_dbus_agent_find_proxy_data_by_proxy (data,
                                                        proxy,
                                                        &proxy_id);
  g_assert (proxy_data != NULL);

  if (proxy_data->signal_cb != NULL)
    {
      proxy_data->signal_cb (data->obj,
                             proxy_id,
                             signal_name,
                             parameters,
                             proxy_data->signal_user_data);
    }
}

static void
evd_dbus_agent_on_proxy_properties_changed (GDBusProxy *proxy,
                                            GVariant   *changed_properties,
                                            GStrv      *invalidated_properties,
                                            gpointer    user_data)
{
  ObjectData *data = (ObjectData *) user_data;
  ProxyData *proxy_data;
  guint proxy_id = 0;

  proxy_data = evd_dbus_agent_find_proxy_data_by_proxy (data,
                                                        proxy,
                                                        &proxy_id);
  g_assert (proxy_data != NULL);

  if (proxy_data->props_changed_cb != NULL)
    {
      proxy_data->props_changed_cb (data->obj,
                                    proxy_id,
                                    changed_properties,
                                    invalidated_properties,
                                    proxy_data->props_changed_user_data);
    }
}

static void
evd_dbus_agent_method_called (GDBusConnection *connection,
                              const gchar *sender,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *method_name,
                              GVariant *parameters,
                              GDBusMethodInvocation *invocation,
                              gpointer user_data)
{
  GObject *obj = G_OBJECT (user_data);
  ObjectData *data;
  gchar *key;
  RegObjData *reg_obj_data;

  data = evd_dbus_agent_get_object_data (obj);
  g_assert (data != NULL);

  key = g_strdup_printf ("%s<%s>", object_path, interface_name);
  reg_obj_data = g_hash_table_lookup (data->reg_objs, key);
  g_free (key);

  g_assert (reg_obj_data != NULL);

  if (data->vtable && data->vtable->method_call != NULL)
    {
      /* cache the method invocation object, bound to the serial */
      reg_obj_data->serial++;
      g_hash_table_insert (reg_obj_data->invocations,
                           (gint64 *) &reg_obj_data->serial,
                           invocation);

      data->vtable->method_call (obj,
                                 sender,
                                 method_name,
                                 reg_obj_data->reg_id,
                                 parameters,
                                 reg_obj_data->serial,
                                 data->vtable_user_data);
    }
  else
    {
      /* return error to invocation, no way to handle it */
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     G_IO_ERROR,
                                                     G_IO_ERROR_NOT_SUPPORTED,
                                                     "Method not handled");
    }
}

/* public methods */

void
evd_dbus_agent_create_address_alias (GObject     *object,
                                     const gchar *address,
                                     const gchar *alias)
{
  ObjectData *data;

  g_return_if_fail (G_IS_OBJECT (object));
  g_return_if_fail (address != NULL);

  data = evd_dbus_agent_get_object_data (object);
  if (data == NULL)
    data = evd_dbus_agent_setup_object_data (object);

  g_hash_table_insert (data->addr_aliases,
                       g_strdup (alias),
                       g_strdup (address));
}

void
evd_dbus_agent_new_connection (GObject             *object,
                               const gchar         *address,
                               gboolean             reuse,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  GDBusConnection *dbus_conn;
  GSimpleAsyncResult *res;
  ObjectData *data;
  gchar *addr;

  g_return_if_fail (G_IS_OBJECT (object));
  g_return_if_fail (address != NULL);

  data = evd_dbus_agent_get_object_data (object);
  if (data == NULL)
    data = evd_dbus_agent_setup_object_data (object);

  res = g_simple_async_result_new (object,
                                   callback,
                                   user_data,
                                   evd_dbus_agent_new_connection);

  g_simple_async_result_set_op_res_gpointer (res, data, NULL);

  /* if 'address' is an alias, dereference it */
  if ( (addr = g_hash_table_lookup (data->addr_aliases, address)) == NULL)
    addr = g_strdup (address);
  else
    addr = g_strdup (addr);

  if (reuse)
    {
      /* @TODO: lookup the connection in global cache. By now assume no cache */
      dbus_conn = NULL;

      if (dbus_conn != NULL)
        {
          guint *conn_id;

          conn_id = evd_dbus_agent_bind_connection_to_object (data, dbus_conn);

          g_simple_async_result_set_op_res_gpointer (res,
                                                     conn_id,
                                                     g_free);

          g_simple_async_result_complete_in_idle (res);
          g_object_unref (res);

          return;
        }
    }

  data->tmp_addr = addr;

  g_dbus_connection_new_for_address (addr,
                                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION |
                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                NULL,
                                cancellable,
                                evd_dbus_agent_on_new_dbus_connection,
                                res);
}

guint
evd_dbus_agent_new_connection_finish (GObject       *object,
                                      GAsyncResult  *result,
                                      GError       **error)
{
  g_return_val_if_fail (G_IS_OBJECT (object), 0);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                object,
                                                evd_dbus_agent_new_connection),
                        0);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      guint *conn_id;

      conn_id =
        g_simple_async_result_get_op_res_gpointer
        (G_SIMPLE_ASYNC_RESULT (result));

      return *conn_id;
    }
  else
    {
      return 0;
    }
}

gboolean
evd_dbus_agent_close_connection (GObject  *object,
                                 guint     connection_id,
                                 GError  **error)
{
  GDBusConnection *conn;

  if ( (conn = evd_dbus_agent_get_connection (object,
                                              connection_id,
                                              error)) != NULL)
    {
      ObjectData *data;

      data = evd_dbus_agent_get_object_data (object);

      /* @TODO: Close the connection if there is no other object using it
         (implement usage ref-count on connection). By now, just remove the
         reference in object data */
      evd_dbus_agent_unbind_connection_from_object (data, conn);

      g_hash_table_remove (data->conns, &connection_id);

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

/**
 * evd_dbus_agent_get_connection:
 *
 * Returns: (transfer none):
 **/
GDBusConnection *
evd_dbus_agent_get_connection (GObject  *obj,
                               guint     connection_id,
                               GError  **error)
{
  ObjectData *data;
  GDBusConnection *conn;

  data = evd_dbus_agent_get_object_data (obj);
  if (data == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Object is invalid");

      return NULL;
    }

  conn = G_DBUS_CONNECTION (g_hash_table_lookup (data->conns, &connection_id));
  if (conn != NULL)
    {
      return conn;
    }
  else
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Object doesn't hold specified connection");

      return NULL;
    }
}

void
evd_dbus_agent_new_proxy (GObject             *object,
                          guint                connection_id,
                          GDBusProxyFlags      flags,
                          const gchar         *name,
                          const gchar         *object_path,
                          const gchar         *iface_name,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
  GDBusConnection *conn;
  GSimpleAsyncResult *res;
  GError *error = NULL;

  g_return_if_fail (G_IS_OBJECT (object));
  g_return_if_fail (connection_id > 0);

  res = g_simple_async_result_new (object,
                                   callback,
                                   user_data,
                                   evd_dbus_agent_new_proxy);

  if ( (conn = evd_dbus_agent_get_connection (object,
                                              connection_id,
                                              &error)) != NULL)
    {
      ObjectData *data;

      data = evd_dbus_agent_get_object_data (object);

      g_simple_async_result_set_op_res_gpointer (res, data, NULL);

      g_dbus_proxy_new (conn,
                        flags,
                        NULL,
                        name,
                        object_path,
                        iface_name,
                        cancellable,
                        evd_dbus_agent_on_new_dbus_proxy,
                        res);
    }
  else
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
    }
}

guint
evd_dbus_agent_new_proxy_finish (GObject       *object,
                                 GAsyncResult  *result,
                                 GError       **error)
{
  g_return_val_if_fail (G_IS_OBJECT (object), 0);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                object,
                                                evd_dbus_agent_new_proxy),
                        0);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      guint *proxy_id;

      proxy_id =
        g_simple_async_result_get_op_res_gpointer
        (G_SIMPLE_ASYNC_RESULT (result));

      return *proxy_id;
    }
  else
    {
      return 0;
    }
}

gboolean
evd_dbus_agent_close_proxy (GObject  *object,
                            guint     proxy_id,
                            GError  **error)
{
  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (proxy_id > 0, FALSE);

  if (evd_dbus_agent_get_proxy (object, proxy_id, error) != NULL)
    {
      ObjectData *data;
      ProxyData *proxy_data;

      data = evd_dbus_agent_get_object_data (object);
      proxy_data = (ProxyData *) g_hash_table_lookup (data->proxies,
                                                      &proxy_id);

      evd_dbus_agent_unbind_proxy_from_object (data, proxy_data);

      g_hash_table_remove (data->proxies, &proxy_id);

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

/**
 * evd_dbus_agent_get_proxy:
 *
 * Returns: (transfer none):
 **/
GDBusProxy *
evd_dbus_agent_get_proxy (GObject  *object,
                          guint     proxy_id,
                          GError  **error)
{
  ObjectData *data;
  ProxyData *proxy_data;

  g_return_val_if_fail (G_IS_OBJECT (object), NULL);
  g_return_val_if_fail (proxy_id > 0, NULL);

  data = evd_dbus_agent_get_object_data (object);
  if (data == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Object is invalid");

      return NULL;
    }

  proxy_data = (ProxyData *) (g_hash_table_lookup (data->proxies, &proxy_id));
  if (proxy_data != NULL)
    {
      return proxy_data->proxy;
    }
  else
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Object doesn't hold specified proxy");

      return NULL;
    }
}

gboolean
evd_dbus_agent_watch_proxy_signals (GObject                    *object,
                                    guint                       proxy_id,
                                    EvdDBusAgentProxySignalCb   callback,
                                    gpointer                    user_data,
                                    GError                    **error)
{
  ObjectData *data;
  ProxyData *proxy_data;

  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (proxy_id > 0, FALSE);

  if (evd_dbus_agent_get_proxy (object, proxy_id, error) == NULL)
    return FALSE;

  data = evd_dbus_agent_get_object_data (object);
  proxy_data = (ProxyData *) (g_hash_table_lookup (data->proxies, &proxy_id));

  if (proxy_data->signal_cb != NULL && callback == NULL)
    {
      g_signal_handlers_disconnect_by_func (proxy_data->proxy,
                                            evd_dbus_agent_on_proxy_signal,
                                            data);
    }

  proxy_data->signal_cb = callback;
  proxy_data->signal_user_data = user_data;

  if (callback != NULL)
    {
      g_signal_connect (proxy_data->proxy,
                        "g-signal",
                        G_CALLBACK (evd_dbus_agent_on_proxy_signal),
                        data);
    }

  return TRUE;
}

void
evd_dbus_agent_set_object_vtable (GObject             *object,
                                  EvdDBusAgentVTable  *vtable,
                                  gpointer             user_data)
{
  ObjectData *data;

  g_return_if_fail (G_IS_OBJECT (object));

  data = evd_dbus_agent_get_object_data (object);
  if (data == NULL)
    data = evd_dbus_agent_setup_object_data (object);

  data->vtable = vtable;
  data->vtable_user_data = user_data;
}

gboolean
evd_dbus_agent_watch_proxy_property_changes (GObject                               *object,
                                             guint                                  proxy_id,
                                             EvdDBusAgentProxyPropertiesChangedCb   callback,
                                             gpointer                               user_data,
                                             GError                               **error)
{
  ObjectData *data;
  ProxyData *proxy_data;

  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (proxy_id > 0, FALSE);

  if (evd_dbus_agent_get_proxy (object, proxy_id, error) == NULL)
    return FALSE;

  data = evd_dbus_agent_get_object_data (object);
  proxy_data = (ProxyData *) (g_hash_table_lookup (data->proxies, &proxy_id));

  if (proxy_data->props_changed_cb != NULL && callback == NULL)
    {
      g_signal_handlers_disconnect_by_func (proxy_data->proxy,
                                            evd_dbus_agent_on_proxy_properties_changed,
                                            data);
    }

  proxy_data->props_changed_cb = callback;
  proxy_data->props_changed_user_data = user_data;

  if (callback != NULL)
    {
      g_signal_connect (proxy_data->proxy,
                        "g-properties-changed",
                        G_CALLBACK (evd_dbus_agent_on_proxy_properties_changed),
                        data);
    }

  return TRUE;
}

guint
evd_dbus_agent_register_object (GObject             *object,
                                guint                connection_id,
                                const gchar         *object_path,
                                GDBusInterfaceInfo  *interface_info,
                                GError             **error)
{
  GDBusConnection *dbus_conn;
  guint reg_id = 0;

  g_return_val_if_fail (G_IS_OBJECT (object), 0);
  g_return_val_if_fail (connection_id > 0, 0);
  g_return_val_if_fail (object_path != NULL, 0);
  g_return_val_if_fail (interface_info != NULL, 0);

  if ( (dbus_conn = evd_dbus_agent_get_connection (object,
                                                   connection_id,
                                                   error)) == NULL)
    {
      return 0;
    }

  reg_id = g_dbus_connection_register_object (dbus_conn,
                                              object_path,
                                              interface_info,
                                              &evd_dbus_agent_iface_vtable,
                                              object,
                                              NULL,
                                              error);
  if (reg_id > 0)
    {
      gchar *key;
      ObjectData *data;
      RegObjData *reg_obj_data;

      data = evd_dbus_agent_get_object_data (object);
      g_assert (data != NULL);

      key = g_strdup_printf ("%s<%s>", object_path, interface_info->name);

      reg_obj_data = g_slice_new (RegObjData);
      reg_obj_data->reg_id = reg_id;
      reg_obj_data->serial = 0;
      reg_obj_data->reg_str_id = key;
      reg_obj_data->invocations = g_hash_table_new_full (g_int64_hash,
                                                         g_int64_equal,
                                                         NULL,
                                                         NULL /* @TODO: should not be g_object_unref? */);

      g_hash_table_insert (data->reg_objs, key, reg_obj_data);
      g_hash_table_insert (data->reg_objs_by_id, &reg_id, reg_obj_data);
    }

  return reg_id;
}

gboolean
evd_dbus_agent_unregister_object (GObject  *object,
                                  guint     connection_id,
                                  guint     registration_id,
                                  GError  **error)
{
  GDBusConnection *dbus_conn;
  ObjectData *data;
  RegObjData *reg_obj_data;
  gint reg_id_key;

  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (connection_id > 0, FALSE);
  g_return_val_if_fail (registration_id > 0, FALSE);

  if ( (dbus_conn = evd_dbus_agent_get_connection (object,
                                                   connection_id,
                                                   error)) == NULL)
    {
      return FALSE;
    }

  g_dbus_connection_unregister_object (dbus_conn, registration_id);

  data = evd_dbus_agent_get_object_data (object);
  g_assert (data != NULL);

  reg_id_key = registration_id;

  reg_obj_data = g_hash_table_lookup (data->reg_objs_by_id, &reg_id_key);
  if (reg_obj_data == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Object registration id '%u' is invalid",
                   registration_id);
      return FALSE;
    }

  g_hash_table_remove (data->reg_objs_by_id, &registration_id);
  g_hash_table_remove (data->reg_objs, reg_obj_data->reg_str_id);

  return TRUE;
}
