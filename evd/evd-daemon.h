/*
 * evd-daemon.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011-2013, Igalia S.L.
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

#ifndef __EVD_DAEMON_H__
#define __EVD_DAEMON_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _EvdDaemon EvdDaemon;
typedef struct _EvdDaemonClass EvdDaemonClass;
typedef struct _EvdDaemonPrivate EvdDaemonPrivate;

struct _EvdDaemon
{
  GObject parent;

  EvdDaemonPrivate *priv;
};

struct _EvdDaemonClass
{
  GObjectClass parent_class;

  /* virtual methods */

  /* signal prototypes */
};

#define EVD_TYPE_DAEMON           (evd_daemon_get_type ())
#define EVD_DAEMON(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_DAEMON, EvdDaemon))
#define EVD_DAEMON_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_DAEMON, EvdDaemonClass))
#define EVD_IS_DAEMON(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_DAEMON))
#define EVD_IS_DAEMON_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_DAEMON))
#define EVD_DAEMON_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_DAEMON, EvdDaemonClass))


GType               evd_daemon_get_type                (void) G_GNUC_CONST;

EvdDaemon *         evd_daemon_get_default             (gint *argc, gchar **argv[]);
EvdDaemon *         evd_daemon_new                     (gint *argc, gchar **argv[]);

gint                evd_daemon_run                     (EvdDaemon  *self,
                                                        GError    **error);
void                evd_daemon_quit                    (EvdDaemon *self,
                                                        gint       exit_code);

gboolean            evd_daemon_daemonize               (EvdDaemon  *self,
                                                        GError    **error);

guint               evd_daemon_set_timeout             (EvdDaemon  *self,
                                                        guint       timeout,
                                                        GSourceFunc function,
                                                        gpointer    user_data);

gboolean            evd_daemon_set_user_id             (EvdDaemon  *self,
                                                        gint        user_id,
                                                        GError    **error);
gboolean            evd_daemon_set_user                (EvdDaemon    *self,
                                                        const gchar  *username,
                                                        GError      **error);

void                evd_daemon_set_pid_file            (EvdDaemon   *self,
                                                        const gchar *pid_file);
const gchar *       evd_daemon_get_pid_file            (EvdDaemon *self);

G_END_DECLS

#endif /* __EVD_DAEMON_H__ */
