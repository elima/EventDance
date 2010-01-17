/*
 * evd-utils.c
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

#include <time.h>

#include "evd-utils.h"

guint
evd_timeout_add (GMainContext *context,
                 guint         timeout,
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
