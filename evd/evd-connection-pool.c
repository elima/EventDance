/*
 * evd-connection-pool.c
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

#include "evd-connection-pool.h"

#include "evd-utils.h"
#include "evd-error.h"
#include "evd-socket.h"

G_DEFINE_TYPE (EvdConnectionPool, evd_connection_pool, EVD_TYPE_IO_STREAM_GROUP)

#define EVD_CONNECTION_POOL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                              EVD_TYPE_CONNECTION_POOL, \
                                              EvdConnectionPoolPrivate))

#define DEFAULT_MIN_CONNS 1
#define DEFAULT_MAX_CONNS 5

#define RETRY_TIMEOUT 500 /* in miliseconds */

#define TOTAL_SOCKETS(pool) (self->priv->connecting_sockets + \
                             g_queue_get_length (pool->priv->conns))

#define HAS_REQUESTS(pool)  (g_queue_get_length (pool->priv->requests) > 0)

/* private data */
struct _EvdConnectionPoolPrivate
{
  gchar *target;

  guint min_conns;
  guint max_conns;

  GQueue *conns;
  GQueue *requests;

  GType connection_type;

  guint connecting_sockets;

  gboolean tls_autostart;
  EvdTlsCredentials *tls_cred;

  guint retry_src_id;
};

/* properties */
enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_CONNECTION_TYPE
};

static void     evd_connection_pool_class_init            (EvdConnectionPoolClass *class);
static void     evd_connection_pool_init                  (EvdConnectionPool *self);
static void     evd_connection_pool_constructed           (GObject *obj);
static void     evd_connection_pool_dispose               (GObject *obj);
static void     evd_connection_pool_finalize              (GObject *obj);

static void     evd_connection_pool_set_property          (GObject      *obj,
                                                           guint         prop_id,
                                                           const GValue *value,
                                                           GParamSpec   *pspec);
static void     evd_connection_pool_get_property          (GObject    *obj,
                                                           guint       prop_id,
                                                           GValue     *value,
                                                           GParamSpec *pspec);

static gboolean group_add_stream                          (EvdIoStreamGroup *group,
                                                           GIOStream        *io_stream);
static gboolean group_remove_stream                       (EvdIoStreamGroup *group,
                                                           GIOStream        *io_stream);

static void     evd_connection_pool_unref_request         (GSimpleAsyncResult *result);

static void     evd_connection_pool_create_new_socket     (EvdConnectionPool *self);

static gboolean evd_connection_pool_create_min_conns      (gpointer user_data);

static void
evd_connection_pool_class_init (EvdConnectionPoolClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdIoStreamGroupClass *io_stream_group_class = EVD_IO_STREAM_GROUP_CLASS (class);

  obj_class->constructed = evd_connection_pool_constructed;
  obj_class->dispose = evd_connection_pool_dispose;
  obj_class->finalize = evd_connection_pool_finalize;
  obj_class->set_property = evd_connection_pool_set_property;
  obj_class->get_property = evd_connection_pool_get_property;

  io_stream_group_class->add = group_add_stream;
  io_stream_group_class->remove = group_remove_stream;

  g_object_class_install_property (obj_class, PROP_ADDRESS,
                                   g_param_spec_string ("address",
                                                        "Address",
                                                        "The target socket address to connect to",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_CONNECTION_TYPE,
                                   g_param_spec_gtype ("connection-type",
                                                       "Connection type",
                                                       "The GType of the connections handled by the pool",
                                                       EVD_TYPE_CONNECTION,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdConnectionPoolPrivate));
}

static void
evd_connection_pool_init (EvdConnectionPool *self)
{
  EvdConnectionPoolPrivate *priv;

  priv = EVD_CONNECTION_POOL_GET_PRIVATE (self);
  self->priv = priv;

  self->priv->target = NULL;

  priv->min_conns = DEFAULT_MIN_CONNS;
  priv->max_conns = DEFAULT_MAX_CONNS;

  priv->conns = g_queue_new ();
  priv->requests = g_queue_new ();

  priv->connecting_sockets = 0;

  priv->tls_autostart = FALSE;
  priv->tls_cred = NULL;

  priv->retry_src_id = 0;
}

static void
evd_connection_pool_constructed (GObject *obj)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (obj);

  evd_connection_pool_create_min_conns (self);

  G_OBJECT_CLASS (evd_connection_pool_parent_class)->constructed (obj);
}

