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

#include "evd-resolver.h"


G_DEFINE_TYPE (EvdResolver, evd_resolver, G_TYPE_OBJECT)

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
      GClosureMarshal marshal = g_cclosure_marshal_VOID__OBJECT;

      g_closure_set_marshal (closure, marshal);
    }

  return closure;
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
evd_resolver_resolve_request (EvdResolver         *self,
                              EvdResolverRequest  *request,
                              GError             **error)
{
  evd_resolver_request_set_resolver (request, (gpointer) self);

  return evd_resolver_request_resolve (request, error);
}

EvdResolverRequest *
evd_resolver_resolve_with_closure (EvdResolver  *self,
                                   const gchar  *address,
                                   GClosure     *closure,
                                   GError      **error)
{
  EvdResolverRequest *request;

  g_return_val_if_fail (EVD_IS_RESOLVER (self), NULL);
  g_return_val_if_fail (address != NULL, NULL);
  g_return_val_if_fail (closure != NULL, NULL);

  request = g_object_new (EVD_TYPE_RESOLVER_REQUEST,
                          "address", address,
                          "closure", closure,
                          NULL);

  if (evd_resolver_resolve_request (self, request, error))
    {
      return request;
    }
  else
    {
      g_object_unref (request);

      return NULL;
    }
}

EvdResolverRequest *
evd_resolver_resolve (EvdResolver                  *self,
                      const gchar                  *address,
                      EvdResolverOnResolveHandler   callback,
                      gpointer                      user_data,
                      GError                      **error)
{
  GClosure *closure;

  g_return_val_if_fail (EVD_IS_RESOLVER (self), FALSE);
  g_return_val_if_fail (address != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);

  closure = evd_resolver_build_closure (self, callback, user_data);

  return evd_resolver_resolve_with_closure (self,
                                            address,
                                            closure,
                                            error);
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
