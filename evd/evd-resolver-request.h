/*
 * evd-resolver-request.h
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

#ifndef __EVD_RESOLVER_REQUEST_H__
#define __EVD_RESOLVER_REQUEST_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdResolverRequest EvdResolverRequest;
typedef struct _EvdResolverRequestClass EvdResolverRequestClass;
typedef struct _EvdResolverRequestPrivate EvdResolverRequestPrivate;

struct _EvdResolverRequest
{
  GObject parent;

  EvdResolverRequestPrivate *priv;
};

struct _EvdResolverRequestClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_RESOLVER_REQUEST           (evd_resolver_request_get_type ())
#define EVD_RESOLVER_REQUEST(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_RESOLVER_REQUEST, EvdResolverRequest))
#define EVD_RESOLVER_REQUEST_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_RESOLVER_REQUEST, EvdResolverRequestClass))
#define EVD_IS_RESOLVER_REQUEST(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_RESOLVER_REQUEST))
#define EVD_IS_RESOLVER_REQUEST_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_RESOLVER_REQUEST))
#define EVD_RESOLVER_REQUEST_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_RESOLVER_REQUEST, EvdResolverRequestClass))

GType                 evd_resolver_request_get_type               (void) G_GNUC_CONST;

void                  evd_resolver_request_set_resolver           (EvdResolverRequest *self,
                                                                   gpointer           *resolver);

void                  evd_resolver_request_reset                  (EvdResolverRequest *self);

gboolean              evd_resolver_request_resolve                (EvdResolverRequest  *self,
                                                                   GError             **error);

/**
 * evd_resolver_request_get_result:
 *
 * Returns: (transfer full) (element-type Gio.SocketAddress): The list of addresses resolved,
 *          or NULL on error, in which case @error is set accordingly.
 */
GList *               evd_resolver_request_get_result             (EvdResolverRequest  *self,
                                                                   GError             **error);

G_END_DECLS

#endif /* __EVD_RESOLVER_REQUEST_H__ */
