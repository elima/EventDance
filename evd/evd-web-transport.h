/*
 * evd-web-transport.h
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

#ifndef __EVD_WEB_TRANSPORT_H__
#define __EVD_WEB_TRANSPORT_H__

#include <evd-web-dir.h>

#include <evd-web-selector.h>

G_BEGIN_DECLS

typedef struct _EvdWebTransport EvdWebTransport;
typedef struct _EvdWebTransportClass EvdWebTransportClass;
typedef struct _EvdWebTransportPrivate EvdWebTransportPrivate;

struct _EvdWebTransport
{
  EvdWebDir parent;

  EvdWebTransportPrivate *priv;
};

struct _EvdWebTransportClass
{
  EvdWebDirClass parent_class;
};

#define EVD_TYPE_WEB_TRANSPORT           (evd_web_transport_get_type ())
#define EVD_WEB_TRANSPORT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_WEB_TRANSPORT, EvdWebTransport))
#define EVD_WEB_TRANSPORT_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_WEB_TRANSPORT, EvdWebTransportClass))
#define EVD_IS_WEB_TRANSPORT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_WEB_TRANSPORT))
#define EVD_IS_WEB_TRANSPORT_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_WEB_TRANSPORT))
#define EVD_WEB_TRANSPORT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_WEB_TRANSPORT, EvdWebTransportClass))


GType               evd_web_transport_get_type             (void) G_GNUC_CONST;

EvdWebTransport    *evd_web_transport_new                  (const gchar *base_path);

void                evd_web_transport_set_selector         (EvdWebTransport *self,
                                                            EvdWebSelector  *selector);
EvdWebSelector     *evd_web_transport_get_selector         (EvdWebTransport *self);

const gchar        *evd_web_transport_get_base_path        (EvdWebTransport *self);

void                evd_web_transport_set_enable_websocket (EvdWebTransport *self,
                                                            gboolean         enabled);

G_END_DECLS

#endif /* __EVD_WEB_TRANSPORT_H__ */
