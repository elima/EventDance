/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * test-all-js.c
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

#include <string.h>
#include <glib.h>
#include <gjs/gjs.h>

typedef struct
{
  GjsContext *context;
} GjsTestJSFixture;

gchar *js_test_dir;

static void
setup (GjsTestJSFixture *fix,
       gconstpointer     test_data)
{
  gchar *search_path[2];

  search_path[0] = g_build_filename (js_test_dir, NULL);
  search_path[1] = NULL;

  fix->context = gjs_context_new_with_search_path (search_path);
  g_free (search_path[0]);
}

static void
teardown (GjsTestJSFixture *fix,
          gconstpointer     test_data)
{
  gjs_memory_report ("before destroying context", FALSE);
  g_object_unref (fix->context);
  gjs_memory_report ("after destroying context", TRUE);
}

static void
test (GjsTestJSFixture *fix,
      gconstpointer     test_data)
{
  GError *error = NULL;
  gint code;

  gjs_context_eval_file (fix->context,
                         (const gchar*) test_data,
                         &code,
                         &error);
  g_free ((gchar *) test_data);

  if (error != NULL)
    {
      /* TODO: here we should decide if a failing test aborts
         the process. By now, just log and continue. */

      g_error ("%s", error->message);

      g_assert (error == NULL);
    }
}

gint
main (gint argc, gchar *argv[])
{
  gchar *test_dir;
  const gchar *name;
  GDir *dir;

  g_test_init (&argc, &argv, NULL);
  g_type_init ();

  test_dir = g_path_get_dirname (argv[0]);
  js_test_dir = g_build_filename (test_dir, "..", "js", NULL);
  g_free (test_dir);

  /* iterate through all 'test*.js' files in 'js_test_dir' */
  dir = g_dir_open (js_test_dir, 0, NULL);
  g_assert (dir != NULL);

  while ((name = g_dir_read_name (dir)) != NULL) {
    gchar *test_name;
    gchar *file_name;

    if (! (g_str_has_prefix (name, "test") &&
           g_str_has_suffix (name, ".js")))
      continue;

    /* pretty print, drop 'test' prefix and '.js' suffix from test name */
    test_name = g_strconcat ("/evd/js/", name + 4, NULL);
    test_name[strlen (test_name)-3] = '\0';

    file_name = g_build_filename (js_test_dir, name, NULL);
    g_test_add (test_name, GjsTestJSFixture, file_name, setup, test, teardown);

    g_free (test_name);
    /* not freeing file_name as it's needed while running the test */
  }
  g_dir_close (dir);

  return g_test_run ();
}
