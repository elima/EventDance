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

#define DEFAULT_BACKEND_MAX_BRIDGES  5

#define BLOCK_SIZE              0xFFFF

/* private data */
struct _EvdReproxyPrivate
{
  GList *backend_nodes;

  guint backend_max_bridges;

  GQueue *awaiting_clients;

  GList *next_backend_node;
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

void            evd_reproxy_socket_on_close       (EvdService *service,
                                                   EvdSocket  *socket);

GList *         evd_reproxy_get_next_backend_node (EvdReproxy *self,
                                                   GList      *backend_node);

static void     evd_reproxy_redirect_data         (EvdSocket *from,
                                                   EvdSocket *to);

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
}

static void
evd_reproxy_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_reproxy_parent_class)->dispose (obj);
}

static void
evd_reproxy_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_reproxy_parent_class)->finalize (obj);
}

static EvdSocket *
evd_reproxy_socket_get_bridge (EvdSocket *socket)
{
  return g_object_get_data (G_OBJECT (socket), "bridge");
}

static void
evd_reproxy_socket_set_bridge (EvdSocket *socket,
                               EvdSocket *bridge)
{
  g_object_set_data (G_OBJECT (socket), "bridge", bridge);
}

static void
evd_reproxy_client_cache_data (EvdSocket   *socket,
                               const gchar *buf,
                               gssize       size)
{
  GString *cache;

  cache = g_object_get_data (G_OBJECT (socket), "data-cache");
  if (cache == NULL)
    {
      cache = g_string_new_len (buf, size);
      g_object_set_data (G_OBJECT (socket), "data-cache", cache);
    }
  else
    {
      g_string_append_len (cache, buf, size);
    }

  g_debug ("cached data from client");
}

static void
evd_reproxy_client_remove_cached_data (EvdSocket *socket)
{
  GString *cache;

  cache = g_object_get_data (G_OBJECT (socket), "data-cache");
  if (cache != NULL)
    {
      g_string_free (cache, TRUE);
      g_object_set_data (G_OBJECT (socket), "data-cache", NULL);

      g_debug ("removed cached data of client");
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
  g_debug ("socket added to waiting queue");
}

static void
evd_reproxy_redirect_data (EvdSocket *from,
                           EvdSocket *to)
{
  gchar buf[BLOCK_SIZE + 1];
  gssize size;
  GError *error = NULL;
  guint retry_wait;

  if ( (size = evd_socket_read_buffer (from, buf, BLOCK_SIZE, &retry_wait, &error)) > 0)
    {
      if (evd_socket_write_buffer (to, buf, size, NULL, &error) >= 0)
        {
          if (! evd_reproxy_backend_is_bridge (from))
            {
              if (evd_reproxy_backend_bridge_is_doubtful (to))
                {
                  g_debug ("bridge is doubtful!");
                  evd_reproxy_client_cache_data (from, buf, size);
                }
            }
          else
            {
              evd_reproxy_client_remove_cached_data (to);
              evd_reproxy_backend_notify_bridge_activity (from);
            }
        }
      else
        {
          if (retry_wait == 0)
            {
              g_warning ("Failed to redirect data: %s", error->message);
              g_error_free (error);
            }
          else
            {
              /* TODO: retry reading after 'retry_wait' miliseconds */
            }
        }
    }
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
  evd_reproxy_socket_set_bridge (bridge, socket);
  evd_reproxy_socket_set_bridge (socket, bridge);

  g_debug ("socket bridged!");

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
      g_debug ("new client (%X)", (guintptr) socket);

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

  evd_reproxy_redirect_data (socket, bridge);
}

void
evd_reproxy_socket_on_close (EvdService *service,
                             EvdSocket  *socket)
{
  EvdReproxy *self = EVD_REPROXY (service);
  EvdSocket *bridge;
  EvdReproxyBackend *backend;

  bridge = evd_reproxy_socket_get_bridge (socket);
  if (bridge != NULL)
    {
      evd_reproxy_socket_set_bridge (bridge, NULL);
      evd_reproxy_socket_set_bridge (socket, NULL);
      evd_socket_close (bridge, NULL);
    }

  backend = evd_reproxy_backend_get_from_socket (socket);
  if (backend != NULL)
    {
      g_debug ("bridge closed (%X)", (guintptr) socket);

      evd_reproxy_backend_bridge_closed (backend, socket);
    }
  else
    {
      g_debug ("client closed (%X)", (guintptr) socket);

      evd_reproxy_client_remove_cached_data (socket);

      g_queue_remove (self->priv->awaiting_clients, (gconstpointer) socket);
    }

  g_object_unref (socket);
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
      evd_reproxy_bridge_sockets (self, client, bridge);
      evd_reproxy_redirect_data (client, bridge);
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

      cache = g_object_get_data (G_OBJECT (client), "data-cache");
      if (cache != NULL)
        {
          evd_socket_unread_buffer (client, cache->str, cache->len);
          evd_reproxy_client_remove_cached_data (client);
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
