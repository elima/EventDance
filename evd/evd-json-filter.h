/*
 * evd-json-filter.h
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

#ifndef __EVD_JSON_FILTER_H__
#define __EVD_JSON_FILTER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _EvdJsonFilter EvdJsonFilter;
typedef struct _EvdJsonFilterClass EvdJsonFilterClass;
typedef struct _EvdJsonFilterPrivate EvdJsonFilterPrivate;

typedef void (* EvdJsonFilterOnPacketHandler) (EvdJsonFilter *self,
                                               const gchar   *buffer,
                                               gsize          size,
                                               gpointer       user_data);

struct _EvdJsonFilter
{
  GObject parent;

  /* private structure */
  EvdJsonFilterPrivate *priv;
};

struct _EvdJsonFilterClass
{
  GObjectClass parent_class;

  /* virtual methods */

  /* signal prototypes */
};

typedef enum
{
  EVD_JSON_FILTER_ERROR_INVALID,
  EVD_JSON_FILTER_ERROR_LAST
} EvdJsonFilterErrors;

#define EVD_TYPE_JSON_FILTER           (evd_json_filter_get_type ())
#define EVD_JSON_FILTER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_JSON_FILTER, EvdJsonFilter))
#define EVD_JSON_FILTER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_JSON_FILTER, EvdJsonFilterClass))
#define EVD_IS_JSON_FILTER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_JSON_FILTER))
#define EVD_IS_JSON_FILTER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_JSON_FILTER))
#define EVD_JSON_FILTER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_JSON_FILTER, EvdJsonFilterClass))


GType evd_json_filter_get_type (void) G_GNUC_CONST;

EvdJsonFilter    *evd_json_filter_new                (void);

void              evd_json_filter_reset              (EvdJsonFilter *self);
gboolean          evd_json_filter_feed_len           (EvdJsonFilter  *self,
                                                      const gchar    *buffer,
                                                      gsize           size,
                                                      GError        **error);
gboolean          evd_json_filter_feed               (EvdJsonFilter  *self,
                                                      const gchar    *buffer,
                                                      GError        **error);

void              evd_json_filter_set_packet_handler (EvdJsonFilter                *self,
                                                      EvdJsonFilterOnPacketHandler  handler,
                                                      gpointer                      user_data);
void              evd_json_filter_set_on_packet      (EvdJsonFilter *self,
                                                      GClosure      *closure);


G_END_DECLS

#endif /* __EVD_JSON_FILTER_H__ */
