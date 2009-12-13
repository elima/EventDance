/*
 * evd-reproxy.c
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

#include <evd-reproxy.h>

G_DEFINE_TYPE (EvdReproxy, evd_reproxy, EVD_TYPE_SERVICE)

#define EVD_REPROXY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      EVD_TYPE_REPROXY, \
                                      EvdReproxyPrivate))

#define DEFAULT_BACKEND_MAX_BRIDGES      5
#define BLOCK_SIZE                  0xFFFF

#define SOCKET_DATA_KEY "socket-data"

typedef struct _EvdReproxySocketData EvdReproxySocketData;

/* private data */
struct _EvdReproxyPrivate
{
  GList *backend_nodes;
  GList *next_backend_node;

  guint backend_max_bridges;

  GQueue *awaiting_clients;

  gboolean enabled;
};

/* reproxy client data */
struct _EvdReproxySocketData
{
  EvdSocket *bridge;
  GString   *cache;
};

static void     evd_reproxy_class_init            (EvdReproxyClass *class);
static void     evd_reproxy_init                  (EvdReproxy *self);

static void     evd_reproxy_finalize              (GObject *obj);
static void     evd_reproxy_dispose               (GObject *obj);

static EvdSocket * evd_reproxy_bridge_sockets     (EvdReproxy *self,
                                                   EvdSocket  *socket,
                                                   EvdSocket  *bridge);

void            evd_reproxy_socket_on_read        (EvdSocketGroup *socket_group,
                                                   EvdSocket      *socket);

void            evd_reproxy_socket_on_write       (EvdSocketGroup *socket_group,
                                                   EvdSocket      *socket);

void            evd_reproxy_socket_on_close       (EvdService *service,
                                                   EvdSocket  *socket);

GList *         evd_reproxy_get_next_backend_node (EvdReproxy *self,
                                                   GList      *backend_node);

static gboolean evd_reproxy_redirect_data           (EvdSocket *from);

static void     evd_reproxy_socket_free_data        (EvdSocket *socket);

static void     evd_reproxy_client_free_cached_data (EvdSocket *socket);

static void
evd_reproxy_class_init (EvdReproxyClass *class)
{
  GObjectClass *obj_class;
  EvdSocketGroupClass *socket_group_class;
  EvdServiceClass *service_class;

  obj_class = G_OBJECT_CLASS (class);
  obj_class->dispose = evd_reproxy_dispose;
  obj_class->finalize = evd_reproxy_finalize;

  socket_group_class = EVD_SOCKET_GROUP_CLASS (class);
  socket_group_class->socket_on_read = evd_reproxy_socket_on_read;
  socket_group_class->socket_on_write = evd_reproxy_socket_on_write;

  service_class = EVD_SERVICE_CLASS (class);
  service_class->socket_on_close = evd_reproxy_socket_on_close;

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
  priv->backend_nodes = NULL;
  priv->backend_max_bridges = DEFAULT_BACKEND_MAX_BRIDGES;
  priv->next_backend_node = NULL;
  priv->awaiting_clients = g_queue_new ();
  priv->enabled = TRUE;
}

static void
evd_reproxy_free_backend (gpointer data, gpointer user_data)
{
  EvdReproxyBackend *backend = (EvdReproxyBackend *) data;
  g_object_unref (backend);
}

static void
evd_reproxy_dispose (GObject *obj)
{
  EvdReproxy *self = EVD_REPROXY (obj);
  EvdSocket *client;

  self->priv->enabled = FALSE;

  while ( (client =
           EVD_SOCKET (g_queue_pop_head (self->priv->awaiting_clients)))
          != NULL)
    {
      evd_reproxy_socket_free_data (client);
      evd_socket_close (client, NULL);
      g_object_unref (client);
    }

  g_debug ("[EvdReproxy] Destroying backends");
  g_list_foreach (self->priv->backend_nodes,
                  evd_reproxy_free_backend,
                  NULL);
  g_list_free (self->priv->backend_nodes);
  self->priv->backend_nodes = NULL;


  G_OBJECT_CLASS (evd_reproxy_parent_class)->dispose (obj);
}

