/*
 * evd-reproxy-backend.c
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <evd-reproxy-backend.h>
#include <evd-reproxy.h>
#include <evd-reproxy-protected.h>

G_DEFINE_TYPE (EvdReproxyBackend, evd_reproxy_backend, G_TYPE_OBJECT)

#define EVD_REPROXY_BACKEND_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                              EVD_TYPE_REPROXY_BACKEND, \
                                              EvdReproxyBackendPrivate))

#define DEFAULT_MIN_POOL_SIZE                    5
#define DEFAULT_MAX_POOL_SIZE                   50
#define DEFAULT_BRIDGE_IDLE_TIMEOUT  1000 * 60 * 1 /* in miliseconds, 5 minutes */

#define BRIDGE_DATA_KEY "bridge-data"

typedef struct _EvdReproxyBridgeData EvdReproxyBridgeData;

/* private data */
struct _EvdReproxyBackendPrivate
{
  gboolean enabled;

  guint min_pool_size;
  guint max_pool_size;

  GQueue *free_bridges;
  GQueue *busy_bridges;
  GQueue *connecting_bridges;

  GSocketAddress *address;

  EvdReproxy *reproxy;

  gulong bridge_idle_timeout;
};

/* backend bridge data */
struct _EvdReproxyBridgeData
{
  EvdReproxyBackend *backend;
  GTimeVal           last_activity_timeval;
};

static void     evd_reproxy_backend_class_init            (EvdReproxyBackendClass *class);
static void     evd_reproxy_backend_init                  (EvdReproxyBackend *self);

static void     evd_reproxy_backend_finalize              (GObject *obj);
static void     evd_reproxy_backend_dispose               (GObject *obj);

static guint    evd_reproxy_backend_count_all_bridges     (EvdReproxyBackend *self);

static void     evd_reproxy_backend_new_bridge            (EvdReproxyBackend *self);

static void     evd_reproxy_backend_free_bridge_data      (EvdSocket *bridge);

static void
evd_reproxy_backend_class_init (EvdReproxyBackendClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);
  obj_class->dispose = evd_reproxy_backend_dispose;
  obj_class->finalize = evd_reproxy_backend_finalize;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdReproxyBackendPrivate));
}

static void
evd_reproxy_backend_init (EvdReproxyBackend *self)
{
  EvdReproxyBackendPrivate *priv;

  priv = EVD_REPROXY_BACKEND_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->min_pool_size = DEFAULT_MIN_POOL_SIZE;
  priv->max_pool_size = DEFAULT_MAX_POOL_SIZE;

  priv->free_bridges = g_queue_new ();
  priv->busy_bridges = g_queue_new ();
  priv->connecting_bridges = g_queue_new ();

  priv->bridge_idle_timeout = DEFAULT_BRIDGE_IDLE_TIMEOUT;

  priv->enabled = TRUE;
}

static void
evd_reproxy_backend_free_bridge_pool (GQueue *pool)
{
  EvdSocket *bridge;

  while ( (bridge = EVD_SOCKET (g_queue_pop_head (pool))) != NULL)
    {
      g_debug ("[EvdReproxyBackend] Closing bridge 0x%X",
               (guintptr) bridge);
      evd_socket_close (bridge, NULL);
    }
}

static void
evd_reproxy_backend_dispose (GObject *obj)
{
  EvdReproxyBackend *self = EVD_REPROXY_BACKEND (obj);

  self->priv->enabled = FALSE;

  evd_reproxy_backend_free_bridge_pool (self->priv->free_bridges);
  evd_reproxy_backend_free_bridge_pool (self->priv->busy_bridges);
  evd_reproxy_backend_free_bridge_pool (self->priv->connecting_bridges);

  G_OBJECT_CLASS (evd_reproxy_backend_parent_class)->dispose (obj);
}

static void
evd_reproxy_backend_finalize (GObject *obj)
{
  EvdReproxyBackend *self = EVD_REPROXY_BACKEND (obj);

  g_queue_free (self->priv->free_bridges);
  g_queue_free (self->priv->busy_bridges);
  g_queue_free (self->priv->connecting_bridges);

  g_object_unref (self->priv->address);

  G_OBJECT_CLASS (evd_reproxy_backend_parent_class)->finalize (obj);

  g_debug ("[ReproxyBackend] Reproxy finalized (%X)", (guintptr) obj);
}

static EvdReproxyBridgeData *
evd_reproxy_backend_bridge_get_data (EvdSocket *socket)
{
  return g_object_get_data (G_OBJECT (socket), BRIDGE_DATA_KEY);
}

static void
evd_reproxy_backend_bridge_set_data (EvdSocket            *socket,
                                     EvdReproxyBridgeData *data)
{
  g_object_set_data (G_OBJECT (socket), BRIDGE_DATA_KEY, data);
}

