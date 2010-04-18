/*
 * evd-tls-dh-generator.h
 *
 * EventDance - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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
 *
 */

#ifndef __EVD_TLS_DH_GENERATOR_H__
#define __EVD_TLS_DH_GENERATOR_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <gnutls/gnutls.h>

G_BEGIN_DECLS

typedef struct _EvdTlsDhGenerator EvdTlsDhGenerator;
typedef struct _EvdTlsDhGeneratorClass EvdTlsDhGeneratorClass;
typedef struct _EvdTlsDhGeneratorPrivate EvdTlsDhGeneratorPrivate;

struct _EvdTlsDhGenerator
{
  GObject parent;

  EvdTlsDhGeneratorPrivate *priv;
};

struct _EvdTlsDhGeneratorClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_TLS_DH_GENERATOR           (evd_tls_dh_generator_get_type ())
#define EVD_TLS_DH_GENERATOR(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TLS_DH_GENERATOR, EvdTlsDhGenerator))
#define EVD_TLS_DH_GENERATOR_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TLS_DH_GENERATOR, EvdTlsDhGeneratorClass))
#define EVD_IS_TLS_DH_GENERATOR(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TLS_DH_GENERATOR))
#define EVD_IS_TLS_DH_GENERATOR_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TLS_DH_GENERATOR))
#define EVD_TLS_DH_GENERATOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TLS_DH_GENERATOR, EvdTlsDhGeneratorClass))


GType              evd_tls_dh_generator_get_type        (void) G_GNUC_CONST;

EvdTlsDhGenerator *evd_tls_dh_generator_new             (void);

void               evd_tls_dh_generator_generate        (EvdTlsDhGenerator   *self,
                                                         guint                bit_length,
                                                         gboolean             regenerate,
                                                         GAsyncReadyCallback  callback,
                                                         GCancellable        *cancellable,
                                                         gpointer             user_data);

gpointer           evd_tls_dh_generator_generate_finish (EvdTlsDhGenerator  *self,
                                                         GAsyncResult       *result,
                                                         GError            **error);

G_END_DECLS

#endif /* __EVD_TLS_DH_GENERATOR_H__ */
