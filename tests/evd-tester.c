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
  struct stat stat_buf = {0,};

  if (! (g_str_has_prefix (filename, "test-")))
    return FALSE;

  if (g_stat (filename, &stat_buf) != 0)
    return FALSE;

  if ( ( (stat_buf.st_mode & S_IFREG) == 0) ||
       ( (stat_buf.st_mode & S_IXUSR) == 0) )
    return FALSE;

  return TRUE;
}

gint
main (gint argc, gchar *argv[])
{
  GDir *dir;
  const gchar *name;
  gboolean abort = FALSE;

  g_type_init ();

  if ( (argc > 1) &&
       (g_strcmp0 (argv[1], "--help") == 0 ||
        g_strcmp0 (argv[1], "-?") == 0) )
    {
      g_test_init (&argc, &argv, NULL);

      return 0;
    }

  dir = g_dir_open (".", 0, NULL);
  g_assert (dir != NULL);

  while ( (! abort) && (name = g_dir_read_name (dir)) != NULL)
    {
      gchar *filename;
      GError *error = NULL;

      if (! is_a_test (name))
        continue;

      filename = g_build_filename (".", name, NULL);

      if (run_test (filename, argv, &error) != 0)
        {
          /* TODO: decide whether to abort running tests when one fails */

          g_error (error->message);
          abort = TRUE;

          g_error_free (error);
          error = NULL;
        }

      g_free (filename);
    }

  g_dir_close(dir);

  return 0;
}