static void
evd_connection_pool_dispose (GObject *obj)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (obj);

  if (self->priv->conns != NULL)
    {
      g_queue_free_full (self->priv->conns, g_object_unref);
      self->priv->conns = NULL;
    }

  if (self->priv->requests != NULL)
    {
      g_queue_free_full (self->priv->requests,
                         (GDestroyNotify) evd_connection_pool_unref_request);
      self->priv->requests = NULL;
    }

  G_OBJECT_CLASS (evd_connection_pool_parent_class)->dispose (obj);
}

static void
evd_connection_pool_finalize (GObject *obj)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (obj);

  g_free (self->priv->target);

  if (self->priv->retry_src_id != 0)
    {
      g_source_remove (self->priv->retry_src_id);
      self->priv->retry_src_id = 0;
    }

  G_OBJECT_CLASS (evd_connection_pool_parent_class)->finalize (obj);
}

static void
evd_connection_pool_set_property (GObject      *obj,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EvdConnectionPool *self;

  self = EVD_CONNECTION_POOL (obj);

  switch (prop_id)
    {
    case PROP_ADDRESS:
      if (self->priv->target != NULL)
        g_free (self->priv->target);
      self->priv->target = g_value_dup_string (value);
      break;

    case PROP_CONNECTION_TYPE:
      {
        GType conn_type;

        conn_type = g_value_get_gtype (value);
        if (g_type_is_a (conn_type, EVD_TYPE_CONNECTION))
          self->priv->connection_type = conn_type;
        else
          g_warning ("Invalid connection type for EvdConnectionPool");

        break;
      }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_connection_pool_get_property (GObject    *obj,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EvdConnectionPool *self;

  self = EVD_CONNECTION_POOL (obj);

  switch (prop_id)
    {
    case PROP_ADDRESS:
      g_value_set_string (value, self->priv->target);
      break;

    case PROP_CONNECTION_TYPE:
      g_value_set_gtype (value, self->priv->connection_type);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_connection_pool_finish_request (EvdConnectionPool  *self,
                                    EvdConnection      *conn,
                                    GSimpleAsyncResult *res)
{
  evd_io_stream_group_remove (EVD_IO_STREAM_GROUP (self), G_IO_STREAM (conn));

  g_simple_async_result_set_op_res_gpointer (res, conn, g_object_unref);
  g_simple_async_result_complete_in_idle (res);
  g_object_unref (res);
}

static void
connection_available (EvdConnectionPool *self, EvdConnection *conn)
{
  if (HAS_REQUESTS (self))
    {
      GSimpleAsyncResult *res;

      res = G_SIMPLE_ASYNC_RESULT (g_queue_pop_head (self->priv->requests));

      evd_connection_pool_finish_request (self, conn, res);

      evd_connection_pool_create_min_conns (self);
    }
  else
    {
      g_queue_push_tail (self->priv->conns, conn);
      g_object_ref (conn);
    }
}

static void
connection_on_tls_started (GObject      *obj,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (user_data);
  EvdConnection *conn = EVD_CONNECTION (obj);
  GError *error = NULL;

  if (evd_connection_starttls_finish (conn, res, &error))
    {
      connection_available (self, conn);
    }
  else
    {
      /* @TODO: do proper logging */
      g_print ("TLS upgrade error in EvdConnectionPool: %s\n", error->message);
      g_error_free (error);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }

  g_object_unref (self);
}

static void
connection_starttls (EvdConnectionPool *self, EvdConnection *conn)
{
  EvdTlsSession *tls_session;
  EvdTlsCredentials *tls_cred;

  tls_session = evd_connection_get_tls_session (conn);
  tls_cred = evd_connection_pool_get_tls_credentials (self);
  evd_tls_session_set_credentials (tls_session, tls_cred);

  g_object_ref (self);
  evd_connection_starttls (conn,
                           EVD_TLS_MODE_CLIENT,
                           NULL,
                           connection_on_tls_started,
                           self);
}

static gboolean
group_add_stream (EvdIoStreamGroup *group, GIOStream *io_stream)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (group);
  EvdConnection *conn = EVD_CONNECTION (io_stream);

  if (self->priv->tls_autostart && ! evd_connection_get_tls_active (conn))
    {
      if (! EVD_IO_STREAM_GROUP_CLASS (evd_connection_pool_parent_class)->add
          (group, io_stream))
        {
          return FALSE;
        }

      connection_starttls (self, conn);

      return TRUE;
    }

  if (HAS_REQUESTS (self))
    {
      connection_available (self, conn);
    }
  else
    {
      if (! EVD_IO_STREAM_GROUP_CLASS (evd_connection_pool_parent_class)->add
          (group, io_stream))
        {
          return FALSE;
        }

      connection_available (self, conn);
    }

  return TRUE;
}

static gboolean
group_remove_stream (EvdIoStreamGroup *group, GIOStream *io_stream)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (group);

  if (! EVD_IO_STREAM_GROUP_CLASS (evd_connection_pool_parent_class)->remove
      (group, io_stream))
    {
      return FALSE;
    }

  g_queue_remove (self->priv->conns, io_stream);

  g_object_unref (io_stream);

  return TRUE;
}

static void
evd_connection_pool_unref_request (GSimpleAsyncResult *result)
{
  g_simple_async_result_set_error (result,
                                   G_IO_ERROR,
                                   G_IO_ERROR_CLOSED,
                                   "Connection pool destroyed");
  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static gboolean
evd_connection_pool_create_min_conns (gpointer user_data)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (user_data);

  self->priv->retry_src_id = 0;

  while (TOTAL_SOCKETS (self) < self->priv->min_conns)
    {
      evd_connection_pool_create_new_socket (self);
    }

  return FALSE;
}

static void
evd_connection_pool_socket_on_connect (GObject      *obj,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (user_data);
  EvdSocket *socket = EVD_SOCKET (obj);
  GIOStream *io_stream;
  GError *error = NULL;

  self->priv->connecting_sockets--;

  if ( (io_stream = evd_socket_connect_finish (socket,
                                               res,
                                               &error)) != NULL)
    {
      /* remove any retry timoeut source */
      if (self->priv->retry_src_id != 0)
        {
          g_source_remove (self->priv->retry_src_id);
          self->priv->retry_src_id = 0;
        }

      evd_io_stream_group_add (EVD_IO_STREAM_GROUP (self), io_stream);
    }
  else
    {
      /* @TODO: log properly */
      g_print ("Connection pool error: %s\n", error->message);
      g_error_free (error);

      /* retry after a timeout */
      self->priv->retry_src_id =
        evd_timeout_add (NULL,
                         RETRY_TIMEOUT,
                         G_PRIORITY_LOW,
                         evd_connection_pool_create_min_conns,
                         self);
    }

  g_object_unref (socket);
  g_object_unref (self);
}

static void
evd_connection_pool_create_new_socket (EvdConnectionPool *self)
{
  EvdSocket *socket;
  EvdConnectionPoolClass *class;

  socket = evd_socket_new ();

  class = EVD_CONNECTION_POOL_GET_CLASS (self);
  if (class->get_connection_type != NULL)
    {
      GType conn_type;

      conn_type = class->get_connection_type (self);
      if (g_type_is_a (conn_type, EVD_TYPE_CONNECTION))
        self->priv->connection_type = conn_type;
      else
        g_warning ("Invalid connection type for EvdConnectionPool");
    }

  g_object_set (socket,
                "io-stream-type", self->priv->connection_type,
                NULL);

  self->priv->connecting_sockets++;

  g_object_ref (self);
  evd_socket_connect_to (socket,
                         self->priv->target,
                         NULL,
                         evd_connection_pool_socket_on_connect,
                         self);
}

/* public methods */

EvdConnectionPool *
evd_connection_pool_new (const gchar *address, GType connection_type)
{
  EvdConnectionPool *self;

  g_return_val_if_fail (address != NULL, NULL);
  g_return_val_if_fail (g_type_is_a (connection_type, EVD_TYPE_CONNECTION),
                        NULL);

  self = g_object_new (EVD_TYPE_CONNECTION_POOL,
                       "address", address,
                       "connection-type", connection_type,
                       NULL);

  return self;
}

gboolean
evd_connection_pool_has_free_connections (EvdConnectionPool *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION_POOL (self), FALSE);

  return g_queue_get_length (self->priv->conns) > 0;
}

void
evd_connection_pool_get_connection (EvdConnectionPool   *self,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  GSimpleAsyncResult *res;

  g_return_if_fail (EVD_IS_CONNECTION_POOL (self));

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_connection_pool_get_connection);

  if (g_queue_get_length (self->priv->conns) > 0)
    {
      evd_connection_pool_finish_request (self,
                          EVD_CONNECTION (g_queue_pop_head (self->priv->conns)),
                          res);

      if (TOTAL_SOCKETS (self) < self->priv->min_conns)
        evd_connection_pool_create_new_socket (self);
    }
  else
    {
      g_queue_push_tail (self->priv->requests, res);

      if (TOTAL_SOCKETS (self) < self->priv->max_conns)
        evd_connection_pool_create_new_socket (self);
    }
}