static void
evd_reproxy_backend_bridge_set_last_activity_time (EvdSocket *socket)
{
  EvdReproxyBridgeData *data;

  data = evd_reproxy_backend_bridge_get_data (socket);
  if (data != NULL)
    g_get_current_time (&data->last_activity_timeval);
}

static gulong
evd_reproxy_backend_bridge_get_inactive_time (EvdSocket *bridge)
{
  EvdReproxyBridgeData *data;

  data = evd_reproxy_backend_bridge_get_data (bridge);
  if (data != NULL)
    {
      GTimeVal current_time;
      gulong elapsed;

      g_get_current_time (&current_time);

      elapsed = (current_time.tv_sec - data->last_activity_timeval.tv_sec) * 1000;
      elapsed += (current_time.tv_usec - data->last_activity_timeval.tv_usec) / 1000;

      return elapsed;
    }
  else
    {
      /* TODO: handle error, should never happen */
    }

  return 0;
}

static guint
evd_reproxy_backend_count_free_bridges (EvdReproxyBackend *self)
{
  return g_queue_get_length (self->priv->free_bridges);
}

static guint
evd_reproxy_backend_count_all_bridges (EvdReproxyBackend *self)
{
  return g_queue_get_length (self->priv->free_bridges) +
    g_queue_get_length (self->priv->busy_bridges) +
    g_queue_get_length (self->priv->connecting_bridges);
}

static void
evd_reproxy_backend_bridge_on_connect (EvdSocket *socket,
                                       gpointer   user_data)
{
  EvdReproxyBackend *self = (EvdReproxyBackend *) user_data;

  g_queue_remove (self->priv->connecting_bridges, (gconstpointer) socket);
  evd_reproxy_backend_bridge_set_last_activity_time (socket);

  if (evd_reproxy_client_awaiting (self->priv->reproxy))
    {
      EvdSocket *bridge;

      if (g_queue_get_length (self->priv->free_bridges) > 0)
        {
          g_queue_push_tail (self->priv->free_bridges, (gpointer) socket);
          bridge = EVD_SOCKET (g_queue_pop_head (self->priv->free_bridges));
        }
      else
        bridge = socket;

      g_queue_push_tail (self->priv->busy_bridges, (gpointer) bridge);

      evd_reproxy_new_bridge_available (self->priv->reproxy, bridge);
    }
  else
    {
      g_queue_push_tail (self->priv->free_bridges, (gpointer) socket);
    }

  if ( (evd_reproxy_client_awaiting (self->priv->reproxy)) ||
       (evd_reproxy_backend_count_free_bridges (self) <
        self->priv->min_pool_size) )
    {
      evd_reproxy_backend_new_bridge (self);
    }
}

static void
evd_reproxy_backend_check_inactive_bridge (gpointer data,
                                           gpointer user_data)
{
  EvdReproxyBackend *self = EVD_REPROXY_BACKEND (user_data);
  EvdSocket *bridge = EVD_SOCKET (data);
  gulong elapsed;

  elapsed = evd_reproxy_backend_bridge_get_inactive_time (bridge);
  if (elapsed >= self->priv->bridge_idle_timeout)
    evd_socket_close (bridge, NULL);
}

static void
evd_reproxy_backend_bridge_on_error (EvdSocket   *bridge,
                                     guint        code,
                                     const gchar *msg,
                                     gpointer     user_data)
{
  EvdReproxyBackend *self = EVD_REPROXY_BACKEND (user_data);
  gulong elapsed;

  elapsed = evd_reproxy_backend_bridge_get_inactive_time (bridge);
  self->priv->bridge_idle_timeout = MIN (self->priv->bridge_idle_timeout,
                                         elapsed);

  g_queue_foreach (self->priv->free_bridges,
                   evd_reproxy_backend_check_inactive_bridge,
                   (gpointer) self);

  evd_reproxy_notify_bridge_error (self->priv->reproxy, bridge);
}

static void
evd_reproxy_backend_connect_bridge (EvdReproxyBackend *self,
                                    EvdSocket         *bridge)
{
  GError *error = NULL;

  if (! evd_socket_connect_to (bridge,
                               self->priv->address,
                               &error))
    {
      /* TODO: what to do exactly? better error handling required! */
      g_warning ("Failed to connect to backend: %s", error->message);
      g_error_free (error);
    }
  else
    {
      g_queue_push_tail (self->priv->connecting_bridges,
                         (gpointer) bridge);
    }
}

