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
  EVD_TLS_ERROR_CRED_INVALID_CERT,
  EVD_TLS_ERROR_CRED_INVALID_KEY,
  EVD_TLS_ERROR_CERT_UNKNOWN_TYPE,
  EVD_TLS_ERROR_CERT_NOT_INITIALIZED,
  EVD_TLS_ERROR_CERT_READ_PROPERTY,
  EVD_TLS_ERROR_SESS_NOT_INITIALIZED
} EvdTlsError;

typedef enum
{
  EVD_TLS_VERIFY_STATE_OK               = 0,
  EVD_TLS_VERIFY_STATE_NO_CERT          = 1 << 0,
  EVD_TLS_VERIFY_STATE_INVALID          = 1 << 1,
  EVD_TLS_VERIFY_STATE_REVOKED          = 1 << 2,
  EVD_TLS_VERIFY_STATE_SIGNER_NOT_FOUND = 1 << 3,
  EVD_TLS_VERIFY_STATE_SIGNER_NOT_CA    = 1 << 4,
  EVD_TLS_VERIFY_STATE_INSECURE_ALG     = 1 << 5,
  EVD_TLS_VERIFY_STATE_EXPIRED          = 1 << 6,
  EVD_TLS_VERIFY_STATE_NOT_ACTIVE       = 1 << 7
} EvdTlsVerifyState;

gboolean evd_tls_init              (GError **error);
void     evd_tls_deinit            (void);

void     evd_tls_build_error       (gint     error_code,
                                    GError **error,
                                    GQuark   domain);

void     evd_tls_free_certificates (GList *certificates);

G_END_DECLS

#endif /* __EVD_TLS_COMMON_H__ */
