/*
 * evd-ipc-mechanism.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2013, Igalia S.L.
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

#ifndef __EVD_IPC_MECHANISM_H__
#define __EVD_IPC_MECHANISM_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include <glib-object.h>

#include "evd-transport.h"

G_BEGIN_DECLS

typedef struct _EvdIpcMechanism EvdIpcMechanism;
typedef struct _EvdIpcMechanismClass EvdIpcMechanismClass;
typedef struct _EvdIpcMechanismPrivate EvdIpcMechanismPrivate;

struct _EvdIpcMechanism
{
  GObject parent;

  EvdIpcMechanismPrivate *priv;
};

struct _EvdIpcMechanismClass
{
  GObjectClass parent_class;

  /* virtual/abstract methods */
  void (* transport_receive)  (EvdIpcMechanism *self,
                               EvdTransport    *transport,
                               EvdPeer         *peer,
                               const guchar    *data,
                               gsize            size);
  void (* transport_new_peer) (EvdIpcMechanism *self,
                               EvdTransport    *transport,
                               EvdPeer         *peer);

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

#define EVD_TYPE_IPC_MECHANISM           (evd_ipc_mechanism_get_type ())
#define EVD_IPC_MECHANISM(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_IPC_MECHANISM, EvdIpcMechanism))
#define EVD_IPC_MECHANISM_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_IPC_MECHANISM, EvdIpcMechanismClass))
#define EVD_IS_IPC_MECHANISM(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_IPC_MECHANISM))
#define EVD_IS_IPC_MECHANISM_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_IPC_MECHANISM))
#define EVD_IPC_MECHANISM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_IPC_MECHANISM, EvdIpcMechanismClass))


GType                evd_ipc_mechanism_get_type                 (void) G_GNUC_CONST;

void                 evd_ipc_mechanism_use_transport            (EvdIpcMechanism    *self,
                                                                 EvdTransport       *transport);
void                 evd_ipc_mechanism_unuse_transport          (EvdIpcMechanism    *self,
                                                                 EvdTransport       *transport);

G_END_DECLS

#endif /* __EVD_IPC_MECHANISM_H__ */
