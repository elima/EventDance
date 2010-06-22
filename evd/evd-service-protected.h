/*
 * evd-service-protected.h
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

#ifndef __EVD_SERVICE_PROTECTED_H__
#define __EVD_SERVICE_PROTECTED_H__

#include <evd-socket.h>
#include <evd-connection.h>

G_BEGIN_DECLS

gboolean evd_service_new_connection_protected    (EvdService    *self,
                                                  EvdConnection *conn);

gboolean evd_service_tls_started_protected       (EvdService    *self,
                                                  EvdConnection *conn);

gboolean evd_service_connection_closed_protected (EvdService    *self,
                                                  EvdConnection *conn);

G_END_DECLS

#endif /* __EVD_SERVICE_PROTECTED_H__ */
