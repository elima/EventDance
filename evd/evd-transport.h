/*
 * evd-transport.h
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
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#ifndef __EVD_TRANSPORT_H__
#define __EVD_TRANSPORT_H__

#include <evd-service.h>
#include <evd-peer.h>

G_BEGIN_DECLS

typedef struct _EvdTransport EvdTransport;
typedef struct _EvdTransportClass EvdTransportClass;
typedef struct _EvdTransportPrivate EvdTransportPrivate;

struct _EvdTransport
{
  EvdService parent;

  EvdTransportPrivate *priv;
};

struct _EvdTransportClass
{
  EvdServiceClass parent_class;

  /* virtual methods */
  gboolean (* peer_is_connected) (EvdTransport *self,
                                  EvdPeer      *peer);
  gboolean (* send)              (EvdTransport  *self,
                                  EvdPeer       *peer,
                                  const gchar   *buffer,
                                  gsize          size,
                                  GError       **error);
};

#define EVD_TYPE_TRANSPORT           (evd_transport_get_type ())
#define EVD_TRANSPORT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TRANSPORT, EvdTransport))
#define EVD_TRANSPORT_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TRANSPORT, EvdTransportClass))
#define EVD_IS_TRANSPORT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TRANSPORT))
#define EVD_IS_TRANSPORT_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TRANSPORT))
#define EVD_TRANSPORT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TRANSPORT, EvdTransportClass))


GType             evd_transport_get_type                 (void) G_GNUC_CONST;

EvdTransport     *evd_transport_new                      (void);

EvdPeer          *evd_transport_lookup_peer              (EvdTransport *self,
                                                          const gchar  *id);

GList            *evd_transport_get_all_peers            (EvdTransport *self);

EvdPeer          *evd_transport_get_new_peer_protected   (EvdTransport *self);

gssize            evd_transport_send                     (EvdTransport  *self,
                                                          EvdPeer       *peer,
                                                          const gchar   *buffer,
                                                          gsize          size,
                                                          GError       **error);

gboolean          evd_transport_peer_is_dead             (EvdTransport *self,
                                                          EvdPeer      *peer);

G_END_DECLS

#endif /* __EVD_TRANSPORT_H__ */
