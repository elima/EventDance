/*
 * evd-tls-common.c
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

#include <glib.h>

#include "evd-tls-common.h"

#define DOMAIN_QUARK_STRING "org.eventdance.lib.tls-global"

G_LOCK_DEFINE_STATIC (evd_tls_init);
static gboolean evd_tls_initialized = FALSE;

gboolean
evd_tls_init (GError **error)
{
  gboolean result = FALSE;

  G_LOCK (evd_tls_init);

  if (! evd_tls_initialized)
    {
      gint err_code;

      err_code = gnutls_global_init ();
      if (err_code == GNUTLS_E_SUCCESS)
        {
          /* to disallow usage of the blocking /dev/random */
          gcry_control (GCRYCTL_ENABLE_QUICK_RANDOM, 0);

          result = TRUE;
        }
      else
        {
          evd_tls_build_error (err_code,
                               error,
                               g_quark_from_static_string (DOMAIN_QUARK_STRING));

          result = FALSE;
        }

      evd_tls_initialized = TRUE;
    }

  G_UNLOCK (evd_tls_init);

  return result;
}

void
evd_tls_deinit (void)
{
  G_LOCK (evd_tls_init);

  if (evd_tls_initialized)
    {
      gnutls_global_deinit ();

      evd_tls_initialized = FALSE;
    }

  G_UNLOCK (evd_tls_init);
}

void
evd_tls_build_error (gint     error_code,
                     GError **error,
                     GQuark   domain)
{
  if (error != NULL)
    {
      *error = g_error_new (domain,
                            error_code,
                            gnutls_strerror (error_code));
    }
}
