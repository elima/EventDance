/*
 * test-tls-cipher.c
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2011, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

#include <string.h>
#include <evd.h>
#include <glib.h>

typedef struct
{
  gchar *test_name;
  EvdTlsCipherAlgo algorithm;
  EvdTlsCipherMode mode;
  gboolean auto_padding;
  gchar *text;
  gchar *key;
  gint error_code;
} TestCase;

typedef struct
{
  EvdTlsCipher *cipher;
  GMainLoop *main_loop;
  gchar *enc_data;
  gchar *out_data;
  TestCase *test_case;
} Fixture;

const TestCase test_cases[] =
{
  {
    "AES128/CBC/auto-padding",
    EVD_TLS_CIPHER_ALGO_AES128, EVD_TLS_CIPHER_MODE_CBC,
    TRUE,
    "This is a secret text",
    "This is a secret password",
    0
  },

  {
    "AES192/ECB/no-auto-padding",
    EVD_TLS_CIPHER_ALGO_AES192, EVD_TLS_CIPHER_MODE_ECB,
    FALSE,
    "This is a text aligned to 32----",
    "some password",
    0
  },

  {
    "AES256/CBC/no-auto-padding/error",
    EVD_TLS_CIPHER_ALGO_AES192, EVD_TLS_CIPHER_MODE_CBC,
    FALSE,
    "This text is not aligned to algorithm's block size boundary",
    "some super-secret password",
    G_IO_ERROR_INVALID_ARGUMENT
  },

  {
    "ARC256/ECB/auto-padding",
    EVD_TLS_CIPHER_ALGO_AES256, EVD_TLS_CIPHER_MODE_ECB,
    TRUE,
    "Once upon a time in a very very far away land...",
    "This is a very long secret key that will definitely be truncted",
    0
  }

};

void
fixture_setup (Fixture       *f,
               gconstpointer  test_data)
{
  const TestCase *tc = test_data;

  if (tc != NULL)
    {
      f->cipher = evd_tls_cipher_new_full (tc->algorithm, tc->mode);
      g_object_set (f->cipher,
                    "auto-padding", tc->auto_padding,
                    NULL);
    }
  else
    {
      f->cipher = evd_tls_cipher_new ();
    }

  f->main_loop = g_main_loop_new (NULL, FALSE);
  f->enc_data = NULL;
  f->out_data = NULL;
}

void
fixture_teardown (Fixture       *f,
                  gconstpointer  test_data)
{
  if (f->out_data != NULL)
    g_free (f->out_data);

  if (f->enc_data != NULL)
    g_free (f->enc_data);

  g_main_loop_unref (f->main_loop);
  g_object_unref (f->cipher);
}

static void
test_basic (Fixture       *f,
            gconstpointer  test_data)
{
  g_assert (EVD_IS_TLS_CIPHER (f->cipher));
}

static gboolean
compare_strings (const gchar *s1, const gchar *s2, gsize len)
{
  gsize i;

  for (i = 0; i < len; i++)
    if (s1[i] != s2[i])
      return FALSE;

  return TRUE;
}

static gboolean
quit (gpointer user_data)
{
  g_main_loop_quit (user_data);

  return FALSE;
}

static void
test_decrypt_callback (GObject      *obj,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  Fixture *f = user_data;
  TestCase *tc = f->test_case;
  GError *error = NULL;
  gsize size;

  g_assert (EVD_TLS_CIPHER (obj) == f->cipher);

  f->out_data = evd_tls_cipher_decrypt_finish (EVD_TLS_CIPHER (obj),
                                               res,
                                               &size,
                                               &error);

  g_assert_no_error (error);
  g_assert (f->out_data != NULL);
  g_assert_cmpint (size, ==, strlen (tc->text));
  g_assert (compare_strings (f->out_data, tc->text, size));

  g_assert_cmpstr (f->out_data, !=, f->enc_data);

  g_idle_add (quit, f->main_loop);
}

static void
test_encrypt_callback (GObject      *obj,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  Fixture *f = user_data;
  TestCase *tc = f->test_case;
  GError *error = NULL;
  gsize size;

  g_assert (EVD_TLS_CIPHER (obj) == f->cipher);

  f->enc_data = evd_tls_cipher_encrypt_finish (EVD_TLS_CIPHER (obj),
                                               res,
                                               &size,
                                               &error);

  if (tc->error_code != 0)
    {
      g_assert_error (error, G_IO_ERROR, tc->error_code);
      g_assert (f->enc_data == NULL);

      g_error_free (error);
      g_main_loop_quit (f->main_loop);

      return;
    }

  g_assert_no_error (error);
  g_assert (f->enc_data != NULL);

  g_assert_cmpint (size, >=, strlen (tc->text));
  g_assert (! compare_strings (f->enc_data, tc->text, size));

  evd_tls_cipher_decrypt (f->cipher,
                          f->enc_data,
                          size,
                          tc->key,
                          strlen (tc->key),
                          NULL,
                          test_decrypt_callback,
                          f);
}

static void
test_encrypt (Fixture       *f,
              gconstpointer  test_data)
{
  TestCase *tc = (TestCase *) test_data;

  f->test_case = tc;

  evd_tls_cipher_encrypt (f->cipher,
                          tc->text,
                          strlen (tc->text),
                          tc->key,
                          strlen (tc->key),
                          NULL,
                          test_encrypt_callback,
                          f);

  g_main_loop_run (f->main_loop);
}

gint
main (gint argc, gchar *argv[])
{
  gint i;

  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/evd/tls/cipher/basic",
              Fixture,
              NULL,
              fixture_setup,
              test_basic,
              fixture_teardown);

  for (i = 0; i < sizeof (test_cases) / sizeof (TestCase); i++)
    {
      gchar *test_name;

      test_name = g_strdup_printf ("/evd/tls/cipher/%s",
                                   test_cases[i].test_name);

      g_test_add (test_name,
                  Fixture,
                  &test_cases[i],
                  fixture_setup,
                  test_encrypt,
                  fixture_teardown);

      g_free (test_name);
    }

  return g_test_run ();
}
