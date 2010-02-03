/*
 * evd-service.h
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

#ifndef __EVD_SERVICE_H__
#define __EVD_SERVICE_H__

#include "evd-socket-group.h"

G_BEGIN_DECLS

typedef struct _EvdService EvdService;
typedef struct _EvdServiceClass EvdServiceClass;
typedef struct _EvdServicePrivate EvdServicePrivate;

struct _EvdService
{
  EvdSocketGroup parent;

  /* private structure */
  EvdServicePrivate *priv;
};

struct _EvdServiceClass
{
  EvdSocketGroupClass parent_class;

  /* virtual methods */
  void (* socket_on_close) (EvdService *self,
                            EvdSocket  *socket);

  /* signal prototypes */

 /**
  * EvdService::new-connection:
  * @self: The #EvdService emmitting the signal.
  * @socket: A new #EvdSocket representing the local end-point of the connection.
  *
  * This signal is invoked when a remote socket connects to any of the
  * listening sockets added to the service.
  */
  void (* new_connection)  (EvdService *self,
                            EvdSocket  *socket);

 /**
  * EvdService::close:
  * @self: The #EvdService emmitting the signal.
  * @socket: The #EvdSocket that closed the connection.
  *
  * This signal is invoked when any of the sockets added to the service closes its connection.
  */
  void (* close)           (EvdService *self);
};

#define EVD_TYPE_SERVICE           (evd_service_get_type ())
#define EVD_SERVICE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_SERVICE, EvdService))
#define EVD_SERVICE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_SERVICE, EvdServiceClass))
#define EVD_IS_SERVICE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_SERVICE))
#define EVD_IS_SERVICE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_SERVICE))
#define EVD_SERVICE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_SERVICE, EvdServiceClass))


GType           evd_service_get_type          (void) G_GNUC_CONST;

EvdService     *evd_service_new               (void);

void            evd_service_add_listener      (EvdService  *self,
                                               EvdSocket   *socket);

gboolean        evd_service_remove_listener   (EvdService *self,
                                               EvdSocket  *socket);

EvdSocket      *evd_service_listen            (EvdService   *self,
                                               const gchar  *address,
                                               GError      **error);

G_END_DECLS

#endif /* __EVD_SERVICE_H__ */