static void
evd_reproxy_finalize (GObject *obj)
{
  EvdReproxy *self = EVD_REPROXY (obj);

  g_queue_free (self->priv->awaiting_clients);
  self->priv->awaiting_clients = NULL;

  G_OBJECT_CLASS (evd_reproxy_parent_class)->finalize (obj);
}

static void
evd_reproxy_socket_create_data (EvdSocket *socket)
{
  EvdReproxySocketData *data;

  data = (EvdReproxySocketData *)
    g_object_get_data (G_OBJECT (socket), SOCKET_DATA_KEY);

  if (data == NULL)
    {
      data = g_new0 (EvdReproxySocketData, 1);
      g_object_set_data (G_OBJECT (socket), SOCKET_DATA_KEY, data);
    }
  else
    {
      if (data->cache != NULL)
        evd_reproxy_client_free_cached_data (socket);
    }

  data->cache = NULL;
  data->bridge = NULL;
}

static EvdReproxySocketData *
evd_reproxy_socket_get_data (EvdSocket *socket)
{
  return (EvdReproxySocketData *)
    g_object_get_data (G_OBJECT (socket), SOCKET_DATA_KEY);
}

static void
evd_reproxy_socket_free_data (EvdSocket *socket)
{
  EvdReproxySocketData *data;

  data = evd_reproxy_socket_get_data (socket);
  if (data != NULL)
    {
      evd_reproxy_client_free_cached_data (socket);

      g_free (data);
      g_object_set_data (G_OBJECT (socket), SOCKET_DATA_KEY, NULL);
    }
}

static EvdSocket *
evd_reproxy_socket_get_bridge (EvdSocket *socket)
{
  EvdReproxySocketData *data;

  data = evd_reproxy_socket_get_data (socket);
  if (data != NULL)
    return data->bridge;
  else
    return NULL;
}

static void
evd_reproxy_socket_set_bridge (EvdSocket *socket,
                               EvdSocket *bridge)
{
  EvdReproxySocketData *data;

  data = evd_reproxy_socket_get_data (socket);
  if (data != NULL)
    data->bridge = bridge;
}

static void
evd_reproxy_client_cache_data (EvdSocket   *socket,
                               const gchar *buf,
                               gssize       size)
{
  EvdReproxySocketData *data;

  data = evd_reproxy_socket_get_data (socket);
  if (data != NULL)
    {
      if (data->cache == NULL)
        data->cache = g_string_new_len (buf, size);
      else
        g_string_append_len (data->cache, buf, size);
    }
}

static GString *
evd_reproxy_client_get_cached_data (EvdSocket *socket)
{
  EvdReproxySocketData *data;

  data = evd_reproxy_socket_get_data (socket);
  if (data != NULL)
    return data->cache;
  else
    return NULL;
}

static void
evd_reproxy_client_free_cached_data (EvdSocket *socket)
{
  EvdReproxySocketData *data;

  data = evd_reproxy_socket_get_data (socket);
  if (data->cache != NULL)
    {
      g_string_free (data->cache, TRUE);
      data->cache = NULL;
    }
}

static EvdReproxyBackend *
evd_reproxy_get_backend_from_node (GList *backend_node)
{
  return (backend_node != NULL) ?
    (EvdReproxyBackend *) backend_node->data : NULL;
}

static void
evd_reproxy_hop_backend (EvdReproxy *self)
{
  self->priv->next_backend_node =
    evd_reproxy_get_next_backend_node (self,
                                       self->priv->next_backend_node);
}

static void
evd_reproxy_enqueue_awaiting_client (EvdReproxy *self,
                                     EvdSocket  *socket)
{
  g_queue_push_tail (self->priv->awaiting_clients, (gpointer) socket);
}

