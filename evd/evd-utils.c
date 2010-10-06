/*
 * evd-utils.c
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

#include <time.h>
#include <uuid/uuid.h>

#include "evd-utils.h"

guint
evd_timeout_add (GMainContext *context,
                 guint         timeout,
                 gint          priority,
                 GSourceFunc   callback,
                 gpointer      user_data)
{
  guint src_id;
  GSource *src;

  if (context == NULL)
    context = g_main_context_get_thread_default ();

  if (timeout == 0)
    src = g_idle_source_new ();
  else
    src = g_timeout_source_new (timeout);

  g_source_set_priority (src, priority);

  g_source_set_callback (src,
                         callback,
                         user_data,
                         NULL);
  src_id = g_source_attach (src, context);
  g_source_unref (src);

  return src_id;
}

void
evd_nanosleep (gulong nanoseconds)
{
  struct timespec delay;

  delay.tv_sec = 0;
  delay.tv_nsec = nanoseconds;

  nanosleep (&delay, NULL);
}

gchar *
evd_uuid_new (void)
{
  uuid_t uuid;
  gchar *uuid_st = NULL;

  uuid_st = g_new (gchar, 37);

  uuid_generate (uuid);
  uuid_unparse (uuid, uuid_st);

  return uuid_st;
}
