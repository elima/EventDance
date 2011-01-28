/*
 * evd-tls-cipher.c
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

#include <string.h>

#include "evd-tls-cipher.h"

#include "evd-error.h"

G_DEFINE_TYPE (EvdTlsCipher, evd_tls_cipher, G_TYPE_OBJECT);

#define EVD_TLS_CIPHER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                         EVD_TYPE_TLS_CIPHER, \
                                         EvdTlsCipherPrivate))

#define DEFAULT_ALGO EVD_TLS_CIPHER_ALGO_AES256
#define DEFAULT_MODE EVD_TLS_CIPHER_MODE_CBC

/* private data */
struct _EvdTlsCipherPrivate
{
  EvdTlsCipherAlgo algo;
  EvdTlsCipherMode mode;

  gboolean auto_padding;
};

typedef struct
{
  gcry_cipher_hd_t hd;
  const gchar *data;
  gsize data_size;
  gchar *out_data;
  gboolean encrypt;
  gchar *key;
} Session;

/* properties */
enum
{
  PROP_0,
  PROP_ALGO,
  PROP_MODE,
  PROP_AUTO_PADDING
};

static void     evd_tls_cipher_class_init          (EvdTlsCipherClass *class);
static void     evd_tls_cipher_init                (EvdTlsCipher *self);

static void     evd_tls_cipher_set_property        (GObject      *obj,
                                                    guint         prop_id,
                                                    const GValue *value,
                                                    GParamSpec   *pspec);
static void     evd_tls_cipher_get_property        (GObject    *obj,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec);

