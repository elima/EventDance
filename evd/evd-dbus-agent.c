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
  GHashTable *owned_names;
  GHashTable *reg_objs;
  GHashTable *reg_objs_by_id;
  GHashTable *addr_aliases;
  EvdDBusAgentVTable *vtable;
  gpointer vtable_user_data;
} ObjectData;

typedef struct
{
  GDBusConnection *conn;
  gint ref_count;
  gboolean reuse;
  gchar *addr;
} ConnData;

typedef struct
{
  ObjectData *obj_data;
  ConnData *conn_data;
} ObjConnData;

typedef struct
{
  ObjectData *obj_data;
  guint32 conn_id;
  guint32 proxy_id;
  GDBusProxy *proxy;
  GSimpleAsyncResult *async_res;
} ProxyData;

typedef struct
{
  ObjectData *obj_data;
  guint32 conn_id;
  guint owner_id;
  GDBusConnection *dbus_conn;
} NameOwnerData;

typedef struct
{
  ObjectData *obj_data;
  guint32 conn_id;
  gchar *reg_str_id;
  GDBusConnection *dbus_conn;
  gchar *obj_path;
  GDBusInterfaceInfo *iface_info;
  guint reg_id;
  guint64 serial;
  GHashTable *invocations;
} RegObjData;

static GHashTable *conn_cache = NULL;

static void     evd_dbus_agent_on_object_connection_closed   (GDBusConnection *connection,
                                                              gboolean         remote_peer_vanished,
                                                              GError          *error,
                                                              gpointer         user_data);

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

static gboolean evd_dbus_agent_foreach_remove_proxy          (gpointer key,
                                                              gpointer value,
                                                              gpointer user_data);
static gboolean evd_dbus_agent_foreach_remove_owned_names    (gpointer key,
                                                              gpointer value,
                                                              gpointer user_data);
