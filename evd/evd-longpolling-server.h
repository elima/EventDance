/*
 * evd-longpolling-server.h
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

#ifndef __EVD_LONGPOLLING_SERVER_H__
#define __EVD_LONGPOLLING_SERVER_H__

#include <evd-web-service.h>

G_BEGIN_DECLS

typedef struct _EvdLongpollingServer EvdLongpollingServer;
typedef struct _EvdLongpollingServerClass EvdLongpollingServerClass;
typedef struct _EvdLongpollingServerPrivate EvdLongpollingServerPrivate;

struct _EvdLongpollingServer
{
  EvdWebService parent;

  EvdLongpollingServerPrivate *priv;
};

struct _EvdLongpollingServerClass
{
  EvdWebServiceClass parent_class;

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

#define EVD_TYPE_LONGPOLLING_SERVER           (evd_longpolling_server_get_type ())
#define EVD_LONGPOLLING_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_LONGPOLLING_SERVER, EvdLongpollingServer))
#define EVD_LONGPOLLING_SERVER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_LONGPOLLING_SERVER, EvdLongpollingServerClass))
#define EVD_IS_LONGPOLLING_SERVER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_LONGPOLLING_SERVER))
#define EVD_IS_LONGPOLLING_SERVER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_LONGPOLLING_SERVER))
#define EVD_LONGPOLLING_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_LONGPOLLING_SERVER, EvdLongpollingServerClass))


GType                  evd_longpolling_server_get_type          (void) G_GNUC_CONST;

EvdLongpollingServer * evd_longpolling_server_new               (void);

G_END_DECLS

#endif /* __EVD_LONGPOLLING_SERVER_H__ */