static void
evd_tls_cipher_class_init (EvdTlsCipherClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->get_property = evd_tls_cipher_get_property;
  obj_class->set_property = evd_tls_cipher_set_property;

  g_object_class_install_property (obj_class, PROP_ALGO,
                                   g_param_spec_uint ("algorithm",
                                                      "Cipher's algorithm",
                                                      "The algorithm to be used by the cipher",
                                                      0,
                                                      EVD_TLS_CIPHER_ALGO_LAST,
                                                      DEFAULT_ALGO,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_MODE,
                                   g_param_spec_uint ("mode",
                                                      "Cipher's mode",
                                                      "The algorithm's mode to be used by the cipher",
                                                      0,
                                                      EVD_TLS_CIPHER_MODE_LAST,
                                                      DEFAULT_MODE,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_AUTO_PADDING,
                                   g_param_spec_boolean ("auto-padding",
                                                         "Auto padding",
                                                         "Whether cipher should automatically align text to algorithm's block size boundary",
                                                         TRUE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdTlsCipherPrivate));
}

static void
evd_tls_cipher_init (EvdTlsCipher *self)
{
  EvdTlsCipherPrivate *priv;

  priv = EVD_TLS_CIPHER_GET_PRIVATE (self);
  self->priv = priv;
}

static void
evd_tls_cipher_set_property (GObject      *obj,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  EvdTlsCipher *self;

  self = EVD_TLS_CIPHER (obj);

  switch (prop_id)
    {
    case PROP_ALGO:
      self->priv->algo = g_value_get_uint (value);
      break;

    case PROP_MODE:
      self->priv->mode = g_value_get_uint (value);
      break;

    case PROP_AUTO_PADDING:
      self->priv->auto_padding = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_tls_cipher_get_property (GObject    *obj,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  EvdTlsCipher *self;

  self = EVD_TLS_CIPHER (obj);

  switch (prop_id)
    {
    case PROP_ALGO:
      g_value_set_uint (value, self->priv->algo);
      break;

    case PROP_MODE:
      g_value_set_uint (value, self->priv->mode);
      break;

    case PROP_AUTO_PADDING:
      g_value_set_boolean (value, self->priv->auto_padding);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_tls_cipher_build_gcry_error (gcry_error_t   gcry_err,
                                 GError       **error)
{
  gchar *err_msg;

  if (error == NULL)
    return;

  err_msg = g_strdup_printf ("%s: %s",
                             gcry_strsource (gcry_err),
                             gcry_strerror (gcry_err));
  g_set_error_literal (error,
                       EVD_TLS_GCRY_ERROR,
                       gcry_err,
                       err_msg);
  g_free (err_msg);
}

static gcry_cipher_hd_t
evd_tls_cipher_create_new_handler (EvdTlsCipher *self, GError **error)
{
  gcry_cipher_hd_t hd;
  gcry_error_t gcry_err;

  gcry_err = gcry_cipher_open (&hd, self->priv->algo, self->priv->mode, 0);
  if (gcry_err != 0)
    {
      evd_tls_cipher_build_gcry_error (gcry_err, error);
      return NULL;
    }

  return hd;
}


static Session *
evd_tls_cipher_new_session (gcry_cipher_hd_t    hd,
                            const gchar        *data,
                            gsize               data_size,
                            gchar              *key,
                            gboolean            encrypt)
{
  Session *s;

  s = g_slice_new0 (Session);
  s->hd = hd;
  s->data = data;
  s->data_size = data_size;
  s->key = key;
  s->encrypt = encrypt;

  return s;
}

static void
evd_tls_cipher_free_session (gpointer data)
{
  Session *s = (Session *) data;

  if (s->key != NULL)
    g_free (s->key);

  gcry_cipher_close (s->hd);

  g_slice_free (Session, s);
}

static void
evd_tls_cipher_do_in_thread (GSimpleAsyncResult *res,
                             GObject            *object,
                             GCancellable       *cancellable)
{
  EvdTlsCipher *self;
  Session *s;
  gcry_error_t gcry_err = 0;
  GError *error = NULL;
  gsize data_size;

  self =
    EVD_TLS_CIPHER (g_async_result_get_source_object (G_ASYNC_RESULT (res)));
  s = (Session *) g_simple_async_result_get_op_res_gpointer (res);

  data_size = s->data_size;

  if (s->encrypt)
    {
      gsize block_size;
      gchar *last_data = NULL;
      gsize main_data_size;

      block_size = gcry_cipher_get_algo_blklen (self->priv->algo);

      if (self->priv->auto_padding)
        {
          gsize remaining;
          gsize padding;
          gint i;

          remaining = s->data_size % block_size;
          padding = abs (block_size - remaining);
          main_data_size = data_size - remaining;

          last_data = g_slice_alloc (block_size);
          memcpy (last_data, s->data + main_data_size, remaining);
          for (i=remaining; i<block_size - 1; i++)
            last_data[i] = g_random_int_range (0, 256);
          last_data[block_size - 1] = padding;

          data_size += padding;

          s->data_size = data_size;
        }
      else
        {
          main_data_size = data_size;
        }

      s->out_data = g_malloc (data_size);

      if (main_data_size > 0)
        gcry_err = gcry_cipher_encrypt (s->hd,
                                        s->out_data,
                                        main_data_size,
                                        s->data,
                                        main_data_size);

      if (self->priv->auto_padding)
        {
          if (gcry_err == 0)
            gcry_err = gcry_cipher_encrypt (s->hd,
                                            s->out_data + main_data_size,
                                            block_size,
                                            last_data,
                                            block_size);

          g_slice_free1 (block_size, last_data);
        }
    }
  else
    {
      s->out_data = g_malloc (data_size);

      gcry_err = gcry_cipher_decrypt (s->hd,
                                      s->out_data,
                                      data_size,
                                      s->data,
                                      data_size);

      if (gcry_err == 0 && self->priv->auto_padding)
        {
          gsize padding;

          padding = s->out_data[data_size - 1];
          s->data_size = data_size - padding;

          memset (s->out_data + s->data_size, 0, padding);
        }
    }

  if (gcry_err != 0)
    {
      evd_tls_cipher_build_gcry_error (gcry_err, &error);
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);
    }

  g_object_unref (res);
  g_object_unref (self);
}

static void
evd_tls_cipher_do (EvdTlsCipher        *self,
                   const gchar         *data,
                   gsize                data_size,
                   const gchar         *key,
                   gsize                key_size,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data,
                   gboolean             encrypt)
{
  GSimpleAsyncResult *res;
  gcry_cipher_hd_t hd;
  GError *error = NULL;
  Session *s;
  gchar *_key = NULL;
  gsize block_size;

  g_return_if_fail (EVD_IS_TLS_CIPHER (self));

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   NULL);

  block_size = gcry_cipher_get_algo_blklen (self->priv->algo);

  if (data_size % block_size != 0)
    {
      if (!encrypt || self->priv->auto_padding == FALSE)
        {
          g_simple_async_result_set_error (res,
                                           G_IO_ERROR,
                                           G_IO_ERROR_INVALID_ARGUMENT,
                                           "Data size not aligned to algorithm's block size boundary");

          g_simple_async_result_complete_in_idle (res);
          g_object_unref (res);

          return;
        }
    }

  /* @TODO: implement a pool of gcry handlers to reuse them */
  hd = evd_tls_cipher_create_new_handler (self, &error);
  if (hd != NULL)
    {
      gcry_error_t gcry_err;
      gsize _key_size;

      _key_size = gcry_cipher_get_algo_keylen (self->priv->algo);
      _key = g_new0 (gchar, _key_size);
      memcpy (_key, key, MIN (_key_size, key_size));

      gcry_err = gcry_cipher_setkey (hd, _key, _key_size);
      if (gcry_err != 0)
        evd_tls_cipher_build_gcry_error (gcry_err, &error);
    }

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);

      return;
    }

  s = evd_tls_cipher_new_session (hd, data, data_size, _key, encrypt);
  g_simple_async_result_set_op_res_gpointer (res, s, evd_tls_cipher_free_session);

  /* @TODO: use a thread pool to avoid overhead */
  g_simple_async_result_run_in_thread (res,
                                       evd_tls_cipher_do_in_thread,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);
}

/* public methods */

EvdTlsCipher *
evd_tls_cipher_new (void)
{
  return g_object_new (EVD_TYPE_TLS_CIPHER, NULL);
}

EvdTlsCipher *
evd_tls_cipher_new_full (EvdTlsCipherAlgo algo, EvdTlsCipherMode mode)
{
  return g_object_new (EVD_TYPE_TLS_CIPHER,
                       "algorithm", algo,
                       "mode", mode,
                       NULL);
}

void
evd_tls_cipher_encrypt (EvdTlsCipher        *self,
                        const gchar         *data,
                        gsize                data_size,
                        const gchar         *key,
                        gsize                key_size,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  evd_tls_cipher_do (self,
                     data,
                     data_size,
                     key,
                     key_size,
                     cancellable,
                     callback,
                     user_data,
                     TRUE);
}

gchar *
evd_tls_cipher_encrypt_finish (EvdTlsCipher  *self,
                               GAsyncResult  *result,
                               gsize         *size,
                               GError       **error)
{
  g_return_val_if_fail (EVD_IS_TLS_CIPHER (self), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        NULL),
                        NULL);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    {
      Session *s;

      s = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

      if (size != NULL)
        *size = s->data_size;

      return s->out_data;
    }
  else
    {
      return NULL;
    }
}

void
evd_tls_cipher_decrypt (EvdTlsCipher        *self,
                        const gchar         *data,
                        gsize                data_size,
                        const gchar         *key,
                        gsize                key_size,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  evd_tls_cipher_do (self,
                     data,
                     data_size,
                     key,
                     key_size,
                     cancellable,
                     callback,
                     user_data,
                     FALSE);
}

gchar *
evd_tls_cipher_decrypt_finish (EvdTlsCipher  *self,
                               GAsyncResult  *result,
                               gsize         *size,
                               GError       **error)
{
  return evd_tls_cipher_encrypt_finish (self, result, size, error);
}
