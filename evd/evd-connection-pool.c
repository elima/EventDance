/*
 * evd-connection-pool.c
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

#define TOTAL_SOCKETS(pool) (g_queue_get_length (pool->priv->sockets) + \
                             g_queue_get_length (pool->priv->conns))

#define HAS_REQUESTS(pool)  (g_queue_get_length (pool->priv->requests) > 0)

/* private data */
struct _EvdConnectionPoolPrivate
{
  gchar *target;

  guint min_conns;
  guint max_conns;

  GQueue *conns;
  GQueue *sockets;
  GQueue *requests;

  GType connection_type;
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
static void     evd_connection_pool_finalize              (GObject *obj);

static void     evd_connection_pool_set_property          (GObject      *obj,
                                                           guint         prop_id,
                                                           const GValue *value,
                                                           GParamSpec   *pspec);
static void     evd_connection_pool_get_property          (GObject    *obj,
                                                           guint       prop_id,
                                                           GValue     *value,
                                                           GParamSpec *pspec);

static void     evd_connection_pool_foreach_unref_conn    (gpointer data,
                                                           gpointer user_data);
static void     evd_connection_pool_foreach_unref_socket  (gpointer data,
                                                           gpointer user_data);
static void     evd_connection_pool_foreach_unref_request (gpointer data,
                                                           gpointer user_data);

static void     evd_connection_pool_socket_on_close       (EvdSocket *socket,
                                                           gpointer   user_data);

static void     evd_connection_pool_create_new_socket     (EvdConnectionPool *self);

static void     evd_connection_pool_reuse_socket          (EvdConnectionPool *self,
                                                           EvdSocket         *socket);

static void
evd_connection_pool_class_init (EvdConnectionPoolClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_connection_pool_finalize;
  obj_class->set_property = evd_connection_pool_set_property;
  obj_class->get_property = evd_connection_pool_get_property;

  g_object_class_install_property (obj_class, PROP_ADDRESS,
                                   g_param_spec_string ("address",
                                                        "Address",
                                                        "The target socket address to connect to",
                                                        NULL,
                                                        G_PARAM_READWRITE |
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
  priv->sockets = g_queue_new ();
  priv->requests = g_queue_new ();
}

static void
evd_connection_pool_finalize (GObject *obj)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (obj);

  g_free (self->priv->target);

  g_queue_foreach (self->priv->conns,
                   evd_connection_pool_foreach_unref_conn,
                   self);
  g_queue_free (self->priv->conns);

  g_queue_foreach (self->priv->sockets,
                   evd_connection_pool_foreach_unref_socket,
                   self);
  g_queue_free (self->priv->sockets);

  g_queue_foreach (self->priv->requests,
                   evd_connection_pool_foreach_unref_request,
                   self);
  g_queue_free (self->priv->requests);

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
evd_connection_pool_connection_on_close (EvdConnection *conn,
                                         gpointer       user_data)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (user_data);

  g_queue_remove (self->priv->conns, conn);

  if (TOTAL_SOCKETS (self) < self->priv->min_conns)
    {
      EvdSocket *socket;

      socket = evd_connection_get_socket (conn);

      g_object_ref (socket);
      evd_connection_pool_reuse_socket (self, socket);
    }

  evd_io_stream_group_remove (EVD_IO_STREAM_GROUP (self), G_IO_STREAM (conn));

  g_object_unref (conn);
}

static void
evd_connection_pool_foreach_unref_conn (gpointer data,
                                        gpointer user_data)
{
  EvdConnection *conn = EVD_CONNECTION (data);

  g_signal_handlers_disconnect_by_func (conn,
                                        evd_connection_pool_connection_on_close,
                                        user_data);
  g_object_unref (conn);
}

static void
evd_connection_pool_foreach_unref_socket (gpointer data,
                                          gpointer user_data)
{
  EvdSocket *socket = EVD_SOCKET (data);

  g_signal_handlers_disconnect_by_func (socket,
                                        evd_connection_pool_socket_on_close,
                                        user_data);
  g_object_unref (socket);
}

static void
evd_connection_pool_foreach_unref_request (gpointer data,
                                           gpointer user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (data);

  g_simple_async_result_set_error (result,
                                   G_IO_ERROR,
                                   G_IO_ERROR_CLOSED,
                                   "Connection pool destroyed");
  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static void
evd_connection_pool_finish_request (EvdConnectionPool  *self,
                                    EvdConnection      *conn,
                                    GSimpleAsyncResult *res)
{
  g_signal_handlers_disconnect_by_func (conn,
                                        evd_connection_pool_connection_on_close,
                                        self);

  g_simple_async_result_set_op_res_gpointer (res, conn, g_object_unref);
  g_simple_async_result_complete_in_idle (res);
  g_object_unref (res);
}

static void
evd_connection_pool_new_connection (EvdConnectionPool *self,
                                    EvdConnection     *conn)
{
  if (HAS_REQUESTS (self))
    {
      GSimpleAsyncResult *res;

      res = G_SIMPLE_ASYNC_RESULT (g_queue_pop_head (self->priv->requests));

      g_signal_handlers_disconnect_by_func (conn,
                                        evd_connection_pool_connection_on_close,
                                        self);

      evd_connection_pool_finish_request (self, conn, res);

      if (TOTAL_SOCKETS (self) < self->priv->min_conns)
        evd_connection_pool_create_new_socket (self);
    }
  else
    {
      g_signal_connect (conn,
                        "close",
                        G_CALLBACK (evd_connection_pool_connection_on_close),
                        self);

      g_queue_push_tail (self->priv->conns, conn);
    }
}

static void
evd_connection_pool_socket_on_connect (GObject      *obj,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  EvdConnectionPool *self = EVD_CONNECTION_POOL (user_data);
  GIOStream *io_stream;
  GError *error = NULL;

  g_queue_remove (self->priv->sockets, obj);

  if ( (io_stream = evd_socket_connect_finish (EVD_SOCKET (obj),
                                               res,
                                               &error)) != NULL)
    {
      EvdConnection *conn;
      EvdSocket *socket;

      conn = EVD_CONNECTION (io_stream);
      evd_io_stream_group_add (EVD_IO_STREAM_GROUP (self), G_IO_STREAM (conn));

      socket = evd_connection_get_socket (conn);

      g_assert (socket == EVD_SOCKET (obj));

      evd_connection_pool_new_connection (self, conn);
    }
  else
    {
      /* @TODO: handle error */
      g_print ("error connection: %s\n", error->message);
      g_error_free (error);

      evd_socket_close (EVD_SOCKET (obj), NULL);

      g_object_ref (EVD_SOCKET (obj));
      evd_connection_pool_reuse_socket (self, EVD_SOCKET (obj));
    }
}

static void
evd_connection_pool_reuse_socket (EvdConnectionPool *self, EvdSocket *socket)
{
  g_queue_push_tail (self->priv->sockets, (gpointer) socket);

  evd_socket_connect_to (socket,
                         self->priv->target,
                         NULL,
                         evd_connection_pool_socket_on_connect,
                         self);
}

static void
evd_connection_pool_socket_on_close (EvdSocket *socket, gpointer user_data)
{
  guint total;

  EvdConnectionPool *self = EVD_CONNECTION_POOL (user_data);

  total = TOTAL_SOCKETS (self);

  if (total >= self->priv->max_conns ||
      (total >= self->priv->min_conns && ! HAS_REQUESTS (self)) )
    {
      g_queue_remove (self->priv->sockets, socket);
      g_object_unref (socket);
    }
  else
    {
      evd_connection_pool_reuse_socket (self, socket);
    }
}

static void
evd_connection_pool_create_new_socket (EvdConnectionPool *self)
{
  EvdSocket *socket;
  EvdConnectionPoolClass *class;

  socket = evd_socket_new ();

  g_signal_connect (socket,
                    "close",
                    G_CALLBACK (evd_connection_pool_socket_on_close),
                    self);

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

  evd_connection_pool_reuse_socket (self, socket);
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

  while (g_queue_get_length (self->priv->sockets) < self->priv->min_conns)
    evd_connection_pool_create_new_socket (self);

  return self;
}

gboolean
evd_connection_pool_has_free_connections (EvdConnectionPool *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION_POOL (self), FALSE);

  return (g_queue_get_length (self->priv->conns) > 0);
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

  if (evd_connection_get_group (conn) != EVD_IO_STREAM_GROUP (self))
    evd_io_stream_group_add (EVD_IO_STREAM_GROUP (self),
                             G_IO_STREAM (conn));

  g_signal_handlers_disconnect_by_func (conn,
                                        evd_connection_pool_connection_on_close,
                                        self);

  g_object_ref (conn);
  evd_connection_pool_new_connection (self, conn);

  return TRUE;
}