static gboolean
evd_reproxy_redirect_data (EvdSocket *from)
{
  gchar buf[BLOCK_SIZE + 1];
  gssize read_size;
  gssize write_size;
  GError *error = NULL;
  EvdSocket *to;
  EvdReproxySocketData *data;
  gsize max_writable;

  to = evd_reproxy_socket_get_bridge (from);
  if (to == NULL)
    return FALSE;

  data = evd_reproxy_socket_get_data (from);

  max_writable = evd_socket_get_max_writable (to);
  //  g_debug ("max writable: %d", max_writable);

  if (max_writable == 0)
    {
      return FALSE;
    }

  if ( (read_size = evd_socket_read_buffer (from,
                                            buf,
                                            max_writable + 2,
                                            &error)) >= 0)
    {
      if (read_size > 0)
        {
          if ( (write_size = evd_socket_write_buffer (to,
                                                      buf,
                                                      read_size,
                                                      &error)) >= 0)
            {
              /* keep back read data that wasn't written */
              if (read_size > write_size)
                {
                  evd_socket_unread_buffer (from,
                                            (gchar *) (((guintptr) buf) + write_size),
                                            read_size - write_size,
                                            NULL);
                }

              if (! evd_reproxy_backend_is_bridge (from))
                {
                  if (evd_reproxy_backend_bridge_is_doubtful (to))
                    {
                      g_debug ("bridge is doubtful!");
                      evd_reproxy_client_cache_data (from, buf, read_size);
                    }
                }
              else
                {
                  evd_reproxy_client_free_cached_data (to);
                  evd_reproxy_backend_notify_bridge_activity (from);
                }
            }
          else
            {
              g_warning ("Failed to redirect data: %s", error->message);
              g_error_free (error);

              evd_socket_unread_buffer (from, buf, read_size, NULL);
            }
        }
    }
  else
    {
      g_warning ("Failed to redirect data: %s", error->message);
      g_error_free (error);
    }

  return FALSE;
}

EvdReproxyBackend *
evd_reproxy_get_backend_with_free_bridge (EvdReproxy *self)
{
  EvdReproxyBackend *backend;
  GList *orig_node;

  if (self->priv->next_backend_node == NULL)
    return NULL;

  orig_node = self->priv->next_backend_node;

  do
    {
      backend =
        evd_reproxy_get_backend_from_node (self->priv->next_backend_node);

      if (evd_reproxy_backend_has_free_bridges (backend))
        {
          return backend;
        }
      else
        {
          evd_reproxy_hop_backend (self);
        }
    }
  while (self->priv->next_backend_node != orig_node);

  return NULL;
}

static EvdSocket *
evd_reproxy_bridge_sockets (EvdReproxy *self,
                            EvdSocket  *socket,
                            EvdSocket  *bridge)
{
  evd_reproxy_socket_create_data (bridge);

  evd_reproxy_socket_set_bridge (bridge, socket);
  evd_reproxy_socket_set_bridge (socket, bridge);

  return bridge;
}

static EvdSocket *
evd_reproxy_find_free_bridge (EvdReproxy *self)
{
  EvdReproxyBackend *backend;

  if (self->priv->next_backend_node == NULL)
    return NULL;

  backend = evd_reproxy_get_backend_with_free_bridge (self);
  if (backend != NULL)
    {
      EvdSocket *bridge;

      bridge = evd_reproxy_backend_get_free_bridge (backend);
      return bridge;
    }
  else
    {
      return NULL;
    }
}

/* protected methods */

void
evd_reproxy_socket_on_read (EvdSocketGroup *socket_group,
                            EvdSocket      *socket)
{
  EvdReproxy *self = EVD_REPROXY (socket_group);
  EvdSocket *bridge;

  bridge = evd_reproxy_socket_get_bridge (socket);
  if (bridge == NULL)
    {
      /* new client */
      if (! self->priv->enabled)
        {
          g_object_unref (socket);
          return;
        }

      g_debug ("[EvdReproxy 0x%X] New client (%X)",
               (guint) (guintptr) self,
               (guint) (guintptr) socket);
      g_object_ref_sink (socket);

      evd_reproxy_socket_create_data (socket);

      bridge = evd_reproxy_find_free_bridge (self);

      if (bridge != NULL)
        {
          evd_reproxy_bridge_sockets (self, socket, bridge);
          evd_reproxy_hop_backend (self);
        }
      else
        {
          evd_reproxy_enqueue_awaiting_client (self, socket);
          return;
        }
    }

  if ( (bridge != NULL) && (evd_socket_can_write (bridge)) )
    evd_reproxy_redirect_data (socket);
}

