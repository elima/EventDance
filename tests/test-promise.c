/*
 * test-promise.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2014, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

#include <glib.h>
#include <gio/gio.h>

#include <evd.h>

typedef struct
{
  GObject *some_object;
  GCancellable *cancellable;

  EvdDeferred *deferred;
  EvdDeferred *deferred1;

  GMainLoop *main_loop;

  guint num_listeners;
  guint num_callbacks;
} Fixture;

void
some_function_as_tag (void)
{
}

static void
fixture_setup (Fixture       *f,
               gconstpointer  test_data)
{
  f->some_object = g_object_new (G_TYPE_OBJECT, NULL);
  f->cancellable = g_cancellable_new ();

  f->deferred = evd_deferred_new (f->some_object,
                                  f->cancellable,
                                  some_function_as_tag);

  f->deferred1 = evd_deferred_new (NULL, NULL, NULL);

  f->main_loop = g_main_loop_new (NULL, FALSE);

  f->num_listeners = 0;
  f->num_callbacks = 0;
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  test_data)
{
  EvdPromise *promise;

  promise = evd_deferred_get_promise (f->deferred);
  g_assert_cmpint (G_OBJECT (promise)->ref_count, ==, 1);
  evd_deferred_unref (f->deferred);

  promise = evd_deferred_get_promise (f->deferred1);
  g_assert_cmpint (G_OBJECT (promise)->ref_count, ==, 1);
  evd_deferred_unref (f->deferred1);

  g_assert_cmpint (f->some_object->ref_count, ==, 1);
  g_object_unref (f->some_object);

  g_assert_cmpint (G_OBJECT (f->cancellable)->ref_count, ==, 1);
  g_object_unref (f->cancellable);

  g_main_loop_unref (f->main_loop);
}

static void
test_basic (Fixture       *f,
            gconstpointer  test_data)
{
  EvdPromise *promise;

  g_assert (f->deferred != NULL);

  promise = evd_deferred_get_promise (f->deferred);
  g_assert (G_IS_ASYNC_RESULT (promise));
  g_assert (EVD_IS_PROMISE (promise));

  g_assert (g_async_result_get_source_object (G_ASYNC_RESULT (promise)) ==
            f->some_object);
  g_assert_cmpint (f->some_object->ref_count, ==, 3);
  g_object_unref (f->some_object);

  g_assert (evd_promise_get_cancellable (promise) == f->cancellable);
  g_assert (g_async_result_get_user_data (G_ASYNC_RESULT (promise)) ==
            NULL);
  g_assert (g_async_result_is_tagged (G_ASYNC_RESULT (promise),
                                      some_function_as_tag));

  g_assert (f->deferred1 != NULL);

  promise = evd_deferred_get_promise (f->deferred1);
  g_assert (G_IS_ASYNC_RESULT (promise));
  g_assert (EVD_IS_PROMISE (promise));

  g_assert (evd_promise_get_cancellable (promise) == NULL);
  g_assert (g_async_result_get_source_object (G_ASYNC_RESULT (promise)) ==
            NULL);
  g_assert (g_async_result_get_user_data (G_ASYNC_RESULT (promise)) ==
            NULL);
  g_assert (g_async_result_is_tagged (G_ASYNC_RESULT (promise), NULL));
}

static void
test_results (Fixture       *f,
              gconstpointer  test_data)
{
  EvdPromise *promise;
  GError *error = NULL;

  /* result pointer */
  promise = evd_deferred_get_promise (f->deferred);

  g_object_ref (f->some_object);
  evd_deferred_set_result_pointer (f->deferred, f->some_object, g_object_unref);

  evd_deferred_complete (f->deferred);
  g_assert (evd_promise_get_result_pointer (promise) == f->some_object);
  g_assert (evd_promise_get_result_size (promise) == 0);
  g_assert (evd_promise_get_result_boolean (promise) == FALSE);
  g_assert (evd_promise_propagate_error (promise, &error) == FALSE);
  g_assert_no_error (error);

  evd_deferred_unref (f->deferred);

  /* result size */
  f->deferred = evd_deferred_new (NULL, NULL, NULL);
  promise = evd_deferred_get_promise (f->deferred);

  evd_deferred_set_result_size (f->deferred, -1);

  evd_deferred_complete (f->deferred);
  g_assert (evd_promise_get_result_pointer (promise) == NULL);
  g_assert (evd_promise_get_result_size (promise) == -1);
  g_assert (evd_promise_get_result_boolean (promise) == FALSE);
  g_assert (evd_promise_propagate_error (promise, &error) == FALSE);
  g_assert_no_error (error);

  evd_deferred_unref (f->deferred);

  /* result boolean */
  f->deferred = evd_deferred_new (NULL, NULL, NULL);
  promise = evd_deferred_get_promise (f->deferred);

  evd_deferred_set_result_boolean (f->deferred, TRUE);

  evd_deferred_complete (f->deferred);
  g_assert (evd_promise_get_result_pointer (promise) == NULL);
  g_assert (evd_promise_get_result_size (promise) == 0);
  g_assert (evd_promise_get_result_boolean (promise) == TRUE);
  g_assert (evd_promise_propagate_error (promise, &error) == FALSE);
  g_assert_no_error (error);

  evd_deferred_unref (f->deferred);

  /* result error */
  f->deferred = evd_deferred_new (NULL, NULL, NULL);
  promise = evd_deferred_get_promise (f->deferred);

  evd_deferred_take_result_error (f->deferred, g_error_new (G_IO_ERROR,
                                                            G_IO_ERROR_FAILED,
                                                            "Some dummy error"));

  evd_deferred_complete (f->deferred);
  g_assert (evd_promise_get_result_pointer (promise) == NULL);
  g_assert (evd_promise_get_result_size (promise) == 0);
  g_assert (evd_promise_get_result_boolean (promise) == FALSE);
  g_assert (evd_promise_propagate_error (promise, &error) == TRUE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);

  evd_deferred_unref (f->deferred);

  /* result no error */
  f->deferred = evd_deferred_new (NULL, NULL, NULL);
  promise = evd_deferred_get_promise (f->deferred);

  evd_deferred_complete (f->deferred);
  g_assert (evd_promise_propagate_error (promise, &error) == FALSE);
  g_assert_no_error (error);
}

