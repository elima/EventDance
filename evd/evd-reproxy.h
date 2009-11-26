/*
 * evd-reproxy.h
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

#ifndef __EVD_REPROXY_H__
#define __EVD_REPROXY_H__

#include <evd-service.h>
#include <evd-reproxy-backend.h>

G_BEGIN_DECLS

typedef struct _EvdReproxy EvdReproxy;
typedef struct _EvdReproxyClass EvdReproxyClass;
typedef struct _EvdReproxyPrivate EvdReproxyPrivate;

struct _EvdReproxy
{
  EvdService parent;

  /* private structure */
  EvdReproxyPrivate *priv;
};

struct _EvdReproxyClass
{
  EvdServiceClass parent_class;

  /* virtual methods */

  /* signal prototypes */
};

#define EVD_TYPE_REPROXY           (evd_reproxy_get_type ())
#define EVD_REPROXY(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_REPROXY, EvdReproxy))
#define EVD_REPROXY_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_REPROXY, EvdReproxyClass))
#define EVD_IS_REPROXY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_REPROXY))
#define EVD_IS_REPROXY_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_REPROXY))
#define EVD_REPROXY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_REPROXY, EvdReproxyClass))


GType           evd_reproxy_get_type          (void) G_GNUC_CONST;

EvdReproxy     *evd_reproxy_new               (void);

void            evd_reproxy_add_backend       (EvdReproxy      *self,
                                               GSocketAddress  *address);

void            evd_reproxy_del_backend       (EvdReproxy  *self,
                                               const gchar *address);

G_END_DECLS

#endif /* __EVD_REPROXY_H__ */
