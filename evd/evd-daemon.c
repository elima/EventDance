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
#include <pwd.h>

#include <glib/gprintf.h>
#include <gio/gio.h>

#include "evd-daemon.h"

#include "evd-utils.h"
#include "evd-error.h"

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

  gint exit_code;

  gchar *pid_file;
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

  priv->exit_code = 0;

  priv->pid_file = NULL;
}

static void
evd_daemon_finalize (GObject *obj)
{
  EvdDaemon *self = EVD_DAEMON (obj);

  g_main_loop_unref (self->priv->main_loop);

  g_free (self->priv->pid_file);

  G_OBJECT_CLASS (evd_daemon_parent_class)->finalize (obj);

  if (evd_daemon_default == self)
    evd_daemon_default = NULL;
}

static void
evd_daemon_on_user_interrupt (gint sig)
{
  signal (SIGINT, NULL);
  signal (SIGTERM, NULL);

  if (evd_daemon_default != NULL)
    evd_daemon_quit (evd_daemon_default, -sig);
}

/* public methods */

/**
 * evd_daemon_get_default: (constructor):
 * @argc: (allow-none):
 * @argv: (allow-none):
 *
 * Returns: (transfer full):
 **/
EvdDaemon *
evd_daemon_get_default (gint *argc, gchar **argv[])
{
  if (evd_daemon_default == NULL)
    evd_daemon_default = evd_daemon_new (argc, argv);
  else
    g_object_ref (evd_daemon_default);

  return evd_daemon_default;
}

/**
 * evd_daemon_new: (constructor):
 * @argv: (allow-none):
 *
 * Returns: (transfer full):
 **/
EvdDaemon *
evd_daemon_new (gint *argc, gchar **argv[])
{
  EvdDaemon *self;
  gboolean daemonize = FALSE;
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

gint
evd_daemon_run (EvdDaemon *self, GError **error)
{
  g_return_val_if_fail (EVD_IS_DAEMON (self), -1);
  g_return_val_if_fail (! g_main_loop_is_running (self->priv->main_loop), -1);

  /* daemonize */
  if (! self->priv->daemonized && self->priv->daemonize)
    {
      if (! evd_daemon_daemonize (self, error))
        return -1;
    }

  /* write PID file */
  if (self->priv->pid_file != NULL)
    {
      const gint BUF_SIZE = 20;
      gint pid;
      gchar buf[BUF_SIZE];

      pid = getpid ();
      g_snprintf (buf, BUF_SIZE - 1, "%d\n", pid);
      buf[BUF_SIZE - 1] = '\0';

      if (! g_file_set_contents (self->priv->pid_file, buf, -1, error))
        return -1;

      /* force PID-file permissions to 00644 */
      if (chmod (self->priv->pid_file,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0)
        {
          gchar *err_st;

          err_st = strerror (errno);
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "Failed to set permissions on PID file: %s",
                       err_st);
          g_free (err_st);

          return -1;
        }
    }

  /* hook SIGINT and SIGTERM if this is the default daemon */
  if (self == evd_daemon_default)
    {
      signal (SIGINT, evd_daemon_on_user_interrupt);
      signal (SIGTERM, evd_daemon_on_user_interrupt);
    }

  /* finally, run the main loop */
  g_main_loop_run (self->priv->main_loop);

  return self->priv->exit_code;
}

void
evd_daemon_quit (EvdDaemon *self, gint exit_code)
{
  g_return_if_fail (EVD_IS_DAEMON (self));

  g_main_loop_quit (self->priv->main_loop);
}

gboolean
evd_daemon_daemonize (EvdDaemon *self, GError **error)
{
  pid_t pid, sid;
  gchar *err_st;

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
  g_set_error (error,
               G_IO_ERROR,
               g_io_error_from_errno (errno),
               "Failed to daemonize process: %s",
               err_st);
  g_free (err_st);

  return FALSE;
}

/**
 * evd_daemon_set_timeout:
 * @function: (scope notified):
 *
 * Returns:
 **/
guint
evd_daemon_set_timeout (EvdDaemon   *self,
                        guint        timeout,
                        GSourceFunc  function,
                        gpointer     user_data)
{
  g_return_val_if_fail (EVD_IS_DAEMON (self), 0);

  return evd_timeout_add (g_main_loop_get_context (self->priv->main_loop),
                          timeout,
                          G_PRIORITY_DEFAULT,
                          function,
                          user_data);
}

gboolean
evd_daemon_set_user_id (EvdDaemon  *self,
                        gint        user_id,
                        GError    **error)
{
  g_return_val_if_fail (EVD_IS_DAEMON (self), FALSE);

  errno = 0;
  if (setuid (user_id) != 0)
    {
      g_set_error (error,
                   EVD_ERRNO_ERROR,
                   errno,
                   "%s",
                   strerror (errno));
      return FALSE;
    }

  return TRUE;
}

gboolean
evd_daemon_set_user (EvdDaemon    *self,
                     const gchar  *username,
                     GError      **error)
{
  struct passwd *buf;

  g_return_val_if_fail (EVD_IS_DAEMON (self), FALSE);
  g_return_val_if_fail (username != NULL, FALSE);

  errno = 0;
  buf = getpwnam (username);
  if (buf == NULL)
    {
      if (errno != 0)
        g_set_error (error,
                     EVD_ERRNO_ERROR,
                     errno,
                     "%s",
                     strerror (errno));
      else
        g_set_error (error,
                     G_IO_ERROR,
                     G_IO_ERROR_NOT_FOUND,
                     "User %s not found",
                     username);

      return FALSE;
    }

  return evd_daemon_set_user_id (self, buf->pw_uid, error);
}

void
evd_daemon_set_pid_file (EvdDaemon *self, const gchar *pid_file)
{
  g_return_if_fail (EVD_IS_DAEMON (self));

  if (g_main_loop_is_running (self->priv->main_loop))
    {
      g_warning ("Ignoring PID file change because daemon is already running");
      return;
    }

  g_free (self->priv->pid_file);
  self->priv->pid_file = NULL;

  if (pid_file != NULL)
    self->priv->pid_file = g_strdup (pid_file);
}

const gchar *
evd_daemon_get_pid_file (EvdDaemon *self)
{
  g_return_val_if_fail (EVD_IS_DAEMON (self), NULL);

  return self->priv->pid_file;
}