static void
promise_on_resolved (GObject      *obj,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  GError *error = NULL;
  EvdPromise *promise;
  Fixture *f = user_data;

  g_assert (G_IS_OBJECT (obj));
  g_assert (obj == f->some_object);
  g_assert (EVD_IS_PROMISE (result));
  g_assert (f != NULL);

  g_assert (g_async_result_get_source_object (result) == obj);
  /* g_async_result_get_source_object() increases reference count */
  g_object_unref (obj);

  g_assert (g_async_result_get_user_data (result) == f);

  promise = EVD_PROMISE (result);

  g_assert (! evd_promise_propagate_error (promise, &error));
  g_assert_no_error (error);

  g_assert (evd_promise_get_result_pointer (promise) == f->some_object);
  g_assert (evd_promise_get_result_size (promise) == 0);
  g_assert (evd_promise_get_result_boolean (promise) == FALSE);

  f->num_callbacks++;
  if (f->num_callbacks == f->num_listeners)
    g_main_loop_quit (f->main_loop);
}

static void
test_then (Fixture       *f,
           gconstpointer  test_data)
{
  EvdPromise *promise;

  promise = evd_deferred_get_promise (f->deferred);

  f->num_listeners = 1;
  evd_promise_then (promise,
                    promise_on_resolved,
                    f);

  g_object_ref (f->some_object);
  evd_deferred_set_result_pointer (f->deferred, f->some_object, g_object_unref);

  evd_deferred_complete_in_idle (f->deferred);

  g_main_loop_run (f->main_loop);
}

static void
test_many_listeners (Fixture       *f,
                     gconstpointer  test_data)
{
  EvdPromise *promise;
  gint i;

  promise = evd_deferred_get_promise (f->deferred);

  g_object_ref (f->some_object);
  evd_deferred_set_result_pointer (f->deferred, f->some_object, g_object_unref);

  evd_deferred_complete_in_idle (f->deferred);

  for (i = 0; i < 10; i++)
    {
      f->num_listeners++;
      evd_promise_then (promise,
                        promise_on_resolved,
                        f);
    }

  g_main_loop_run (f->main_loop);
}

static void
promise_on_resolved_cancelled (GObject      *obj,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  GError *error = NULL;
  EvdPromise *promise;
  Fixture *f = user_data;

  g_assert (G_IS_OBJECT (obj));
  g_assert (EVD_IS_PROMISE (result));
  g_assert (f != NULL);

  promise = EVD_PROMISE (result);

  g_assert (evd_promise_propagate_error (promise, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  g_assert (evd_promise_get_result_pointer (promise) == NULL);
  g_assert (evd_promise_get_result_size (promise) == 0);
  g_assert (evd_promise_get_result_boolean (promise) == FALSE);

  g_main_loop_quit (f->main_loop);
}

static void
promise_on_cancelled (GCancellable *cancellable, gpointer user_data)
{
  Fixture *f = user_data;
  GError *error = NULL;

  g_set_error (&error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");
  evd_deferred_take_result_error (f->deferred, error);

  evd_deferred_complete_in_idle (f->deferred);
}

static void
test_cancel (Fixture       *f,
             gconstpointer  test_data)
{
  EvdPromise *promise;
  GCancellable *cancellable;

  promise = evd_deferred_get_promise (f->deferred);

  cancellable = evd_promise_get_cancellable (promise);
  g_signal_connect (cancellable,
                    "cancelled",
                    G_CALLBACK (promise_on_cancelled),
                    f);

  evd_promise_then (promise,
                    promise_on_resolved_cancelled,
                    f);

  evd_promise_cancel (promise);

  g_main_loop_run (f->main_loop);
}

gint
main (gint argc, gchar *argv[])
{
#ifndef GLIB_VERSION_2_36
  g_type_init ();
#endif

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/evd/promise/basic",
              Fixture,
              NULL,
              fixture_setup,
              test_basic,
              fixture_teardown);

  g_test_add ("/evd/promise/results",
              Fixture,
              NULL,
              fixture_setup,
              test_results,
              fixture_teardown);

  g_test_add ("/evd/promise/then",
              Fixture,
              NULL,
              fixture_setup,
              test_then,
              fixture_teardown);

  g_test_add ("/evd/promise/many-listeners",
              Fixture,
              NULL,
              fixture_setup,
              test_many_listeners,
              fixture_teardown);

  g_test_add ("/evd/promise/cancel",
              Fixture,
              NULL,
              fixture_setup,
              test_cancel,
              fixture_teardown);

  return g_test_run ();
}
