/*
 * evd-reproxy.c
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

#include "evd-reproxy.h"

#include "evd-utils.h"
#include "evd-buffered-input-stream.h"
#include "evd-connection.h"

G_DEFINE_TYPE (EvdReproxy, evd_reproxy, EVD_TYPE_SERVICE)

#define EVD_REPROXY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      EVD_TYPE_REPROXY, \
                                      EvdReproxyPrivate))

#define DEFAULT_BACKEND_MIN_CONNS   1
#define DEFAULT_BACKEND_MAX_CONNS   2

#define BRIDGE_BLOCK_SIZE           8193

#define BRIDGE_DATA_KEY "org.eventdance.lib.reproxy.bridge"

typedef struct _EvdReproxySocketData EvdReproxySocketData;

/* private data */
struct _EvdReproxyPrivate
{
  GList *backends;
  GList *next_backend_node;

  guint backend_max_conns;
  guint backend_min_conns;

  GQueue *conns;
};

typedef struct
{
  EvdConnection *conn;
  gchar *buf;
  gsize size;
} EvdReproxyBridge;

static void     evd_reproxy_class_init            (EvdReproxyClass *class);
static void     evd_reproxy_init                  (EvdReproxy *self);

static void     evd_reproxy_finalize              (GObject *obj);
static void     evd_reproxy_dispose               (GObject *obj);

static void     evd_reproxy_connection_accepted   (EvdService    *service,
                                                   EvdConnection *conn);

static gboolean evd_reproxy_bridge_read           (gpointer user_data);

static void
evd_reproxy_class_init (EvdReproxyClass *class)
{
  GObjectClass *obj_class;
  EvdServiceClass *service_class;

  obj_class = G_OBJECT_CLASS (class);
  obj_class->dispose = evd_reproxy_dispose;
  obj_class->finalize = evd_reproxy_finalize;

  service_class = EVD_SERVICE_CLASS (class);
  service_class->connection_accepted = evd_reproxy_connection_accepted;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdReproxyPrivate));
}

static void
evd_reproxy_init (EvdReproxy *self)
{
  EvdReproxyPrivate *priv;

  priv = EVD_REPROXY_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->backends = NULL;

  priv->backend_min_conns = DEFAULT_BACKEND_MIN_CONNS;
  priv->backend_max_conns = DEFAULT_BACKEND_MAX_CONNS;

  priv->next_backend_node = NULL;

  priv->conns = g_queue_new ();
}

static void
evd_reproxy_free_backend (gpointer data, gpointer user_data)
{
  EvdConnectionPool *backend = EVD_CONNECTION_POOL (data);

  g_object_unref (backend);
}

static void
evd_reproxy_free_connection (gpointer data, gpointer user_data)
{
  EvdConnection *conn = EVD_CONNECTION (data);

  g_object_unref (conn);
}

static void
evd_reproxy_dispose (GObject *obj)
{
  EvdReproxy *self = EVD_REPROXY (obj);

  g_list_foreach (self->priv->backends,
                  evd_reproxy_free_backend,
                  NULL);
  g_list_free (self->priv->backends);
  self->priv->backends = NULL;

  G_OBJECT_CLASS (evd_reproxy_parent_class)->dispose (obj);
}

static void
evd_reproxy_finalize (GObject *obj)
{
  EvdReproxy *self = EVD_REPROXY (obj);

  g_queue_foreach (self->priv->conns,
                   evd_reproxy_free_connection,
                   NULL);
  g_queue_free (self->priv->conns);

  G_OBJECT_CLASS (evd_reproxy_parent_class)->finalize (obj);
}

static EvdConnectionPool *
evd_reproxy_get_backend_from_node (GList *backend_node)
{
  return (backend_node != NULL) ?
    EVD_CONNECTION_POOL (backend_node->data) : NULL;
}

static GList *
evd_reproxy_get_next_backend_node (EvdReproxy *self, GList *backend_node)
{
  if (backend_node != NULL)
    {
      if (backend_node->next != NULL)
        return backend_node->next;
      else
        return self->priv->backends;
    }
  else
    {
      return NULL;
    }
}

static void
evd_reproxy_hop_backend (EvdReproxy *self)
{
  self->priv->next_backend_node =
    evd_reproxy_get_next_backend_node (self,
                                       self->priv->next_backend_node);
}