static gboolean evd_dbus_agent_foreach_remove_reg_obj        (gpointer key,
                                                              gpointer value,
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

static ConnData *
evd_dbus_agent_conn_data_new (const gchar *addr, gboolean reuse)
{
  ConnData *conn_data;

  conn_data = g_slice_new (ConnData);
  conn_data->conn = NULL;
  conn_data->ref_count = 0;
  conn_data->addr = g_strdup (addr);
  conn_data->reuse = reuse;

  return conn_data;
}

static void
evd_dbus_agent_conn_data_ref (ConnData *conn_data)
{
  conn_data->ref_count++;
}

static void
evd_dbus_agent_conn_data_unref (ConnData *conn_data)
{
  conn_data->ref_count--;
  if (conn_data->ref_count <= 0)
    {
      if (conn_cache != NULL)
        g_hash_table_remove (conn_cache, conn_data->addr);

      if (conn_data->conn != NULL)
        {
          g_dbus_connection_close (conn_data->conn, NULL, NULL, NULL);
          g_object_unref (conn_data->conn);
        }

      g_free (conn_data->addr);

      g_slice_free (ConnData, conn_data);
    }
}

static void
evd_dbus_agent_free_obj_conn_data (gpointer data)
{
  ObjectData *obj_data;
  ObjConnData *obj_conn_data = (ObjConnData *) data;

  obj_data = obj_conn_data->obj_data;

  /* remove all proxies created over this connection */
  g_hash_table_foreach_remove (obj_data->proxies,
                               evd_dbus_agent_foreach_remove_proxy,
                               obj_conn_data->conn_data->conn);

  /* unown all names owned over this connection */
  g_hash_table_foreach_remove (obj_data->owned_names,
                               evd_dbus_agent_foreach_remove_owned_names,
                               obj_conn_data->conn_data->conn);

  /* remove all objects registered over this connection */
  g_hash_table_foreach_remove (obj_data->reg_objs_by_id,
                               evd_dbus_agent_foreach_remove_reg_obj,
                               obj_conn_data->conn_data->conn);

  g_signal_handlers_disconnect_by_func (obj_conn_data->conn_data->conn,
                       G_CALLBACK (evd_dbus_agent_on_object_connection_closed),
                       obj_data);

  evd_dbus_agent_conn_data_unref (obj_conn_data->conn_data);

  g_slice_free (ObjConnData, obj_conn_data);
}

static void
evd_dbus_agent_free_proxy_data (gpointer data)
{
  ProxyData *proxy_data = (ProxyData *) data;

  if (proxy_data->proxy != NULL)
    {
      g_signal_handlers_disconnect_by_func (proxy_data->proxy,
                                            evd_dbus_agent_on_proxy_signal,
                                            proxy_data);
      g_signal_handlers_disconnect_by_func (proxy_data->proxy,
                                            evd_dbus_agent_on_proxy_properties_changed,
                                            proxy_data);

      g_object_unref (proxy_data->proxy);
    }

  g_slice_free (ProxyData, proxy_data);
}

static void
evd_dbus_agent_free_name_owner_data (gpointer data)
{
  NameOwnerData *owner_data = (NameOwnerData *) data;

  g_bus_unown_name (owner_data->owner_id);

  g_slice_free (NameOwnerData, owner_data);
}

static void
evd_dbus_agent_free_reg_obj_data (gpointer data)
{
  ObjectData *obj_data;
  RegObjData *reg_obj_data = (RegObjData *) data;

  obj_data = reg_obj_data->obj_data;

  g_dbus_connection_unregister_object (reg_obj_data->dbus_conn,
                                       reg_obj_data->reg_id);

  g_hash_table_remove (obj_data->reg_objs, &reg_obj_data->reg_str_id);

  g_free (reg_obj_data->obj_path);
  g_dbus_interface_info_unref (reg_obj_data->iface_info);
  g_object_unref (reg_obj_data->dbus_conn);
  g_hash_table_destroy (reg_obj_data->invocations);

  g_slice_free (RegObjData, reg_obj_data);
}

static void
evd_dbus_agent_on_object_destroyed (gpointer  user_data,
                                    GObject  *where_the_object_was)
{
  ObjectData *data = (ObjectData *) user_data;

  g_assert (data != NULL);

  g_hash_table_destroy (data->conns);
  g_hash_table_destroy (data->proxies);
  g_hash_table_destroy (data->owned_names);
  g_hash_table_destroy (data->addr_aliases);
  g_hash_table_destroy (data->reg_objs_by_id);
  g_hash_table_destroy (data->reg_objs);

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
                                       evd_dbus_agent_free_obj_conn_data);
  data->conn_counter = 0;

  data->proxies = g_hash_table_new_full (g_int_hash,
                                         g_int_equal,
                                         NULL,
                                         evd_dbus_agent_free_proxy_data);
  data->proxy_counter = 0;

  data->owned_names = g_hash_table_new_full (g_int_hash,
                                             g_int_equal,
                                             NULL,
                                             evd_dbus_agent_free_name_owner_data);

  data->reg_objs = g_hash_table_new_full (g_str_hash,
                                          g_str_equal,
                                          g_free,
                                          NULL);
  data->reg_objs_by_id = g_hash_table_new_full (g_int_hash,
                                                g_int_equal,
                                                NULL,
                                                evd_dbus_agent_free_reg_obj_data);

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
  ObjectData *data = (ObjectData *) user_data;
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, data->conns);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      ConnData *conn_data;

      conn_data = (ConnData *) value;
      if (conn_data->conn == conn)
        {
          /* @TODO: notify object that connection dropped */

          g_hash_table_remove (data->conns, (guint *) key);

          break;
        }
    }
}

static ConnData *
evd_dbus_agent_search_conn_in_global_cache (const gchar *addr, gboolean reuse)
{
  if (conn_cache != NULL)
    return (ConnData *) g_hash_table_lookup (conn_cache, addr);
  else
    return NULL;
}

static void
evd_dbus_agent_cache_conn_in_global_cache (ConnData *conn_data)
{
  if (conn_cache == NULL)
    conn_cache = g_hash_table_new_full (g_str_hash,
                                        g_str_equal,
                                        NULL,
                                        NULL);
  g_hash_table_insert (conn_cache, conn_data->addr, conn_data);
}

