/*
 * evd-pki-privkey.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011, Igalia S.L.
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

#include "evd-pki-privkey.h"

#include "evd-error.h"

G_DEFINE_TYPE (EvdPkiPrivkey, evd_pki_privkey, G_TYPE_OBJECT)

#define EVD_PKI_PRIVKEY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_PKI_PRIVKEY, \
                                          EvdPkiPrivkeyPrivate))

/* private data */
struct _EvdPkiPrivkeyPrivate
{
  gcry_sexp_t key_sexp;

  EvdPkiKeyType type;
};


/* properties */
enum
{
  PROP_0,
  PROP_TYPE
};

static void     evd_pki_privkey_class_init         (EvdPkiPrivkeyClass *class);
static void     evd_pki_privkey_init               (EvdPkiPrivkey *self);

static void     evd_pki_privkey_finalize           (GObject *obj);

static void     evd_pki_privkey_get_property       (GObject    *obj,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec);

static void
evd_pki_privkey_class_init (EvdPkiPrivkeyClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_pki_privkey_finalize;
  obj_class->get_property = evd_pki_privkey_get_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_TYPE,
                                   g_param_spec_uint ("type",
                                                      "Key type",
                                                      "The type of private key (RSA, DSA, etc)",
                                                      EVD_PKI_KEY_TYPE_UNKNOWN,
                                                      EVD_PKI_KEY_TYPE_DSA,
                                                      EVD_PKI_KEY_TYPE_UNKNOWN,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdPkiPrivkeyPrivate));
}

static void
evd_pki_privkey_init (EvdPkiPrivkey *self)
{
  EvdPkiPrivkeyPrivate *priv;

  priv = EVD_PKI_PRIVKEY_GET_PRIVATE (self);
  self->priv = priv;

  priv->key_sexp = NULL;

  self->priv->type = EVD_PKI_KEY_TYPE_UNKNOWN;
}

static void
evd_pki_privkey_finalize (GObject *obj)
{
  EvdPkiPrivkey *self = EVD_PKI_PRIVKEY (obj);

  if (self->priv->key_sexp != NULL)
    gcry_sexp_release (self->priv->key_sexp);

  G_OBJECT_CLASS (evd_pki_privkey_parent_class)->finalize (obj);
}

static void
evd_pki_privkey_get_property (GObject    *obj,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EvdPkiPrivkey *self;

  self = EVD_PKI_PRIVKEY (obj);

  switch (prop_id)
    {
    case PROP_TYPE:
      g_value_set_uint (value, self->priv->type);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
free_gstring_wisely (gpointer data)
{
  GString *st = data;

  g_string_free (st, st->str != NULL);
}

static void
decrypt_in_thread (GSimpleAsyncResult *res,
                   GObject            *object,
                   GCancellable       *cancellable)
{
  EvdPkiPrivkey *self = EVD_PKI_PRIVKEY (object);;
  gcry_sexp_t data_sexp;
  gcry_sexp_t ciph_sexp;
  gcry_error_t err;
  GError *error = NULL;
  const gchar *data;
  GString *result;
  gsize len;

  data_sexp = g_simple_async_result_get_op_res_gpointer (res);

  /* decrypt */
  err = gcry_pk_decrypt (&ciph_sexp, data_sexp, self->priv->key_sexp);
  if (err != GPG_ERR_NO_ERROR)
    {
      evd_error_build_gcrypt (err, &error);
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);
      goto out;
    }

  /* extract data */
  data = gcry_sexp_nth_data (ciph_sexp, 0, &len);
  result = g_string_new_len (data, len);
  gcry_sexp_release (ciph_sexp);

  g_simple_async_result_set_op_res_gpointer (res, result, free_gstring_wisely);

 out:
  g_object_unref (res);
}

/* public methods */

EvdPkiPrivkey *
evd_pki_privkey_new (void)
{
  return g_object_new (EVD_TYPE_PKI_PRIVKEY, NULL);
}

EvdPkiKeyType
evd_pki_privkey_get_key_type (EvdPkiPrivkey *self)
{
  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), -1);

  return self->priv->type;
}

