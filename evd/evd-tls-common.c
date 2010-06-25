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
#include <glib-object.h>

#include <gcrypt.h>
#include <pthread.h>
#include <errno.h>

#include "evd-error.h"
#include "evd-tls-common.h"
#include "evd-tls-dh-generator.h"

G_LOCK_DEFINE_STATIC (evd_tls_init);
static gboolean evd_tls_initialized = FALSE;

static EvdTlsDhGenerator *evd_tls_dh_gen;

GCRY_THREAD_OPTION_PTHREAD_IMPL;

gboolean
evd_tls_init (GError **error)
{
  gboolean result = FALSE;

  G_LOCK (evd_tls_init);

  if (! evd_tls_initialized)
    {
      gint err_code;

      g_type_init ();

      g_thread_init (NULL);

      gcry_control (GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
      gcry_control (GCRYCTL_ENABLE_QUICK_RANDOM, 0);

      err_code = gnutls_global_init ();
      if (err_code == GNUTLS_E_SUCCESS)
        {
          evd_tls_dh_gen = evd_tls_dh_generator_new ();

          result = TRUE;
        }
      else
        {
          evd_tls_build_error (err_code,
                               error);

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
      g_object_unref (evd_tls_dh_gen);

      /* check why after calling 'gnutls_global_deinit', calling again
         'gnutls_global_init' throws a segfault */
      /* gnutls_global_deinit (); */

      evd_tls_initialized = FALSE;
    }

  G_UNLOCK (evd_tls_init);
}

void
evd_tls_build_error (gint     error_code,
                     GError **error)
{
  g_set_error_literal (error,
                       EVD_TLS_ERROR,
                       error_code,
                       gnutls_strerror (error_code));
}

void
evd_tls_free_certificates (GList *certificates)
{
  GList *node;

  node = certificates;
  while (node != NULL)
    {
      GObject *cert;

      cert = G_OBJECT (node->data);
      g_object_unref (cert);

      node = node->next;
    }

  g_list_free (certificates);
}

void
evd_tls_generate_dh_params (guint                bit_length,
                            gboolean             regenerate,
                            GAsyncReadyCallback  callback,
                            GCancellable        *cancellable,
                            gpointer             user_data)
{
  evd_tls_dh_generator_generate (evd_tls_dh_gen,
                                 bit_length,
                                 regenerate,
                                 callback,
                                 cancellable,
                                 user_data);
}

gpointer
evd_tls_generate_dh_params_finish (GAsyncResult  *result,
                                   GError       **error)
{
  return evd_tls_dh_generator_generate_finish (evd_tls_dh_gen,
                                               result,
                                               error);
}