/**
 * evd_connection_pool_get_connection_finish:
 *
 * Returns: (transfer full):
 **/
EvdConnection *
evd_connection_pool_get_connection_finish (EvdConnectionPool  *self,
                                           GAsyncResult       *result,
                                           GError            **error)
{
  g_return_val_if_fail (EVD_IS_CONNECTION_POOL (self), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                               G_OBJECT (self),
                               evd_connection_pool_get_connection),
                        NULL);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      EvdConnection *conn;

      conn =
        g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
      g_object_ref (conn);

      return conn;
    }
  else
    {
      return NULL;
    }
}

gboolean
evd_connection_pool_recycle (EvdConnectionPool *self, EvdConnection *conn)
{
  g_return_val_if_fail (EVD_IS_CONNECTION_POOL (self), FALSE);
  g_return_val_if_fail (EVD_IS_CONNECTION (conn), FALSE);

  if (g_io_stream_is_closed (G_IO_STREAM (conn)))
    return FALSE;

  if (TOTAL_SOCKETS (self) >= self->priv->max_conns)
    return FALSE;

  if (evd_io_stream_get_group (EVD_IO_STREAM (conn)) == EVD_IO_STREAM_GROUP (self))
    return TRUE;
  else
    return evd_io_stream_group_add (EVD_IO_STREAM_GROUP (self),
                                    G_IO_STREAM (conn));
}