static void
evd_reproxy_enqueue_connection (EvdReproxy    *self,
                                EvdConnection *conn)
{
  g_object_ref (conn);
  g_queue_push_tail (self->priv->conns, (gpointer) conn);
}

static EvdConnectionPool *
evd_reproxy_get_backend_with_free_connections (EvdReproxy *self)
{
  EvdConnectionPool *backend;
  GList *orig_node;

  if (self->priv->next_backend_node == NULL)
    return NULL;

  orig_node = self->priv->next_backend_node;

  do
    {
      backend =
        evd_reproxy_get_backend_from_node (self->priv->next_backend_node);

      if (evd_connection_pool_has_free_connections (backend))
        return backend;
      else
        evd_reproxy_hop_backend (self);
    }
  while (self->priv->next_backend_node != orig_node);

  return NULL;
}

static gboolean
evd_reproxy_bridge_write (gpointer user_data)
{
  EvdConnection *conn0 = EVD_CONNECTION (user_data);
  EvdReproxyBridge *bridge;
  GOutputStream *stream;
  gssize out_size;
  GError *error = NULL;

  bridge = g_object_get_data (G_OBJECT (conn0), BRIDGE_DATA_KEY);

  stream = g_io_stream_get_output_stream (G_IO_STREAM (bridge->conn));

  if ( (out_size =
        g_output_stream_write (stream, bridge->buf, bridge->size, NULL, &error)) >= 0)
    {
      if (out_size < bridge->size)
        {
          GInputStream *input_stream;

          input_stream = g_io_stream_get_input_stream (G_IO_STREAM (conn0));

          evd_buffered_input_stream_unread (EVD_BUFFERED_INPUT_STREAM (input_stream),
                                            bridge->buf + out_size,
                                            bridge->size - out_size,
                                            NULL,
                                            &error);
        }
      else
        {
          evd_reproxy_bridge_read (conn0);
        }
    }

  if (error != NULL)
    {
      g_debug ("error: %s", error->message);
      g_error_free (error);
    }

  return FALSE;
}

static void
evd_reproxy_bridge_on_read (GObject      *obj,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  GError *error = NULL;
  EvdConnection *conn0 = EVD_CONNECTION (user_data);
  EvdReproxyBridge *bridge;
  gssize size;

  bridge = g_object_get_data (G_OBJECT (conn0), BRIDGE_DATA_KEY);

  if ( (size = g_input_stream_read_finish (G_INPUT_STREAM (obj),
                                           res,
                                           &error)) > 0)
    {
      bridge->size = (gsize) size;

      evd_reproxy_bridge_write (conn0);
    }
  else if (size < 0)
    {
      g_debug ("reproxy read error: %s", error->message);
      g_error_free (error);
    }
}

static gboolean
evd_reproxy_bridge_read (gpointer user_data)
{
  EvdConnection *conn0 = EVD_CONNECTION (user_data);
  EvdReproxyBridge *bridge;

  bridge = g_object_get_data (G_OBJECT (conn0), BRIDGE_DATA_KEY);

  if (evd_connection_get_max_writable (bridge->conn) > 0)
    {
      GInputStream *stream;

      stream = g_io_stream_get_input_stream (G_IO_STREAM (conn0));

      if (! g_input_stream_has_pending (stream))
        g_input_stream_read_async (stream,
                                   bridge->buf,
                                   BRIDGE_BLOCK_SIZE,
                                   evd_connection_get_priority (conn0),
                                   NULL,
                                   evd_reproxy_bridge_on_read,
                                   conn0);
    }
  else
    {
      evd_connection_lock_close (EVD_CONNECTION (conn0));
    }

  return FALSE;
}

static void
evd_reproxy_bridge_on_write (EvdConnection *bridge, gpointer user_data)
{
  evd_connection_unlock_close (EVD_CONNECTION (user_data));
  evd_reproxy_bridge_read (EVD_CONNECTION (user_data));
}

static void
evd_reproxy_connection_setup_bridge (EvdReproxy    *self,
                                     EvdConnection *conn0,
                                     EvdConnection *conn1)
{
  EvdReproxyBridge *bridge;

  bridge = g_new0 (EvdReproxyBridge, 1);
  bridge->conn = conn1;

  g_object_ref (conn1);

  bridge->buf = g_new (gchar, BRIDGE_BLOCK_SIZE);

  g_object_set_data (G_OBJECT (conn0), BRIDGE_DATA_KEY, bridge);

  g_signal_connect (conn1,
                    "write",
                    G_CALLBACK (evd_reproxy_bridge_on_write),
                    conn0);

  evd_reproxy_bridge_read (conn0);
}

