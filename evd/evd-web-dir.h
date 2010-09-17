/*
 * evd-web-dir.h
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

#ifndef __EVD_WEB_DIR_H__
#define __EVD_WEB_DIR_H__

#include <evd-web-service.h>

G_BEGIN_DECLS

typedef struct _EvdWebDir EvdWebDir;
typedef struct _EvdWebDirClass EvdWebDirClass;
typedef struct _EvdWebDirPrivate EvdWebDirPrivate;

struct _EvdWebDir
{
  EvdWebService parent;

  EvdWebDirPrivate *priv;
};

struct _EvdWebDirClass
{
  EvdWebServiceClass parent_class;
};

#define EVD_TYPE_WEB_DIR           (evd_web_dir_get_type ())
#define EVD_WEB_DIR(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_WEB_DIR, EvdWebDir))
#define EVD_WEB_DIR_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_WEB_DIR, EvdWebDirClass))
#define EVD_IS_WEB_DIR(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_WEB_DIR))
#define EVD_IS_WEB_DIR_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_WEB_DIR))
#define EVD_WEB_DIR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_WEB_DIR, EvdWebDirClass))


GType               evd_web_dir_get_type          (void) G_GNUC_CONST;

EvdWebDir          *evd_web_dir_new               (const gchar *root);

G_END_DECLS

#endif /* __EVD_WEB_DIR_H__ */
