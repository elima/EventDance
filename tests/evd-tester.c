/*
 * evd-tester.c
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

#include <sys/stat.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <gio/gio.h>

static gint
run_test (gchar   *filename,
          gchar   *argv[],
          GError **error)
{
  gint exit_status;

  argv[0] = filename;

  g_spawn_sync (".",
                argv,
                NULL,
                0,
                NULL,
                NULL,
                NULL,
                NULL,
                &exit_status,
                error);

  return exit_status;
}

static gboolean
is_a_test (const gchar *filename)
{
  gboolean is_a_test = FALSE;
  gchar *name;

  name = g_path_get_basename (filename);
  if (g_str_has_prefix (name, "test-"))
    {
      struct stat stat_buf = {0,};

      if (g_stat (filename, &stat_buf) == 0)
        {
          if ( ( (stat_buf.st_mode & S_IFREG) != 0) &&
               ( (stat_buf.st_mode & S_IXUSR) != 0) )
            is_a_test = TRUE;
        }
    }
  g_free (name);

  return is_a_test;
}

gint
main (gint argc, gchar *argv[])
{
  GDir *dir;
  gchar *current_dir;
  gchar *test_dir;
  const gchar *name;
  gboolean abort = FALSE;

  gchar *filename;
  GError *error = NULL;

  g_type_init ();

  if ( (argc > 1) &&
       (g_strcmp0 (argv[1], "--help") == 0 ||
        g_strcmp0 (argv[1], "-?") == 0) )
    {
      g_test_init (&argc, &argv, NULL);

      return 0;
    }

  current_dir = g_path_get_dirname (argv[0]);
  test_dir = g_build_filename (current_dir, "../", NULL);
  g_free (current_dir);

  dir = g_dir_open (test_dir, 0, NULL);
  g_assert (dir != NULL);

  while ( (! abort) && (name = g_dir_read_name (dir)) != NULL)
    {
      if (g_str_has_prefix (name, "test-all"))
        continue;

      filename = g_build_filename (test_dir, name, NULL);

      if (is_a_test (filename))
        {
          if (run_test (filename, argv, &error) != 0)
            {
              /* TODO: decide whether to abort running tests when one fails */

              g_error (error->message);
              abort = TRUE;

              g_error_free (error);
              error = NULL;
            }
        }

      g_free (filename);
    }
  g_dir_close(dir);

#ifdef HAVE_JS
  filename = g_build_filename (test_dir, "test-all-js", NULL);
  if (run_test (filename, argv, &error) != 0)
    {
      g_error (error->message);
      g_error_free (error);
    }
  g_free (filename);
#endif

  g_free (test_dir);

  return 0;
}
