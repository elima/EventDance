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

#ifndef __EVD_UTILS_H__
#define __EVD_UTILS_H__

#include <glib.h>

typedef enum
{
  EVD_VALIDATE_ACCEPT  = 0,
  EVD_VALIDATE_REJECT  = 1,
  EVD_VALIDATE_PENDING = 2
} EvdValidateEnum;

guint   evd_timeout_add (GMainContext *context,
                         guint         timeout,
                         gint          priority,
                         GSourceFunc   callback,
                         gpointer      user_data);

void   evd_nanosleep    (gulong nanoseconds);

gchar *evd_uuid_new     (void);

#endif /* __EVD_UTILS_H__ */