static void
evd_reproxy_backend_new_bridge (EvdReproxyBackend *self)
{
  EvdSocket *bridge;
  EvdReproxyBridgeData *bridge_data;

  if (evd_reproxy_backend_count_all_bridges (self) >=
      self->priv->max_pool_size)
    {
      return;
    }

  bridge = evd_socket_new ();

  g_object_set (bridge,
                "group", EVD_SOCKET_GROUP (self->priv->reproxy),
                NULL);

  g_signal_connect (bridge,
                    "connect",
                    G_CALLBACK (evd_reproxy_backend_bridge_on_connect),
                    (gpointer) self);

  g_signal_connect (bridge,
                    "error",
                    G_CALLBACK (evd_reproxy_backend_bridge_on_error),
                    (gpointer) self);

  bridge_data = g_new0 (EvdReproxyBridgeData, 1);
  bridge_data->backend = self;

  evd_reproxy_backend_bridge_set_data (bridge, bridge_data);

  evd_reproxy_backend_connect_bridge (self, bridge);
}

static void
evd_reproxy_backend_free_bridge_data (EvdSocket *bridge)
{
  EvdReproxyBridgeData *bridge_data;

  bridge_data = evd_reproxy_backend_bridge_get_data (bridge);
  if (bridge_data)
    {
      g_free (bridge_data);
      evd_reproxy_backend_bridge_set_data (bridge, NULL);
    }
}

/* protected methods */

/* public methods */

EvdReproxyBackend *
evd_reproxy_backend_new (EvdService     *reproxy,
                         GSocketAddress *address)
{
  EvdReproxyBackend *self;

  g_return_val_if_fail (EVD_IS_SERVICE (reproxy), NULL);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), NULL);

  self = g_object_new (EVD_TYPE_REPROXY_BACKEND, NULL);

  self->priv->reproxy = EVD_REPROXY (reproxy);

  g_object_ref (address);
  self->priv->address = address;

  evd_reproxy_backend_new_bridge (self);

  return self;
}

EvdReproxyBackend *
evd_reproxy_backend_get_from_socket (EvdSocket *socket)
{
  EvdReproxyBridgeData *data = NULL;

  data = evd_reproxy_backend_bridge_get_data (socket);

  if (data != NULL)
    return data->backend;
  else
    return NULL;
}

gboolean
evd_reproxy_backend_has_free_bridges (EvdReproxyBackend *self)
{
  if (evd_reproxy_backend_count_free_bridges (self) > 0)
    {
      return TRUE;
    }
  else
    {
      evd_reproxy_backend_new_bridge (self);
      return FALSE;
    }
}

gboolean
evd_reproxy_backend_is_bridge (EvdSocket *socket)
{
  return (evd_reproxy_backend_bridge_get_data (socket) != NULL);
}

EvdSocket *
evd_reproxy_backend_get_free_bridge (EvdReproxyBackend *self)
{
  EvdSocket *bridge;

  bridge = EVD_SOCKET (g_queue_pop_head (self->priv->free_bridges));

  if (bridge != NULL)
    g_queue_push_tail (self->priv->busy_bridges, (gpointer) bridge);

  return bridge;
}

void
evd_reproxy_backend_bridge_closed (EvdReproxyBackend *self,
                                   EvdSocket         *bridge)
{
  g_queue_remove (self->priv->free_bridges, (gconstpointer) bridge);
  g_queue_remove (self->priv->busy_bridges, (gconstpointer) bridge);

  if ( (self->priv->enabled) &&
       ((evd_reproxy_client_awaiting (self->priv->reproxy)) ||
        (evd_reproxy_backend_count_all_bridges (self) <
         self->priv->min_pool_size)) )
    {
      evd_reproxy_backend_connect_bridge (self, bridge);
      g_debug ("[EvdReproxyBackend 0x%X] Bridge reused (%X)",
               (guintptr) self,
               (guintptr) bridge);
    }
  else
    {
      g_debug ("[EvdReproxyBackend 0x%X] Destroying bridge (%X)",
               (guintptr) self,
               (guintptr) bridge);
      evd_reproxy_backend_free_bridge_data (bridge);
      g_object_unref (bridge);
    }
}

gboolean
evd_reproxy_backend_bridge_is_doubtful (EvdSocket *bridge)
{
  EvdReproxyBackend *self;
  self = evd_reproxy_backend_get_from_socket (bridge);

  if (self != NULL)
    {
      if (evd_reproxy_backend_bridge_get_inactive_time (bridge) >
          self->priv->bridge_idle_timeout)
        return TRUE;
    }

  return FALSE;
}

void
evd_reproxy_backend_notify_bridge_activity (EvdSocket *bridge)
{
  EvdReproxyBackend *self;

  self = evd_reproxy_backend_get_from_socket (bridge);
  if (self != NULL)
    {
      gulong elapsed;
      elapsed = evd_reproxy_backend_bridge_get_inactive_time (bridge);

      self->priv->bridge_idle_timeout = MAX (self->priv->bridge_idle_timeout,
                                             elapsed);
    }

  evd_reproxy_backend_bridge_set_last_activity_time (bridge);
}
