/*
 * evd-dbus-daemon.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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

#include <errno.h>

#include "evd-dbus-daemon.h"

#include "evd-error.h"

#define EVD_DBUS_DAEMON_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_DBUS_DAEMON, \
                                          EvdDBusDaemonPrivate))

/* private data */
struct _EvdDBusDaemonPrivate
{
  GPid pid;
  gchar *addr;
  gchar *config_file;
};

/* signals */
enum
{
  SIGNAL_LAST
};

//static guint evd_dbus_daemon_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_CONFIG_FILE,
  PROP_ADDRESS
};

static void     evd_dbus_daemon_class_init          (EvdDBusDaemonClass *class);
static void     evd_dbus_daemon_init                (EvdDBusDaemon *self);

static void     evd_dbus_daemon_finalize            (GObject *obj);
static void     evd_dbus_daemon_set_property        (GObject      *obj,
                                                     guint         prop_id,
                                                     const GValue *value,
                                                     GParamSpec   *pspec);
static void     evd_dbus_daemon_get_property        (GObject    *obj,
                                                     guint       prop_id,
                                                     GValue     *value,
                                                     GParamSpec *pspec);

static void     evd_dbus_daemon_initable_iface_init (GInitableIface *iface);

static gboolean evd_dbus_daemon_initable_init       (GInitable     *initable,
                                                     GCancellable  *cancellable,
                                                     GError       **error);

G_DEFINE_TYPE_WITH_CODE (EvdDBusDaemon, evd_dbus_daemon, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                evd_dbus_daemon_initable_iface_init));

static void
evd_dbus_daemon_initable_iface_init (GInitableIface *iface)
{
  iface->init = evd_dbus_daemon_initable_init;
}

static void
evd_dbus_daemon_class_init (EvdDBusDaemonClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_dbus_daemon_finalize;
  obj_class->get_property = evd_dbus_daemon_get_property;
  obj_class->set_property = evd_dbus_daemon_set_property;

  g_object_class_install_property (obj_class, PROP_CONFIG_FILE,
                                   g_param_spec_string ("config-file",
                                                        "DBus configuration file",
                                                        "Filename of the configuration file to pass to the DBus daemon",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_ADDRESS,
                                   g_param_spec_string ("address",
                                                        "Address",
                                                        "DBus daemon address",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdDBusDaemonPrivate));
}

static void
evd_dbus_daemon_init (EvdDBusDaemon *self)
{
  EvdDBusDaemonPrivate *priv;

  priv = EVD_DBUS_DAEMON_GET_PRIVATE (self);
  self->priv = priv;

  priv->config_file = NULL;
  priv->pid = 0;
  priv->addr = NULL;
}

static void
evd_dbus_daemon_finalize (GObject *obj)
{
  EvdDBusDaemon *self = EVD_DBUS_DAEMON (obj);

  g_free (self->priv->config_file);
  g_free (self->priv->addr);

  if (self->priv->pid > 0)
    {
      kill (self->priv->pid, SIGTERM);
      g_spawn_close_pid (self->priv->pid);
    }

  G_OBJECT_CLASS (evd_dbus_daemon_parent_class)->finalize (obj);
}

static void
evd_dbus_daemon_set_property (GObject      *obj,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EvdDBusDaemon *self;

  self = EVD_DBUS_DAEMON (obj);

  switch (prop_id)
    {
    case PROP_CONFIG_FILE:
      self->priv->config_file = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_dbus_daemon_get_property (GObject    *obj,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EvdDBusDaemon *self;

  self = EVD_DBUS_DAEMON (obj);

  switch (prop_id)
    {
    case PROP_CONFIG_FILE:
      g_value_take_string (value, self->priv->config_file);
      break;

    case PROP_ADDRESS:
      g_value_set_string (value, self->priv->addr);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static GPid
spawn_dbus_daemon (const gchar *config_file, gint *stdout_fd, GError **error)
{
  gchar *cmdline;
  gchar **argv = NULL;
  GPid pid;

  cmdline = g_strdup_printf ("dbus-daemon --config-file %s --print-address --nofork",
                             config_file);
  argv = g_strsplit (cmdline, " ", 0);
  g_free (cmdline);

  if (! g_spawn_async_with_pipes (".",
                                  argv,
                                  NULL,
                                  G_SPAWN_DO_NOT_REAP_CHILD
                                  | G_SPAWN_SEARCH_PATH,
                                  NULL,
                                  NULL,
                                  &pid,
                                  NULL,
                                  stdout_fd,
                                  NULL,
                                  error))
    {
      pid = -1;
    }

  g_strfreev (argv);

  return pid;
}

static gboolean
evd_dbus_daemon_initable_init (GInitable     *initable,
                               GCancellable  *cancellable,
                               GError       **error)
{
  EvdDBusDaemon *self = EVD_DBUS_DAEMON (initable);
  gint stdout_fd;

  self->priv->pid = spawn_dbus_daemon (self->priv->config_file,
                                       &stdout_fd,
                                       error);

  if (self->priv->pid <= 0)
    {
      return FALSE;
    }
  else
    {
      gchar buf[256] = { 0, };
      gchar **lines;
      gssize size;

      errno = 0;
      size = read (stdout_fd, buf, 256);
      if (size >= 0)
        {
          lines = g_strsplit (buf, "\n", 0);
          self->priv->addr = g_strdup (lines[0]);
          g_strfreev (lines);

          return TRUE;
        }
      else
        {
          g_set_error (error,
                       EVD_ERRNO_ERROR,
                       errno,
                       "Failed to D-Bus daemon address from stdout: %s",
                       strerror (errno));

          return FALSE;
        }
    }
}

/* public methods */

EvdDBusDaemon *
evd_dbus_daemon_new (const gchar  *config_file,
                     GError      **error)
{
  g_return_val_if_fail (config_file != NULL, NULL);

  return EVD_DBUS_DAEMON (g_initable_new (EVD_TYPE_DBUS_DAEMON,
                                          NULL,
                                          error,
                                          "config-file", config_file,
                                          NULL));
}