void
evd_reproxy_socket_on_write (EvdSocketGroup *socket_group,
                             EvdSocket      *socket)
{
  EvdSocket *bridge;

  //  g_debug ("write!");

  bridge = evd_reproxy_socket_get_bridge (socket);
  if ( (bridge != NULL) && (evd_socket_can_read (bridge)) )
    evd_reproxy_redirect_data (bridge);
}

void
evd_reproxy_socket_on_close (EvdService *service,
                             EvdSocket  *socket)
{
  EvdReproxy *self = EVD_REPROXY (service);
  EvdSocket *bridge;
  EvdReproxyBackend *backend;

  g_debug ("[EvdReproxy] Socket closed (0x%X)", (guint) (guintptr) socket);

  bridge = evd_reproxy_socket_get_bridge (socket);
  if (bridge != NULL)
    {
      evd_reproxy_socket_set_bridge (bridge, NULL);

      evd_reproxy_socket_set_bridge (socket, NULL);
      if (! evd_socket_has_write_data_pending (bridge))
        evd_socket_close (bridge, NULL);
      else
        {
          /* TODO: check if this situation is ever possible */
        }
    }

  backend = evd_reproxy_backend_get_from_socket (socket);
  if (backend != NULL)
    {
      evd_reproxy_backend_bridge_closed (backend, socket);
    }
  else
    {
      g_debug ("[EvdReproxy] Client closed (0x%X)", (guint) (guintptr) socket);
      g_queue_remove (self->priv->awaiting_clients, (gconstpointer) socket);
      g_object_unref (socket);
    }

  evd_reproxy_socket_free_data (socket);
}

GList *
evd_reproxy_get_next_backend_node (EvdReproxy *self, GList *backend_node)
{
  if (backend_node != NULL)
    {
      if (backend_node->next != NULL)
        return backend_node->next;
      else
        return self->priv->backend_nodes;
    }

  return NULL;
}

gboolean
evd_reproxy_client_awaiting (EvdReproxy *self)
{
  return (g_queue_get_length (self->priv->awaiting_clients) > 0);
}

gboolean
evd_reproxy_new_bridge_available (EvdReproxy *self,
                                  EvdSocket  *bridge)
{
  EvdSocket *client;

  client = EVD_SOCKET (g_queue_pop_head (self->priv->awaiting_clients));
  if (client)
    {
      EvdReproxySocketData *data;

      evd_reproxy_bridge_sockets (self, client, bridge);

      data = evd_reproxy_socket_get_data (client);
      evd_reproxy_redirect_data (client);

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

void
evd_reproxy_notify_bridge_error (EvdReproxy *self,
                                 EvdSocket  *bridge)
{
  EvdSocket *client;

  client = evd_reproxy_socket_get_bridge (bridge);
  if (client)
    {
      GString *cache;

      evd_reproxy_socket_set_bridge (bridge, NULL);
      evd_reproxy_socket_set_bridge (client, NULL);

      cache = evd_reproxy_client_get_cached_data (client);
      if (cache != NULL)
        {
          evd_socket_unread_buffer (client, cache->str, cache->len, NULL);
          evd_reproxy_client_free_cached_data (client);
        }

      evd_reproxy_socket_on_read (EVD_SOCKET_GROUP (self),
                                  client);
    }
}

/* public methods */

EvdReproxy *
evd_reproxy_new (void)
{
  EvdReproxy *self;

  self = g_object_new (EVD_TYPE_REPROXY, NULL);

  return self;
}

void
evd_reproxy_add_backend (EvdReproxy      *self,
                         GSocketAddress  *address)
{
  EvdReproxyBackend *backend;

  g_return_if_fail (EVD_IS_REPROXY (self));
  g_return_if_fail (G_IS_SOCKET_ADDRESS (address));

  backend = evd_reproxy_backend_new (EVD_SERVICE (self),
                                     address);

  self->priv->backend_nodes = g_list_append (self->priv->backend_nodes,
                                             (gpointer) backend);

  if (self->priv->next_backend_node == NULL)
    self->priv->next_backend_node = self->priv->backend_nodes;
}

void
evd_reproxy_del_backend (EvdReproxy  *self,
                         const gchar *address)
{
  /* TODO */
}
