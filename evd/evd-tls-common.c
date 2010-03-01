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

#include "evd-tls-common.h"

G_LOCK_DEFINE_STATIC (evd_tls_global);
static gint evd_tls_global_ref_count = 0;

void
evd_tls_global_init (void)
{
  G_LOCK (evd_tls_global);

  if (evd_tls_global_ref_count == 0)
    {
      /* to disallow usage of the blocking /dev/random */
      gcry_control (GCRYCTL_ENABLE_QUICK_RANDOM, 0);

      gnutls_global_init ();
    }
  evd_tls_global_ref_count++;

  G_UNLOCK (evd_tls_global);
}

void
evd_tls_global_deinit (void)
{
  G_LOCK (evd_tls_global);

  evd_tls_global_ref_count--;
  if (evd_tls_global_ref_count == 0)
    gnutls_global_deinit ();

  G_UNLOCK (evd_tls_global);
}
