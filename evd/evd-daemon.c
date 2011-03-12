/*
 * evd-daemon.c
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include <gio/gio.h>

#include "evd-daemon.h"

G_DEFINE_TYPE (EvdDaemon, evd_daemon, G_TYPE_OBJECT)

#define EVD_DAEMON_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                     EVD_TYPE_DAEMON, \
                                     EvdDaemonPrivate))

/* private data */
struct _EvdDaemonPrivate
{
  GMainLoop *main_loop;

  gboolean daemonize;
  gboolean daemonized;
};

static EvdDaemon *evd_daemon_default = NULL;

static void     evd_daemon_class_init         (EvdDaemonClass *class);
static void     evd_daemon_init               (EvdDaemon *self);

static void     evd_daemon_finalize           (GObject *obj);

static void
evd_daemon_class_init (EvdDaemonClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_daemon_finalize;

  g_type_class_add_private (obj_class, sizeof (EvdDaemonPrivate));
}

static void
evd_daemon_init (EvdDaemon *self)
{
  EvdDaemonPrivate *priv;

  priv = EVD_DAEMON_GET_PRIVATE (self);
  self->priv = priv;

  priv->main_loop = g_main_loop_new (g_main_context_get_thread_default (),
                                     FALSE);

  priv->daemonize = FALSE;
  priv->daemonized = FALSE;
}

static void
evd_daemon_finalize (GObject *obj)
{
  EvdDaemon *self = EVD_DAEMON (obj);

  g_main_loop_unref (self->priv->main_loop);

  G_OBJECT_CLASS (evd_daemon_parent_class)->finalize (obj);

  if (evd_daemon_default == self)
    evd_daemon_default = NULL;
}

static void
evd_daemon_on_user_interrupt (gint sig)
{
  signal (SIGINT, NULL);

  if (evd_daemon_default != NULL)
    evd_daemon_quit (evd_daemon_default);
}

/* public methods */

EvdDaemon *
evd_daemon_get_default (gint *argc, gchar **argv[])
{
  if (evd_daemon_default == NULL)
    evd_daemon_default = evd_daemon_new (argc, argv);

  return evd_daemon_default;
}

EvdDaemon *
evd_daemon_new (gint *argc, gchar **argv[])
{
  EvdDaemon *self;
  gboolean daemonize;
  GOptionContext *context;

  const GOptionEntry entries[] =
    {
      { "daemonize", 'D', 0, G_OPTION_ARG_NONE, &daemonize, NULL, NULL },
      { NULL }
    };

  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_help_enabled (context, FALSE);
  g_option_context_set_ignore_unknown_options (context, TRUE);
  g_option_context_parse (context, argc, argv, NULL); /* lets ignore any parsing error */
  g_option_context_free (context);

  self = g_object_new (EVD_TYPE_DAEMON, NULL);

  self->priv->daemonize = daemonize;

  if (evd_daemon_default == NULL)
    evd_daemon_default = self;

  return self;
}

GMainLoop *
evd_daemon_get_main_loop (EvdDaemon *self)
{
  g_return_val_if_fail (EVD_IS_DAEMON (self), NULL);

  return self->priv->main_loop;
}

void
evd_daemon_run (EvdDaemon *self)
{
  g_return_if_fail (EVD_IS_DAEMON (self));

  if (! self->priv->daemonized && self->priv->daemonize)
    {
      GError *error = NULL;

      if (! evd_daemon_daemonize (self, &error))
        {
          /* @TODO: log error */
          g_debug ("Error daemonizing: %s", error->message);
          g_error_free (error);
          return;
        }
    }

  if (self == evd_daemon_default)
    signal (SIGINT, evd_daemon_on_user_interrupt);

  g_main_loop_run (self->priv->main_loop);
}

void
evd_daemon_quit (EvdDaemon *self)
{
  g_return_if_fail (EVD_IS_DAEMON (self));

  g_main_loop_quit (self->priv->main_loop);
}

gboolean
evd_daemon_daemonize (EvdDaemon *self, GError **error)
{
  pid_t pid, sid;
  gchar *err_st;
  gchar *err_msg;

  errno = 0;

  /* already a daemon */
  if (self->priv->daemonized || getppid () == 1)
    return TRUE;

  /* Fork off the parent process */
  pid = fork ();
  if (pid < 0)
    goto error;

  /* If we got a good PID, then we can exit the parent process. */
  if (pid > 0)
    exit (0);

  /* At this point we are executing as the child process */

  /* Change the file mode mask */
  umask (0);

  /* Create a new SID for the child process */
  sid = setsid ();
  if (sid < 0)
    goto error;

  /* Change the current working directory.  This prevents the current
     directory from being locked; hence not being able to remove it. */
  if ((chdir ("/")) < 0)
    goto error;

  /* Redirect standard files to /dev/null */
  if (freopen ("/dev/null", "r", stdin) == NULL ||
      freopen ("/dev/null", "w", stdout) == NULL ||
      freopen ("/dev/null", "w", stderr) == NULL)
    {
      goto error;
    }

  self->priv->daemonized = TRUE;

  return TRUE;

 error:
  err_st = strerror (errno);
  err_msg = g_strdup_printf ("Failed to daemonize process: %s", err_st);
  g_free (err_st);
  g_set_error_literal (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       err_msg);
  g_free (err_msg);

  return FALSE;
}
