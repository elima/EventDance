/*
 * evd-pki.c
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

#include <gcrypt.h>

#include "evd-pki.h"

#include "evd-error.h"

typedef struct
{
  EvdPkiKeyType key_type;
  guint bit_length;
  gboolean transient;
  EvdPkiPrivkey *privkey;
  EvdPkiPubkey *pubkey;
} GenData;

static void
free_gen_data (gpointer _data)
{
  GenData *data = _data;

  if (data->privkey != NULL)
    g_object_unref (data->privkey);

  if (data->pubkey != NULL)
    g_object_unref (data->pubkey);

  g_slice_free (GenData, data);
}

static void
generate_keypair_in_thread (GSimpleAsyncResult *res,
                            GObject            *object,
                            GCancellable       *cancellable)
{
  GenData *data;
  gcry_sexp_t request_sexp, keys_sexp;
  gcry_sexp_t privkey_sexp, pubkey_sexp;
  gcry_error_t err = GPG_ERR_NO_ERROR;
  GError *error = NULL;
  gchar *bit_len_st;
  gchar *format;

  data = g_simple_async_result_get_op_res_gpointer (res);

  bit_len_st = g_strdup_printf ("%u", data->bit_length);
  format = g_strdup_printf ("(genkey (%s (nbits %d:%s) %s))",
                            data->key_type == EVD_PKI_KEY_TYPE_RSA ? "rsa" : "dsa",
                            (gint) strlen (bit_len_st),
                            bit_len_st,
                            data->transient ? "(transient-key)" : "");
  g_free (bit_len_st);

  /* pack gen parameters into S-exp */
  err = gcry_sexp_new (&request_sexp, format, strlen (format), 0);
  if (err == GPG_ERR_NO_ERROR)
    {
      /* generate key pair */
      err = gcry_pk_genkey (&keys_sexp, request_sexp);
      if (err == GPG_ERR_NO_ERROR)
        {
          /* extract generated public key */
          pubkey_sexp = gcry_sexp_find_token (keys_sexp,
                                              "public-key",
                                              strlen ("public-key"));

          privkey_sexp = gcry_sexp_find_token (keys_sexp,
                                               "private-key",
                                               strlen ("private-key"));

          /* create the key objects and import the native data into */
          data->privkey = evd_pki_privkey_new ();
          data->pubkey = evd_pki_pubkey_new ();

          if (! evd_pki_privkey_import_native (data->privkey,
                                               (gpointer) privkey_sexp,
                                               &error) ||
              ! evd_pki_pubkey_import_native (data->pubkey,
                                              (gpointer) pubkey_sexp,
                                              &error))
            {
              g_simple_async_result_set_from_error (res, error);
              g_error_free (error);

              gcry_sexp_release (pubkey_sexp);
              gcry_sexp_release (privkey_sexp);
            }

          gcry_sexp_release (keys_sexp);
        }

      gcry_sexp_release (request_sexp);
    }

  g_free (format);

  if (evd_error_propagate_gcrypt (err, &error))
    g_simple_async_result_take_error (res, error);

  g_object_unref (res);
}

/* public methods */

void
evd_pki_generate_key_pair (EvdPkiKeyType        key_type,
                           guint                bit_length,
                           gboolean             fast_but_insecure,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  GSimpleAsyncResult *res;
  GenData *data;

  res = g_simple_async_result_new (NULL,
                                   callback,
                                   user_data,
                                   evd_pki_generate_key_pair);

  if (key_type != EVD_PKI_KEY_TYPE_RSA)
    {
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_NOT_SUPPORTED,
                                       "Only RSA keys are currently supported");
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
      return;
    }

  data = g_slice_new0 (GenData);
  data->key_type = key_type;
  data->bit_length = bit_length;
  data->transient = fast_but_insecure;

  g_simple_async_result_set_op_res_gpointer (res, data, free_gen_data);

  g_simple_async_result_run_in_thread (res,
                                       generate_keypair_in_thread,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);
}

gboolean
evd_pki_generate_key_pair_finish (GAsyncResult   *result,
                                  EvdPkiPrivkey **privkey,
                                  EvdPkiPubkey  **pubkey,
                                  GError        **error)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (result);

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                     NULL,
                                                     evd_pki_generate_key_pair),
                        FALSE);

  if (! g_simple_async_result_propagate_error (res, error))
    {
      GenData *data;

      data = g_simple_async_result_get_op_res_gpointer (res);

      if (data->privkey != NULL)
        {
          *privkey = data->privkey;
          data->privkey = NULL;
        }

      if (data->pubkey != NULL)
        {
          *pubkey = data->pubkey;
          data->pubkey = NULL;
        }

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}