static void
evd_reproxy_connection_on_flush (GObject      *obj,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
  EvdReproxyBridge *bridge = (EvdReproxyBridge *) user_data;
  EvdConnection *conn;

  conn = bridge->conn;

  g_output_stream_flush_finish (G_OUTPUT_STREAM (obj), res, NULL);

  if (! g_io_stream_is_closed (G_IO_STREAM (conn)))
    g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);

  g_free (bridge->buf);
  g_free (bridge);

  g_object_unref (conn);
}

static void
evd_reproxy_connection_on_close (EvdConnection *conn, gpointer user_data)
{
  EvdReproxyBridge *bridge;
  GOutputStream *stream;

  bridge = g_object_get_data (G_OBJECT (conn), BRIDGE_DATA_KEY);

  stream = g_io_stream_get_output_stream (G_IO_STREAM (bridge->conn));
  g_output_stream_flush_async (stream,
                               evd_connection_get_priority (bridge->conn),
                               NULL,
                               evd_reproxy_connection_on_flush,
                               bridge);
}

static void
evd_reproxy_backend_on_connection (GObject      *obj,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
  EvdReproxy *self = EVD_REPROXY (user_data);
  EvdConnection *conn1;
  GError *error = NULL;

  if ( (conn1 =
        evd_connection_pool_get_connection_finish (EVD_CONNECTION_POOL (obj),
                                                   res,
                                                   &error)) != NULL)
    {
      EvdConnection *conn0;

      g_signal_connect (conn1,
                        "close",
                        G_CALLBACK (evd_reproxy_connection_on_close),
                        self);

      conn0 = g_queue_pop_head (self->priv->conns);

      g_signal_connect (conn0,
                        "close",
                        G_CALLBACK (evd_reproxy_connection_on_close),
                        self);

      evd_reproxy_connection_setup_bridge (self, conn0, conn1);
      evd_reproxy_connection_setup_bridge (self, conn1, conn0);

      g_object_unref (conn0);
    }
  else
    {
      g_debug ("reproxy new conn error: %s", error->message);
      g_error_free (error);

      /* @TODO: retry, but wisely */
    }
}

static void
evd_reproxy_connection_accepted (EvdService *service, EvdConnection *conn)
{
  EvdReproxy *self = EVD_REPROXY (service);
  EvdConnectionPool *backend;

  backend = evd_reproxy_get_backend_with_free_connections (self);
  if (backend == NULL)
    {
      backend = evd_reproxy_get_backend_from_node (self->priv->next_backend_node);
      evd_reproxy_hop_backend (self);
    }

  evd_connection_pool_get_connection (backend,
                                      NULL,
                                      evd_reproxy_backend_on_connection,
                                      self);

  evd_reproxy_enqueue_connection (self, conn);
}

/* public methods */

EvdReproxy *
evd_reproxy_new (void)
{
  EvdReproxy *self;

  self = g_object_new (EVD_TYPE_REPROXY, NULL);

  return self;
}

/**
 * evd_reproxy_add_backend:
 *
 * Returns: (transfer none): An #EvdConnectionPool object representing
            the new backend.
 **/
EvdConnectionPool *
evd_reproxy_add_backend (EvdReproxy *self, const gchar *address)
{
  EvdConnectionPool *backend;

  g_return_val_if_fail (EVD_IS_REPROXY (self), NULL);
  g_return_val_if_fail (address != NULL, NULL);

  backend = evd_connection_pool_new (address, EVD_TYPE_CONNECTION);

  self->priv->backends = g_list_append (self->priv->backends, backend);

  if (self->priv->next_backend_node == NULL)
    self->priv->next_backend_node = self->priv->backends;

  return backend;
}

void
evd_reproxy_remove_backend (EvdReproxy *self, EvdConnectionPool *backend)
{
  g_return_if_fail (EVD_IS_REPROXY (self));
  g_return_if_fail (EVD_IS_CONNECTION_POOL (self));

  self->priv->backends = g_list_remove_all (self->priv->backends, backend);
}
