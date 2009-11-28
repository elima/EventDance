/*
 * evd-stream-protected.h
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

#ifndef __EVD_STREAM_PROTECTED_H__
#define __EVD_STREAM_PROTECTED_H__

#include "evd-stream.h"

G_BEGIN_DECLS

/**
 * SECTION:evd-stream-protected
 * @short_description: Protected methods of #EvdStream.
 *
 */

void          evd_stream_report_read    (EvdStream *self,
                                         gsize      size);
void          evd_stream_report_write   (EvdStream *self,
                                         gsize      size);

G_END_DECLS

#endif /* __EVD_STREAM_PROTECTED_H__ */
