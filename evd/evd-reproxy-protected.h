/*
 * evd-reproxy-protected.h
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

#ifndef __EVD_REPROXY_PROTECTED_H__
#define __EVD_REPROXY_PROTECTED_H__

G_BEGIN_DECLS

gboolean evd_reproxy_client_awaiting       (EvdReproxy *self);

GList *  evd_reproxy_get_next_backend_node (EvdReproxy *self,
                                            GList      *backend_node);

gboolean evd_reproxy_new_bridge_available  (EvdReproxy *self,
                                            EvdSocket  *bridge);

void      evd_reproxy_notify_bridge_error (EvdReproxy *self,
                                           EvdSocket  *bridge);

G_END_DECLS

#endif /* __EVD_REPROXY_PROTECTED_H__ */
