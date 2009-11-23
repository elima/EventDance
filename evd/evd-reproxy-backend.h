/*
 * evd-reproxy-backend.h
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

#ifndef __EVD_REPROXY_BACKEND_H__
#define __EVD_REPROXY_BACKEND_H__

#include <glib-object.h>

#include <evd-service.h>

G_BEGIN_DECLS

typedef struct _EvdReproxyBackend EvdReproxyBackend;
typedef struct _EvdReproxyBackendClass EvdReproxyBackendClass;
typedef struct _EvdReproxyBackendPrivate EvdReproxyBackendPrivate;

struct _EvdReproxyBackend
{
  GObject parent;

  /* private structure */
  EvdReproxyBackendPrivate *priv;
};

struct _EvdReproxyBackendClass
{
  GObjectClass parent_class;

  /* virtual methods */

  /* signal prototypes */
};

#define EVD_TYPE_REPROXY_BACKEND           (evd_reproxy_backend_get_type ())
#define EVD_REPROXY_BACKEND(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_REPROXY_BACKEND, EvdReproxyBackend))
#define EVD_REPROXY_BACKEND_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_REPROXY_BACKEND, EvdReproxyBackendClass))
#define EVD_IS_REPROXY_BACKEND(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_REPROXY_BACKEND))
#define EVD_IS_REPROXY_BACKEND_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_REPROXY_BACKEND))
#define EVD_REPROXY_BACKEND_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_REPROXY_BACKEND, EvdReproxyBackendClass))


GType              evd_reproxy_backend_get_type          (void) G_GNUC_CONST;

EvdReproxyBackend *evd_reproxy_backend_new               (EvdService     *reproxy,
                                                          GSocketAddress *address);

EvdReproxyBackend *evd_reproxy_backend_get_from_socket   (EvdSocket *socket);

gboolean           evd_reproxy_backend_has_free_bridges  (EvdReproxyBackend *self);

gboolean           evd_reproxy_backend_is_bridge         (EvdSocket *socket);

EvdSocket *        evd_reproxy_backend_get_free_bridge   (EvdReproxyBackend *self);

void               evd_reproxy_backend_bridge_closed     (EvdReproxyBackend *self,
                                                          EvdSocket         *bridge);

gboolean           evd_reproxy_backend_bridge_is_doubtful (EvdSocket *bridge);

void               evd_reproxy_backend_notify_bridge_activity (EvdSocket *bridge);

G_END_DECLS

#endif /* __EVD_REPROXY_BACKEND_H__ */
