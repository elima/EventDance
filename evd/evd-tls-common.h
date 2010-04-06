/*
 * evd-tls-common.h
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

#ifndef __EVD_TLS_COMMON_H__
#define __EVD_TLS_COMMON_H__

#include <glib.h>
#include <gnutls/gnutls.h>
#include <gcrypt.h>

G_BEGIN_DECLS

/* TLS mode (client vs. server) */
typedef enum
{
  EVD_TLS_MODE_CLIENT = GNUTLS_CLIENT,
  EVD_TLS_MODE_SERVER = GNUTLS_SERVER
} EvdTlsMode;

/* TLS error codes*/
typedef enum
{
  EVD_TLS_ERROR_SUCCESS               = GNUTLS_E_SUCCESS,
  EVD_TLS_ERROR_AGAIN                 = GNUTLS_E_AGAIN,
  EVD_TLS_ERROR_INTERRUPTED           = GNUTLS_E_INTERRUPTED,
  EVD_TLS_ERROR_UNEXPECTED_PACKET_LEN = GNUTLS_E_UNEXPECTED_PACKET_LENGTH,
  EVD_TLS_ERROR_UNKNOWN               = 1024,
  EVD_TLS_ERROR_INVALID_CRED_CERT,
  EVD_TLS_ERROR_INVALID_CRED_KEY
  EVD_TLS_ERROR_CERT_UNKNOWN_TYPE
} EvdTlsError;

gboolean evd_tls_init        (GError **error);
void     evd_tls_deinit      (void);

void     evd_tls_build_error (gint     error_code,
                              GError **error,
                              GQuark   domain);

G_END_DECLS

#endif /* __EVD_TLS_COMMON_H__ */
