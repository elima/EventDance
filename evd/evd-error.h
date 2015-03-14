/*
 * evd-error.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2015, Igalia S.L.
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

#ifndef __EVD_ERROR_H__
#define __EVD_ERROR_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include <glib.h>
#include <gnutls/gnutls.h>
#include <errno.h>

#define EVD_GNUTLS_ERROR_DOMAIN_STR "org.eventdance.lib.Gnutls.ErrorDomain"
#define EVD_GNUTLS_ERROR            g_quark_from_string (EVD_GNUTLS_ERROR_DOMAIN_STR)

#define EVD_ERRNO_ERROR_DOMAIN_STR "org.eventdance.lib.Errno.ErrorDomain"
#define EVD_ERRNO_ERROR            g_quark_from_string (EVD_ERRNO_ERROR_DOMAIN_STR)

gboolean     evd_error_propagate_gnutls     (gint     gnutls_error_code,
                                             GError **error);

#endif /* __EVD_ERROR_H__ */
