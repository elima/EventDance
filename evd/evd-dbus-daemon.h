/*
 * evd-dbus-daemon.h
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

#ifndef __EVD_DBUS_DAEMON_H__
#define __EVD_DBUS_DAEMON_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdDBusDaemon EvdDBusDaemon;
typedef struct _EvdDBusDaemonClass EvdDBusDaemonClass;
typedef struct _EvdDBusDaemonPrivate EvdDBusDaemonPrivate;

struct _EvdDBusDaemon
{
  GObject parent;

  EvdDBusDaemonPrivate *priv;
};

struct _EvdDBusDaemonClass
{
  GObjectClass parent_class;

  /* virtual methods */

  /* signal prototypes */
};

#define EVD_TYPE_DBUS_DAEMON           (evd_dbus_daemon_get_type ())
#define EVD_DBUS_DAEMON(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_DBUS_DAEMON, EvdDBusDaemon))
#define EVD_DBUS_DAEMON_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_DBUS_DAEMON, EvdDBusDaemonClass))
#define EVD_IS_DBUS_DAEMON(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_DBUS_DAEMON))
#define EVD_IS_DBUS_DAEMON_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_DBUS_DAEMON))
#define EVD_DBUS_DAEMON_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_DBUS_DAEMON, EvdDBusDaemonClass))


GType             evd_dbus_daemon_get_type                (void) G_GNUC_CONST;

EvdDBusDaemon *   evd_dbus_daemon_new                     (const gchar  *config_file,
                                                           GError      **error);

G_END_DECLS

#endif /* __EVD_DBUS_DAEMON_H__ */