gboolean
evd_pki_privkey_import_native (EvdPkiPrivkey  *self,
                               gpointer        privkey_st,
                               GError        **error)
{
  gcry_error_t err;
  gcry_sexp_t algo_sexp;
  gchar *algo_st;
  gboolean result = TRUE;

  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), FALSE);
  g_return_val_if_fail (privkey_st != NULL, FALSE);

  /* check if there are operations pending and return error if so */

  if (self->priv->key_sexp)
    {
      gcry_sexp_release (self->priv->key_sexp);
      self->priv->type = EVD_PKI_KEY_TYPE_UNKNOWN;
    }

  self->priv->key_sexp = (gcry_sexp_t) privkey_st;

  err = gcry_pk_testkey (self->priv->key_sexp);
  if (err != GPG_ERR_NO_ERROR)
    {
      evd_error_build_gcrypt (err, error);
      self->priv->key_sexp = NULL;

      return FALSE;
    }

  /* detect key algorithm */
  algo_sexp = gcry_sexp_nth (self->priv->key_sexp, 1);
  algo_st = gcry_sexp_nth_string (algo_sexp, 0);
  gcry_sexp_release (algo_sexp);

  if (g_strcmp0 (algo_st, "rsa") == 0)
    self->priv->type = EVD_PKI_KEY_TYPE_RSA;
  else if (g_strcmp0 (algo_st, "dsa") == 0)
    self->priv->type = EVD_PKI_KEY_TYPE_DSA;
  else
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Key algorithm not supported");
      self->priv->key_sexp = NULL;
      result = FALSE;
    }

  gcry_free (algo_st);

  return result;
}

void
evd_pki_privkey_decrypt (EvdPkiPrivkey       *self,
                         const gchar         *data,
                         gsize                size,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  gcry_sexp_t data_sexp;
  GSimpleAsyncResult *res;
  gcry_error_t err;

  g_return_if_fail (EVD_IS_PKI_PRIVKEY (self));

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_pki_privkey_decrypt);

  /* pack message into an S-expression */
  err = gcry_sexp_build (&data_sexp,
                         0,
                         "(enc-val (%s (a %b)))",
                         self->priv->type == EVD_PKI_KEY_TYPE_RSA ? "rsa" : "dsa",
                         size,
                         data);
  if (err != GPG_ERR_NO_ERROR)
    {
      GError *error = NULL;

      evd_error_build_gcrypt (err, &error);

      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
    }
  else
    {
      g_simple_async_result_set_op_res_gpointer (res,
                                                 data_sexp,
                                                 (GDestroyNotify) gcry_sexp_release);

      /* @TODO: use a thread pool to avoid overhead */
      g_simple_async_result_run_in_thread (res,
                                           decrypt_in_thread,
                                           G_PRIORITY_DEFAULT,
                                           cancellable);
    }
}

gchar *
evd_pki_privkey_decrypt_finish (EvdPkiPrivkey  *self,
                                GAsyncResult   *result,
                                gsize          *size,
                                GError        **error)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (result);

  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_pki_privkey_decrypt),
                        NULL);

  if (! g_simple_async_result_propagate_error (res, error))
    {
      GString *data;
      gchar *ret;

      data = g_simple_async_result_get_op_res_gpointer (res);
      ret = data->str;
      data->str = NULL;

      if (size != NULL)
        *size = data->len;

      return ret;
    }
  else
    return NULL;
}

void
evd_pki_privkey_encrypt (EvdPkiPrivkey       *self,
                         const gchar         *data,
                         gsize                size,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  evd_pki_privkey_decrypt (self, data, size, cancellable, callback, user_data);
}

gchar *
evd_pki_privkey_encrypt_finish (EvdPkiPrivkey  *self,
                                GAsyncResult   *result,
                                gsize          *size,
                                GError        **error)
{
  return evd_pki_privkey_decrypt_finish (self, result, size, error);
}
