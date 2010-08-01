/*
 * evd-resolver.h
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

#ifndef __EVD_RESOLVER_H__
#define __EVD_RESOLVER_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdResolver EvdResolver;
typedef struct _EvdResolverClass EvdResolverClass;

struct _EvdResolver
{
  GObject parent;
};

struct _EvdResolverClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_RESOLVER           (evd_resolver_get_type ())
#define EVD_RESOLVER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_RESOLVER, EvdResolver))
#define EVD_RESOLVER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_RESOLVER, EvdResolverClass))
#define EVD_IS_RESOLVER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_RESOLVER))
#define EVD_IS_RESOLVER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_RESOLVER))
#define EVD_RESOLVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_RESOLVER, EvdResolverClass))

GType               evd_resolver_get_type             (void) G_GNUC_CONST;

EvdResolver        *evd_resolver_get_default          (void);

EvdResolver        *evd_resolver_new                  (void);

void                evd_resolver_resolve_async        (EvdResolver         *resolver,
                                                       const gchar         *address,
                                                       GCancellable        *cancellable,
                                                       GAsyncReadyCallback  callback,
                                                       gpointer             user_data);
GList              *evd_resolver_resolve_finish       (EvdResolver   *self,
                                                       GAsyncResult  *result,
                                                       GError       **error);

void                evd_resolver_free_addresses       (GList *addresses);

G_END_DECLS

#endif /* __EVD_RESOLVER_H__ */
