/*
 * evd-socket-protected.h
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

#ifndef __EVD_SOCKET_PROTECTED_H__
#define __EVD_SOCKET_PROTECTED_H__

#include "evd-socket.h"
#include "evd-socket-group.h"

G_BEGIN_DECLS

void            evd_socket_set_status          (EvdSocket *self,
                                                EvdSocketState status);
void            evd_socket_throw_error         (EvdSocket *self, GError *error);
void            evd_socket_handle_condition    (EvdSocket    *self,
                                                GIOCondition  condition);
void            evd_socket_set_group           (EvdSocket      *self,
                                                EvdSocketGroup *group);
EvdSocketGroup *evd_socket_get_group           (EvdSocket      *self);

void            evd_socket_invoke_on_read      (EvdSocket *self);
void            evd_socket_invoke_on_write     (EvdSocket *self);


gboolean        evd_socket_cleanup             (EvdSocket  *self,
                                                GError    **error);

EvdSocket      *evd_socket_accept              (EvdSocket  *self,
                                                GError    **error);

void            evd_socket_notify_condition    (EvdSocket    *self,
                                                GIOCondition  cond);

G_END_DECLS

#endif /* __EVD_SOCKET_PROTECTED_H__ */
