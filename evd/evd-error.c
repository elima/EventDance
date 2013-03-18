/*
 * evd-error.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011-2013, Igalia S.L.
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

#include "evd-error.h"

void
evd_error_build_gnutls (gint     gnutls_error,
                        GError **error)
{
  g_set_error_literal (error,
                       EVD_GNUTLS_ERROR,
                       gnutls_error,
                       gnutls_strerror (gnutls_error));
}

/**
 * evd_error_propagate_gnutls:
 *
 * Since: 0.1.28
 **/
gboolean
evd_error_propagate_gnutls (gint gnutls_error_code, GError **error)
{
  if (gnutls_error_code == GNUTLS_E_SUCCESS)
    {
      return FALSE;
    }
  else
    {
      g_set_error_literal (error,
                           EVD_GNUTLS_ERROR,
                           gnutls_error_code,
                           gnutls_strerror (gnutls_error_code));
      return TRUE;
    }
}

void
evd_error_build_gcrypt (guint    gcrypt_error,
                        GError **error)
{
  g_set_error_literal (error,
                       EVD_GCRYPT_ERROR,
                       gcry_err_code (gcrypt_error),
                       gcry_strerror (gcrypt_error));
}