void
evd_connection_pool_set_tls_autostart (EvdConnectionPool *self,
                                       gboolean           autostart)
{
  g_return_if_fail (EVD_IS_CONNECTION_POOL (self));

  self->priv->tls_autostart = autostart;

  if (autostart)
    {
      gpointer item;
      EvdConnection *conn;

      item = g_queue_pop_head (self->priv->conns);
      while (item != NULL)
        {
          conn = EVD_CONNECTION (item);

          connection_starttls (self, conn);
          g_object_unref (conn);

          item = g_queue_pop_head (self->priv->conns);
        }
    }
}

gboolean
evd_connection_pool_get_tls_autostart (EvdConnectionPool *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION_POOL (self), FALSE);

  return self->priv->tls_autostart;
}

void
evd_connection_pool_set_tls_credentials (EvdConnectionPool *self,
                                         EvdTlsCredentials *credentials)
{
  g_return_if_fail (EVD_IS_CONNECTION_POOL (self));
  g_return_if_fail (EVD_IS_TLS_CREDENTIALS (credentials));

  if (self->priv->tls_cred != NULL)
    g_object_unref (self->priv->tls_cred);

  self->priv->tls_cred = credentials;
  g_object_ref (self->priv->tls_cred);
}

/**
 * evd_connection_pool_get_tls_credentials:
 *
 * Returns: (transfer none):
 **/
EvdTlsCredentials *
evd_connection_pool_get_tls_credentials (EvdConnectionPool *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION_POOL (self), NULL);

  if (self->priv->tls_cred == NULL)
    self->priv->tls_cred = evd_tls_credentials_new ();

  return self->priv->tls_cred;
}
