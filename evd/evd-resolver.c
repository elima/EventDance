/*
 * evd-resolver.c
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

#include <stdio.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gunixsocketaddress.h>
#endif

#include "evd-utils.h"
#include "evd-marshal.h"
#include "evd-resolver.h"


G_DEFINE_TYPE (EvdResolver, evd_resolver, G_TYPE_OBJECT)

typedef struct
{
  EvdResolver *self;
  GClosure    *closure;
  GList       *addresses;
  GError      *error;

  EvdResolverOnResolveHandler callback;
  gpointer                    user_data;
} EvdResolverResultData;

static void     evd_resolver_class_init         (EvdResolverClass *class);
static void     evd_resolver_init               (EvdResolver *self);

static void     evd_resolver_finalize           (GObject *obj);
static void     evd_resolver_dispose            (GObject *obj);

static EvdResolver *evd_resolver_default = NULL;

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

  obj_class->dispose = evd_resolver_dispose;
  obj_class->finalize = evd_resolver_finalize;
}

static void
evd_resolver_init (EvdResolver *self)
{
}

static void
evd_resolver_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_resolver_parent_class)->dispose (obj);
}

static void
evd_resolver_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_resolver_parent_class)->finalize (obj);

  if (obj == G_OBJECT (evd_resolver_default))
    evd_resolver_default = NULL;
}

/*
static void
evd_resolver_on_resolver_result (GResolver    *resolver,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
  EvdResolver *self;
  GList *result;
  GError *error = NULL;

  self = EVD_RESOLVER (user_data);

  if ((result = g_resolver_lookup_by_name_finish (resolver,
						  res,
						  &error)) != NULL)
    {
      GList *node = result;
      GSocketFamily family;

      g_object_get (self, "family", &family, NULL);

      while (node != NULL)
        {
          node = node->next;
        }

      g_resolver_free_addresses (result);
    }
  else
    {
    }
}
*/

 /*
static GClosure *
evd_resolver_build_closure (EvdResolver                 *self,
                            EvdResolverOnResolveHandler  callback,
                            gpointer                     user_data)
{
  GClosure *closure;

  closure = g_cclosure_new (G_CALLBACK (callback),
			    user_data,
			    NULL);

  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    {
      GClosureMarshal marshal = evd_marshal_VOID__OBJECT_BOXED_BOXED;

      g_closure_set_marshal (closure, marshal);
    }

  return closure;
}
 */

static void
evd_resolver_invoke_result_closure (EvdResolver *self,
                                    GClosure    *closure,
                                    GList       *addresses,
                                    GError      *error)
{
  GValue params[3] = { {0, } };

  g_value_init (&params[0], EVD_TYPE_RESOLVER);
  g_value_set_object (&params[0], self);

  //  g_value_init (&params[1], G_TYPE_BOXED);
  //  g_value_set_boxed (&params[1], addresses);

  g_value_init (&params[2], G_TYPE_BOXED);
  g_value_set_boxed (&params[2], error);

  g_object_ref (self);
  g_closure_invoke (closure, NULL, 3, params, NULL);
  g_object_unref (self);

  g_value_unset (&params[0]);
  g_value_unset (&params[1]);
  g_value_unset (&params[2]);
}

static gboolean
evd_resolver_on_deliver (gpointer user_data)
{
  EvdResolverResultData *data = (EvdResolverResultData *) user_data;

  if (data->closure != NULL)
    {
      evd_resolver_invoke_result_closure (data->self,
                                          data->closure,
                                          data->addresses,
                                          data->error);
      g_closure_unref (data->closure);
    }
  else
    {
      data->callback (data->self,
                      data->addresses,
                      data->error,
                      data->user_data);
    }

  g_object_unref (data->self);
  g_free (data);

  return FALSE;
}

static void
evd_resolver_queue_deliver_result (EvdResolver                 *self,
                                   GClosure                    *closure,
                                   EvdResolverOnResolveHandler  callback,
                                   gpointer                     user_data,
                                   GList                       *addresses,
                                   GError                      *error)
{
  EvdResolverResultData *data;

  data = g_new0 (EvdResolverResultData, 1);
  data->self = self;
  data->addresses = addresses;
  data->error = error;
  data->closure = closure;
  data->callback = callback;
  data->user_data = user_data;

  g_object_ref (self);

  if (data->closure != NULL)
    {
      g_closure_ref (closure);
      g_closure_sink (closure);
    }

  evd_timeout_add (NULL, 0, evd_resolver_on_deliver, data);
}

/* public methods */

EvdResolver *
evd_resolver_new (void)
{
  EvdResolver *self;

  self = g_object_new (EVD_TYPE_RESOLVER, NULL);

  return self;
}

gboolean
evd_resolver_resolve (EvdResolver                 *self,
                      const gchar                 *address,
                      EvdResolverOnResolveHandler  callback,
                      gpointer                     user_data)
{
  GSocketAddress *addr;
  GInetAddress *inet_addr;
  GList *list = NULL;

  gchar *split;
  gchar *host;
  gchar *port_str;
  gint port;

  g_return_val_if_fail (EVD_IS_RESOLVER (self), FALSE);
  g_return_val_if_fail (address != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);

#ifdef HAVE_GIO_UNIX
  if (address[0] == '/')
    {
      /* assume unix address */
      /* TODO: improve this detection, seems very naive */
      addr = (GSocketAddress *) g_unix_socket_address_new (address);

      list = g_list_append (list, (gpointer) addr);
      evd_resolver_queue_deliver_result (self,
                                         NULL,
                                         callback,
                                         user_data,
                                         list,
                                         NULL);
      return TRUE;
    }
#endif

  /* at this point address is expected to be of type inet
     and in the form host:port, where host can be also a domain name */
  host = g_strdup (address);
  split = g_strrstr (host, ":");
  if (split != NULL)
    {
      split[0] = '\0';
      port_str = (char *) ((void *) split) + 1;

      if (sscanf (port_str, "%d", &port) == 0)
        {
          g_debug ("error");
        }
    }
  else
    {
      g_debug ("error");
    }

  /* at this point we have a valid port, and a host to validate */
  inet_addr = g_inet_address_new_from_string (host);
  if (inet_addr != NULL)
    {
      addr = g_inet_socket_address_new (inet_addr, port);

      list = g_list_append (list, (gpointer) addr);
      evd_resolver_queue_deliver_result (self,
                                         NULL,
                                         callback,
                                         user_data,
                                         list,
                                         NULL);
    }
  else
    {
      /* TODO */
    }

  return TRUE;
}

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