static guint *
evd_dbus_agent_bind_connection_to_object (ObjectData  *obj_data,
                                          ObjConnData *obj_conn_data)
{
  guint *conn_id;

  obj_data->conn_counter ++;

  conn_id = g_new (guint, 1);
  *conn_id = obj_data->conn_counter;

  evd_dbus_agent_conn_data_ref (obj_conn_data->conn_data);

  g_hash_table_insert (obj_data->conns, conn_id, obj_conn_data);

  g_signal_connect (obj_conn_data->conn_data->conn,
                    "closed",
                    G_CALLBACK (evd_dbus_agent_on_object_connection_closed),
                    obj_data);

  g_object_ref (obj_conn_data->conn_data->conn);

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
  ObjectData *obj_data;
  ConnData *conn_data;
  ObjConnData *obj_conn_data;

  result = G_SIMPLE_ASYNC_RESULT (user_data);

  obj_conn_data =
    (ObjConnData *) g_simple_async_result_get_op_res_gpointer (result);
  obj_data = obj_conn_data->obj_data;
  conn_data = obj_conn_data->conn_data;

  if ( (dbus_conn = g_dbus_connection_new_for_address_finish (res,
                                                              &error)) == NULL)
    {
      evd_dbus_agent_conn_data_unref (conn_data);
      g_slice_free (ObjConnData, obj_conn_data);

      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }
  else
    {
      guint *conn_id;

      conn_data->conn = dbus_conn;

      conn_id = evd_dbus_agent_bind_connection_to_object (obj_data, obj_conn_data);

      if (conn_data->reuse)
        evd_dbus_agent_cache_conn_in_global_cache (conn_data);

      g_simple_async_result_set_op_res_gpointer (result, conn_id, NULL);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static void
evd_dbus_agent_on_new_dbus_proxy (GObject      *obj,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  GSimpleAsyncResult *result;
  GDBusProxy *proxy;
  GError *error = NULL;
  ProxyData *proxy_data;

  proxy_data = (ProxyData *) user_data;

  result = proxy_data->async_res;

  if ( (proxy = g_dbus_proxy_new_finish (res, &error)) != NULL)
    {
      ObjectData *obj_data;
      GDBusProxyFlags flags;
      guint *proxy_id;

      obj_data = proxy_data->obj_data;

      obj_data->proxy_counter++;

      proxy_data->proxy_id = obj_data->proxy_counter;
      proxy_data->proxy = proxy;

      g_hash_table_insert (obj_data->proxies,
                           &proxy_data->proxy_id,
                           proxy_data);

      proxy_id = g_new (guint, 1);
      *proxy_id = obj_data->proxy_counter;
      g_simple_async_result_set_op_res_gpointer (result,
                                                 proxy_id,
                                                 g_free);

      flags = g_dbus_proxy_get_flags (proxy);
      if ( (flags & G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS) == 0)
        {
          g_signal_connect (proxy,
                            "g-signal",
                            G_CALLBACK (evd_dbus_agent_on_proxy_signal),
                            proxy_data);
        }
      if ( (flags & G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES) == 0)
        {
          g_signal_connect (proxy,
                        "g-properties-changed",
                        G_CALLBACK (evd_dbus_agent_on_proxy_properties_changed),
                        proxy_data);
        }
    }
  else
    {
      g_simple_async_result_set_from_error (proxy_data->async_res, error);
      g_error_free (error);
      evd_dbus_agent_free_proxy_data (proxy_data);
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
  ObjectData *data;
  ProxyData *proxy_data = (ProxyData *) user_data;

  g_assert (proxy_data != NULL);

  data = proxy_data->obj_data;
  g_assert (data != NULL);

  if (data->vtable != NULL && data->vtable->proxy_signal != NULL)
    {
      data->vtable->proxy_signal (data->obj,
                                  proxy_data->conn_id,
                                  proxy_data->proxy_id,
                                  signal_name,
                                  parameters,
                                  data->vtable_user_data);
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

  if (data->vtable != NULL && data->vtable->proxy_properties_changed != NULL)
    {
      data->vtable->proxy_properties_changed (data->obj,
                                              proxy_data->conn_id,
                                              proxy_id,
                                              changed_properties,
                                              invalidated_properties,
                                              data->vtable_user_data);
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

  key = g_strdup_printf ("%p-%s<%s>", connection, object_path, interface_name);
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
                                 reg_obj_data->conn_id,
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

static gboolean
evd_dbus_agent_foreach_remove_proxy (gpointer key,
                                     gpointer value,
                                     gpointer user_data)
{
  ProxyData *proxy_data = (ProxyData *) value;
  GDBusConnection *conn = G_DBUS_CONNECTION (user_data);
  GDBusConnection *proxy_conn;

  proxy_conn = g_dbus_proxy_get_connection (proxy_data->proxy);
  return proxy_conn == conn;
}

static gboolean
evd_dbus_agent_foreach_remove_owned_names (gpointer key,
                                           gpointer value,
                                           gpointer user_data)
{
  NameOwnerData *name_owner_data = (NameOwnerData *) value;
  GDBusConnection *conn = G_DBUS_CONNECTION (user_data);

  return name_owner_data->dbus_conn == conn;
}

static gboolean
evd_dbus_agent_foreach_remove_reg_obj (gpointer key,
                                       gpointer value,
                                       gpointer user_data)
{
  RegObjData *reg_obj_data = (RegObjData *) value;
  GDBusConnection *conn = G_DBUS_CONNECTION (user_data);

  return reg_obj_data->dbus_conn == conn;
}

static RegObjData *
evd_dbus_agent_get_registered_object_data (GObject  *object,
                                           guint     registration_id,
                                           GError  **error)
{
  ObjectData *data;
  RegObjData *reg_obj_data;

  g_return_val_if_fail (G_IS_OBJECT (object), NULL);
  g_return_val_if_fail (registration_id > 0, NULL);

  data = evd_dbus_agent_get_object_data (object);
  if (data == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Object is invalid");
      return NULL;
    }

  reg_obj_data = g_hash_table_lookup (data->reg_objs_by_id, &registration_id);
  if (reg_obj_data == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Object registration id '%u' is invalid",
                   registration_id);
      return NULL;
    }

  return reg_obj_data;
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
  GSimpleAsyncResult *res;
  ObjectData *data;
  gchar *addr;
  ObjConnData *obj_conn_data;
  ConnData *conn_data;

  g_return_if_fail (G_IS_OBJECT (object));
  g_return_if_fail (address != NULL);

  data = evd_dbus_agent_get_object_data (object);
  if (data == NULL)
    data = evd_dbus_agent_setup_object_data (object);

  obj_conn_data = g_slice_new (ObjConnData);
  obj_conn_data->obj_data = data;

  res = g_simple_async_result_new (object,
                                   callback,
                                   user_data,
                                   evd_dbus_agent_new_connection);

  /* if 'address' is an alias, dereference it */
  if ( (addr = g_hash_table_lookup (data->addr_aliases, address)) == NULL)
    addr = g_strdup (address);
  else
    addr = g_strdup (addr);

  if (reuse)
    {
      /* lookup the connection in global cache */
      conn_data = evd_dbus_agent_search_conn_in_global_cache (addr, reuse);
      if (conn_data != NULL)
        {
          guint *conn_id;

          obj_conn_data->conn_data = conn_data;
          conn_id = evd_dbus_agent_bind_connection_to_object (data,
                                                              obj_conn_data);

          g_simple_async_result_set_op_res_gpointer (res,
                                                     conn_id,
                                                     NULL);
          g_simple_async_result_complete_in_idle (res);
          g_object_unref (res);

          return;
        }
    }

  conn_data = evd_dbus_agent_conn_data_new (addr, reuse);
  obj_conn_data->conn_data = conn_data;

  g_simple_async_result_set_op_res_gpointer (res, obj_conn_data, NULL);

  g_dbus_connection_new_for_address (addr,
                                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION |
                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                NULL,
                                cancellable,
                                evd_dbus_agent_on_new_dbus_connection,
                                res);

  g_free (addr);
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
  ObjConnData *obj_conn_data;

  data = evd_dbus_agent_get_object_data (obj);
  if (data == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Object is invalid");
      return NULL;
    }

  obj_conn_data = (ObjConnData *) (g_hash_table_lookup (data->conns,
                                                        &connection_id));
  if (obj_conn_data != NULL)
    {
      return obj_conn_data->conn_data->conn;
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
  ProxyData *proxy_data;

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

      proxy_data = g_slice_new (ProxyData);
      proxy_data->obj_data = data;
      proxy_data->conn_id = connection_id;
      proxy_data->async_res = res;
      proxy_data->proxy = NULL;

      g_dbus_proxy_new (conn,
                        flags,
                        NULL,
                        name,
                        object_path,
                        iface_name,
                        cancellable,
                        evd_dbus_agent_on_new_dbus_proxy,
                        proxy_data);
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

      data = evd_dbus_agent_get_object_data (object);

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

      key = g_strdup_printf ("%p-%s<%s>",
                             dbus_conn,
                             object_path,
                             interface_info->name);

      reg_obj_data = g_slice_new (RegObjData);
      reg_obj_data->obj_path = g_strdup (object_path);
      reg_obj_data->dbus_conn = dbus_conn;
      g_object_ref (dbus_conn);
      reg_obj_data->reg_id = reg_id;
      reg_obj_data->serial = 0;
      reg_obj_data->reg_str_id = key;
      reg_obj_data->obj_data = data;
      reg_obj_data->conn_id = connection_id;
      reg_obj_data->iface_info = g_dbus_interface_info_ref (interface_info);
      reg_obj_data->invocations = g_hash_table_new_full (g_int64_hash,
                                                         g_int64_equal,
                                                         NULL,
                                                         NULL);

      g_hash_table_insert (data->reg_objs, key, reg_obj_data);
      g_hash_table_insert (data->reg_objs_by_id,
                           &reg_obj_data->reg_id,
                           reg_obj_data);
    }

  return reg_id;
}

gboolean
evd_dbus_agent_unregister_object (GObject  *object,
                                  guint     registration_id,
                                  GError  **error)
{
  if (evd_dbus_agent_get_registered_object_data (object,
                                                 registration_id,
                                                 error) == NULL)
    {
      return FALSE;
    }
  else
    {
      ObjectData *data;

      data = evd_dbus_agent_get_object_data (object);
      g_hash_table_remove (data->reg_objs_by_id, &registration_id);

      return TRUE;
    }
}

GDBusInterfaceInfo *
evd_dbus_agent_get_registered_object_interface (GObject  *object,
                                                guint     registration_id,
                                                GError  **error)
{
  RegObjData *reg_obj_data;

  reg_obj_data = evd_dbus_agent_get_registered_object_data (object,
                                                            registration_id,
                                                            error);
  if (reg_obj_data == NULL)
    return NULL;
  else
    return reg_obj_data->iface_info;
}

GDBusMethodInvocation *
evd_dbus_agent_get_method_invocation (GObject  *object,
                                      guint     registration_id,
                                      guint64   serial,
                                      GError  **error)
{
  RegObjData *reg_obj_data;
  GDBusMethodInvocation *invocation;

  reg_obj_data = evd_dbus_agent_get_registered_object_data (object,
                                                            registration_id,
                                                            error);
  if (reg_obj_data == NULL)
    return NULL;

  invocation =
    (GDBusMethodInvocation *) g_hash_table_lookup (reg_obj_data->invocations,
                                                   &serial);
  if (invocation == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Method invocation serial '%" G_GUINT64_FORMAT "' is invalid",
                   serial);
      return NULL;
    }

  return invocation;
}

gboolean
evd_dbus_agent_method_call_return (GObject  *object,
                                   guint     registration_id,
                                   guint64   serial,
                                   GVariant  *return_parameters,
                                   GError   **error)
{
  ObjectData *data;
  RegObjData *reg_obj_data;
  GDBusMethodInvocation *invocation;

  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (registration_id > 0, FALSE);
  g_return_val_if_fail (return_parameters != NULL, FALSE);

  data = evd_dbus_agent_get_object_data (object);
  if (data == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Object is invalid");
      return FALSE;
    }

  reg_obj_data = g_hash_table_lookup (data->reg_objs_by_id, &registration_id);
  if (reg_obj_data == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Object registration id '%u' is invalid",
                   registration_id);
      return FALSE;
    }

  invocation = g_hash_table_lookup (reg_obj_data->invocations, &serial);
  if (invocation == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "No method call with serial '%" G_GUINT64_FORMAT  "'",
                   serial);
      return FALSE;
    }

  g_dbus_method_invocation_return_value (invocation, return_parameters);

  g_hash_table_remove (reg_obj_data->invocations, &serial);

  return TRUE;
}

gboolean
evd_dbus_agent_emit_signal (GObject      *object,
                            guint         registration_id,
                            const gchar  *signal_name,
                            GVariant     *parameters,
                            GError      **error)
{
  ObjectData *data;
  RegObjData *reg_obj_data;

  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (registration_id > 0, FALSE);
  g_return_val_if_fail (signal_name != NULL, FALSE);

  data = evd_dbus_agent_get_object_data (object);
  if (data == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Object is invalid");
      return FALSE;
    }

  reg_obj_data = g_hash_table_lookup (data->reg_objs_by_id, &registration_id);
  if (reg_obj_data == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Object registration id '%u' is invalid",
                   registration_id);
      return FALSE;
    }

  return g_dbus_connection_emit_signal (reg_obj_data->dbus_conn,
                                        NULL,
                                        reg_obj_data->obj_path,
                                        reg_obj_data->iface_info->name,
                                        signal_name,
                                        parameters,
                                        error);
}

static void
evd_dbus_agent_name_acquired (GDBusConnection *connection,
                              const gchar     *name,
                              gpointer         user_data)
{
  NameOwnerData *owner_data = (NameOwnerData *) user_data;
  ObjectData *obj_data;

  obj_data = owner_data->obj_data;

  if (obj_data->vtable != NULL && obj_data->vtable->name_acquired != NULL)
    {
      obj_data->vtable->name_acquired (obj_data->obj,
                                       owner_data->conn_id,
                                       owner_data->owner_id,
                                       obj_data->vtable_user_data);
    }
}

static void
evd_dbus_agent_name_lost (GDBusConnection *connection,
                          const gchar     *name,
                          gpointer         user_data)
{
  NameOwnerData *owner_data = (NameOwnerData *) user_data;
  ObjectData *obj_data;

  obj_data = owner_data->obj_data;

  if (obj_data->vtable != NULL && obj_data->vtable->name_lost != NULL)
    {
      obj_data->vtable->name_lost (obj_data->obj,
                                   owner_data->conn_id,
                                   owner_data->owner_id,
                                   obj_data->vtable_user_data);
    }
}

guint
evd_dbus_agent_own_name (GObject             *object,
                         guint                connection_id,
                         const gchar         *name,
                         GBusNameOwnerFlags   flags,
                         GError             **error)
{
  GDBusConnection *conn;
  ObjectData *obj_data;
  NameOwnerData *owner_data;

  conn = evd_dbus_agent_get_connection (object, connection_id, error);
  if (conn == NULL)
    return 0;

  obj_data = evd_dbus_agent_get_object_data (object);

  owner_data = g_slice_new (NameOwnerData);
  owner_data->obj_data = obj_data;
  owner_data->conn_id = connection_id;
  owner_data->dbus_conn = conn;

  owner_data->owner_id = g_bus_own_name_on_connection (conn,
                                           name,
                                           flags,
                                           evd_dbus_agent_name_acquired,
                                           evd_dbus_agent_name_lost,
                                           owner_data,
                                           NULL);

  g_hash_table_insert (obj_data->owned_names,
                       &owner_data->owner_id,
                       owner_data);

  return owner_data->owner_id;
}

gboolean
evd_dbus_agent_unown_name (GObject  *object,
                           guint     owner_id,
                           GError  **error)
{
  ObjectData *obj_data;

  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (owner_id > 0, FALSE);

  obj_data = evd_dbus_agent_get_object_data (object);
  if (obj_data == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Object is invalid");
      return FALSE;
    }

  g_hash_table_remove (obj_data->owned_names, &owner_id);

  return TRUE;
}
