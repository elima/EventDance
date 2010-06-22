/*
 * evd-long-polling.h
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

#ifndef __EVD_LONG_POLLING_H__
#define __EVD_LONG_POLLING_H__

#include <evd-transport.h>

G_BEGIN_DECLS

typedef struct _EvdLongPolling EvdLongPolling;
typedef struct _EvdLongPollingClass EvdLongPollingClass;
typedef struct _EvdLongPollingPrivate EvdLongPollingPrivate;

struct _EvdLongPolling
{
  EvdTransport parent;

  EvdLongPollingPrivate *priv;
};

struct _EvdLongPollingClass
{
  EvdTransportClass parent_class;

  /* virtual methods */

  /* signal prototypes */
};

#define EVD_TYPE_LONG_POLLING           (evd_long_polling_get_type ())
#define EVD_LONG_POLLING(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_LONG_POLLING, EvdLongPolling))
#define EVD_LONG_POLLING_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_LONG_POLLING, EvdLongPollingClass))
#define EVD_IS_LONG_POLLING(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_LONG_POLLING))
#define EVD_IS_LONG_POLLING_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_LONG_POLLING))
#define EVD_LONG_POLLING_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_LONG_POLLING, EvdLongPollingClass))


GType               evd_long_polling_get_type          (void) G_GNUC_CONST;

EvdLongPolling     *evd_long_polling_new               (void);

gboolean            evd_long_polling_new_connection_protected    (EvdService    *service,
                                                                  EvdConnection *conn);

gboolean            evd_long_polling_tls_started_protected       (EvdService     *service,
                                                                  EvdConnection  *conn);

gboolean            evd_long_polling_connection_closed_protected (EvdService    *self,
                                                                  EvdConnection *conn);

G_END_DECLS

#endif /* __EVD_LONG_POLLING_H__ */
