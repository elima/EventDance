/*
 * evd-resolver.c
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

#ifdef HAVE_GIO_UNIX
#include <gio/gunixsocketaddress.h>
#endif

#include "evd-resolver.h"

G_DEFINE_TYPE (EvdResolver, evd_resolver, G_TYPE_OBJECT)

typedef struct
{
  guint16 port;
  GList *addresses;
  EvdResolver *resolver;
} EvdResolverData;

static void     evd_resolver_class_init         (EvdResolverClass *class);
static void     evd_resolver_init               (EvdResolver *self);

static void     evd_resolver_finalize           (GObject *obj);

static EvdResolver *evd_resolver_default = NULL;

/**
 * evd_resolver_get_default:
 *
 * Returns: (transfer full):
 **/
EvdResolver *
evd_resolver_get_default (void)
{
  if (evd_resolver_default == NULL)
    evd_resolver_default = evd_resolver_new ();
  else
    g_object_ref (evd_resolver_default);

  return evd_resolver_default;
}

static void
evd_resolver_class_init (EvdResolverClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_resolver_finalize;
}

static void
evd_resolver_init (EvdResolver *self)
{
}

static void
evd_resolver_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_resolver_parent_class)->finalize (obj);

  if (obj == G_OBJECT (evd_resolver_default))
    evd_resolver_default = NULL;
}

static void
evd_resolver_free_data (gpointer _data)
{
  EvdResolverData *data = (EvdResolverData *) _data;

  g_object_unref (data->resolver);

  if (data->addresses != NULL)
    {
      g_list_foreach (data->addresses, (GFunc) g_object_unref, NULL);
      g_list_free (data->addresses);
    }

  g_slice_free (EvdResolverData, data);
}

static void
evd_resolver_on_resolver_result (GResolver    *resolver,
                                 GAsyncResult *async_result,
                                 gpointer      user_data)
{
  GList *result = NULL;
  GError *error = NULL;
  GTask *task = G_TASK (user_data);
  EvdResolverData *data = g_task_get_task_data (task);

  if ((result = g_resolver_lookup_by_name_finish (resolver,
				  async_result,
				  &error)) != NULL)
    {
      GList *node = result;
      GInetAddress *inet_addr;
      GSocketAddress *addr;

      while (node != NULL)
        {
          inet_addr = G_INET_ADDRESS (node->data);

          addr = g_inet_socket_address_new (inet_addr, data->port);
          data->addresses = g_list_append (data->addresses, addr);

          node = node->next;
        }

      g_resolver_free_addresses (result);
    }
  else
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  g_task_return_pointer (task, data->addresses, (GDestroyNotify) evd_resolver_free_addresses);
  data->addresses = NULL;
  g_object_unref (task);
}

/* public methods */

EvdResolver *
evd_resolver_new (void)
{
  EvdResolver *self;

  self = g_object_new (EVD_TYPE_RESOLVER, NULL);

  return self;
}

void
evd_resolver_resolve (EvdResolver         *self,
                      const gchar         *address,
                      GCancellable        *cancellable,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
  GTask *task;
  EvdResolverData *data;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, evd_resolver_resolve);

  data = g_slice_new0 (EvdResolverData);
  data->resolver = self;
  g_object_ref (self);

  g_task_set_task_data (task, data, (GDestroyNotify) evd_resolver_free_data);

  if (address[0] == '/')
    {
#ifdef HAVE_GIO_UNIX
      GSocketAddress *addr;

      /* assume unix address */
      /* TODO: improve this detection, seems very naive */
      addr = (GSocketAddress *) g_unix_socket_address_new (address);
      data->addresses = g_list_append (data->addresses, addr);
#else
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_SUPPORTED,
                               "Unix socket addresses are not supported");
      g_object_unref (task);
      return;
#endif
    }
  else
    {
      GNetworkAddress *net_addr;
      GSocketConnectable *connectable;
      gchar *domain;
      guint16 port;
      GError *error = NULL;

      if ( (connectable = g_network_address_parse (address, 0, &error)) == NULL)
        {
          g_task_return_error (task, error);
          g_object_unref (task);
          return;
        }
      else
        {
          GInetAddress *inet_addr;

          net_addr = G_NETWORK_ADDRESS (connectable);

          domain = g_strdup (g_network_address_get_hostname (net_addr));
          port = g_network_address_get_port (net_addr);

          g_object_unref (net_addr);

          /* at this point we have a valid port, and a host to validate,
             so let's build the request */
          data->port = port;

          inet_addr = g_inet_address_new_from_string (domain);
          if (inet_addr != NULL)
            {
              GSocketAddress *addr;

              addr = g_inet_socket_address_new (inet_addr, port);
              g_object_unref (inet_addr);
              data->addresses = g_list_append (data->addresses, addr);

              g_free (domain);
            }
          else
            {
              g_resolver_lookup_by_name_async (g_resolver_get_default (),
                          domain,
                          cancellable,
                          (GAsyncReadyCallback) evd_resolver_on_resolver_result,
                          g_object_ref (task));

              g_free (domain);

              g_object_unref (task);
              return;
            }
        }
    }

  g_task_return_pointer (task, data->addresses, (GDestroyNotify) evd_resolver_free_addresses);
  data->addresses = NULL;
  g_object_unref (task);
}

/**
 * evd_resolver_resolve_finish:
 *
 * Returns: (element-type GSocketAddress) (transfer full):
 **/
GList *
evd_resolver_resolve_finish (EvdResolver   *self,
                             GAsyncResult  *result,
                             GError       **error)
{
  g_return_val_if_fail (EVD_IS_RESOLVER (self), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        evd_resolver_resolve,
                        NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * evd_resolver_free_addresses:
 * @addresses: (element-type GSocketAddress):
 **/
void
evd_resolver_free_addresses (GList *addresses)
{
  GList *node;

  node = addresses;
  while (node != NULL)
    {
      GSocketAddress *addr;

      addr = G_SOCKET_ADDRESS (node->data);
      g_object_unref (addr);

      node = node->next;
    }

  g_list_free (addresses);
}
