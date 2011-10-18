/*
 * evd-web-selector.h
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

#ifndef __EVD_WEB_SELECTOR_H__
#define __EVD_WEB_SELECTOR_H__

#include <evd-web-service.h>

G_BEGIN_DECLS

typedef struct _EvdWebSelector EvdWebSelector;
typedef struct _EvdWebSelectorClass EvdWebSelectorClass;
typedef struct _EvdWebSelectorPrivate EvdWebSelectorPrivate;

struct _EvdWebSelector
{
  EvdWebService parent;

  EvdWebSelectorPrivate *priv;
};

struct _EvdWebSelectorClass
{
  EvdWebServiceClass parent_class;

  /* padding for future expansion */
  void (* _padding_0_) (void);
  void (* _padding_1_) (void);
  void (* _padding_2_) (void);
  void (* _padding_3_) (void);
  void (* _padding_4_) (void);
  void (* _padding_5_) (void);
  void (* _padding_6_) (void);
  void (* _padding_7_) (void);
};

#define EVD_TYPE_WEB_SELECTOR           (evd_web_selector_get_type ())
#define EVD_WEB_SELECTOR(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_WEB_SELECTOR, EvdWebSelector))
#define EVD_WEB_SELECTOR_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_WEB_SELECTOR, EvdWebSelectorClass))
#define EVD_IS_WEB_SELECTOR(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_WEB_SELECTOR))
#define EVD_IS_WEB_SELECTOR_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_WEB_SELECTOR))
#define EVD_WEB_SELECTOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_WEB_SELECTOR, EvdWebSelectorClass))


GType               evd_web_selector_get_type            (void) G_GNUC_CONST;

EvdWebSelector     *evd_web_selector_new                 (void);

gboolean            evd_web_selector_add_service         (EvdWebSelector  *self,
                                                          const gchar     *domain_pattern,
                                                          const gchar     *path_pattern,
                                                          EvdService      *service,
                                                          GError         **error);
void                evd_web_selector_remove_service      (EvdWebSelector  *self,
                                                          const gchar     *domain_pattern,
                                                          const gchar     *path_pattern,
                                                          EvdService      *service);

void                evd_web_selector_set_default_service (EvdWebSelector *self,
                                                          EvdService     *service);

G_END_DECLS

#endif /* __EVD_WEB_SELECTOR_H__ */
