/*
 * GStreamer
 * Copyright (C) <2006> INdT - Rosfran Borges <rosfran.borges@indt.org.br>
 * Copyright (C) <2007> INdT - Rentao Filho <renato.filho@indt.org.br>
 *
 * This library is free software; you can
 * redistribute it and/or modify it under the terms of the GNU Library
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library
 * General Public License for more details. You should have received a copy
 * of the GNU Library General Public License along with this library; if
 * not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite
 * 330, Boston, MA 02111-1307, USA.
 */

#ifndef __GST_MYTHTV_SRC_H__
#define __GST_MYTHTV_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstpushsrc.h>
#include <stdio.h>
#include "cmyth/cmyth.h"

G_BEGIN_DECLS
#define GST_TYPE_MYTHTV_SRC \
	(gst_mythtv_src_get_type())
#define GST_MYTHTV_SRC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MYTHTV_SRC,GstMythtvSrc))
#define GST_MYTHTV_SRC_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MYTHTV_SRC,GstMythtvSrcClass))
#define GST_IS_MYTHTV_SRC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MYTHTV_SRC))
#define GST_IS_MYTHTV_SRC_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MYTHTV_SRC))
typedef struct _GstMythtvSrc GstMythtvSrc;
typedef struct _GstMythtvSrcClass GstMythtvSrcClass;

struct _GstMythtvSrc {
	GstPushSrc      element;

	cmyth_conn_t control;
	cmyth_file_t      file;
	cmyth_recorder_t rec;
	cmyth_proginfo_t prog;
	gchar          *uri;
	long long pos;
	guint64 size;
};

struct _GstMythtvSrcClass {
	GstPushSrcClass parent_class;
};

GType           gst_mythtv_src_get_type(void);

G_END_DECLS
#endif /* __GST_MYTHTV_SRC_H__ */
