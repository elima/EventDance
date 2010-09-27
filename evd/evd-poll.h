/*
 * evd-poll.h
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
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#ifndef __EVD_POLL_H__
#define __EVD_POLL_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _EvdPoll EvdPoll;
typedef struct _EvdPollClass EvdPollClass;
typedef struct _EvdPollPrivate EvdPollPrivate;
typedef struct _EvdPollSession  EvdPollSession;

typedef GIOCondition (* EvdPollCallback) (EvdPoll      *self,
                                          GIOCondition  condition,
                                          gpointer      user_data);

struct _EvdPoll
{
  GObject parent;

  EvdPollPrivate *priv;
};

struct _EvdPollClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_POLL           (evd_poll_get_type ())
#define EVD_POLL(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_POLL, EvdPoll))
#define EVD_POLL_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_POLL, EvdPollClass))
#define EVD_IS_POLL(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_POLL))
#define EVD_IS_POLL_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_POLL))
#define EVD_POLL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_POLL, EvdPollClass))


GType              evd_poll_get_type     (void) G_GNUC_CONST;

EvdPoll           *evd_poll_new          (void);

EvdPoll           *evd_poll_get_default  (void);

EvdPollSession    *evd_poll_add          (EvdPoll          *self,
                                          gint              fd,
                                          GIOCondition      condition,
                                          GMainContext     *main_context,
                                          guint             priority,
                                          EvdPollCallback   callback,
                                          gpointer          user_data,
                                          GError          **error);

gboolean           evd_poll_mod          (EvdPoll         *self,
                                          EvdPollSession  *session,
                                          GIOCondition     condition,
                                          GError         **error);

gboolean           evd_poll_del          (EvdPoll         *self,
                                          EvdPollSession  *session,
                                          GError         **error);

void               evd_poll_free_session (EvdPollSession *session);

G_END_DECLS

#endif /* __EVD_POLL_H__ */
