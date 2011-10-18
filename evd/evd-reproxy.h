/*
 * evd-reproxy.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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

#ifndef __EVD_REPROXY_H__
#define __EVD_REPROXY_H__

#include <evd-service.h>

#include <evd-connection-pool.h>

G_BEGIN_DECLS

typedef struct _EvdReproxy EvdReproxy;
typedef struct _EvdReproxyClass EvdReproxyClass;
typedef struct _EvdReproxyPrivate EvdReproxyPrivate;

struct _EvdReproxy
{
  EvdService parent;

  EvdReproxyPrivate *priv;
};

struct _EvdReproxyClass
{
  EvdServiceClass parent_class;

  /* padding for future expansion */
  void (* _padding_0_) (void);
  void (* _padding_1_) (void);
  void (* _padding_2_) (void);
  void (* _padding_3_) (void);
  void (* _padding_4_) (void);
  void (* _padding_5_) (void);
  void (* _padding_6_) (void);
  void (* _padding_7_) (void);
};

#define EVD_TYPE_REPROXY           (evd_reproxy_get_type ())
#define EVD_REPROXY(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_REPROXY, EvdReproxy))
#define EVD_REPROXY_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_REPROXY, EvdReproxyClass))
#define EVD_IS_REPROXY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_REPROXY))
#define EVD_IS_REPROXY_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_REPROXY))
#define EVD_REPROXY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_REPROXY, EvdReproxyClass))


GType              evd_reproxy_get_type          (void) G_GNUC_CONST;

EvdReproxy        *evd_reproxy_new               (void);

EvdConnectionPool *evd_reproxy_add_backend       (EvdReproxy  *self,
                                                  const gchar *address);

void               evd_reproxy_remove_backend    (EvdReproxy        *self,
                                                  EvdConnectionPool *backend);

G_END_DECLS

#endif /* __EVD_REPROXY_H__ */
