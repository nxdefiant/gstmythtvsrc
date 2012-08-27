/*
 * GStreamer MythTV Plug-in 
 * Copyright(C) <2006> Rosfran Borges <rosfran.borges@indt.org.br>
 * Copyright(C) <2007> Renato Filho <renato.filho@indt.org.br>  
 * This library is free software; you can
 * redistribute it and/or modify it under the terms of the GNU Library
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or(at your option) any later version.
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library 
 * General Public License for more details. You should have received a copy 
 * of the GNU Library General Public License along with this library; if
 * not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 
 * 330, Boston, MA 02111-1307, USA. 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmythtvsrc.h"
#include "refmem/refmem.h"

#include <string.h>
#include <unistd.h>

#define GST_GMYTHTV_ID_NUM                  1
#define GST_GMYTHTV_CHANNEL_DEFAULT_NUM    (-1)
#define GMYTHTV_VERSION_DEFAULT             30
#define GST_FLOW_ERROR_NO_DATA             (-101)

GST_DEBUG_CATEGORY_STATIC (gst_mythtv_src_debug);
#define GST_CAT_DEFAULT gst_mythtv_src_debug

enum
{
	PROP_0,
	PROP_LOCATION,
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE("src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS_ANY);

static void gst_mythtv_src_finalize(GObject * gobject);

static GstFlowReturn gst_mythtv_src_create(GstPushSrc * psrc, GstBuffer ** outbuf);

static gboolean gst_mythtv_src_start(GstBaseSrc * bsrc);
static gboolean gst_mythtv_src_stop(GstBaseSrc * bsrc);
static gboolean gst_mythtv_src_is_seekable(GstBaseSrc *push_src);
static gboolean gst_mythtv_src_get_size(GstBaseSrc *bsrc, guint64 *size);
static gboolean gst_mythtv_src_do_seek(GstBaseSrc *base, GstSegment *segment);

static GstStateChangeReturn gst_mythtv_src_change_state(GstElement * element, GstStateChange transition);

static void gst_mythtv_src_set_property(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_mythtv_src_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_mythtv_src_uri_handler_init(gpointer g_iface, gpointer iface_data);

static void _urihandler_init(GType type)
{
	static const GInterfaceInfo urihandler_info = {
		gst_mythtv_src_uri_handler_init,
		NULL,
		NULL
	};

	g_type_add_interface_static(type, GST_TYPE_URI_HANDLER, &urihandler_info);
}

GST_BOILERPLATE_FULL(GstMythtvSrc, gst_mythtv_src, GstPushSrc, GST_TYPE_PUSH_SRC, _urihandler_init)
static void gst_mythtv_src_base_init(gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_add_static_pad_template(element_class, &srctemplate);

	gst_element_class_set_details_simple(element_class, "MythTV client source",
			"Source/Network",
			"Control and receive data as a client over the network "
			"via raw socket connections using the MythTV protocol",
			"Rosfran Borges <rosfran.borges@indt.org.br>, "
			"Renato Filho <renato.filho@indt.org.br>");

	element_class->change_state = gst_mythtv_src_change_state;

}

static void gst_mythtv_src_class_init(GstMythtvSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstPushSrcClass *gstpushsrc_class;
	GstBaseSrcClass *gstbasesrc_class;

	gobject_class =(GObjectClass *)klass;
	gstbasesrc_class =(GstBaseSrcClass *)klass;
	gstpushsrc_class =(GstPushSrcClass *)klass;

	gobject_class->set_property = gst_mythtv_src_set_property;
	gobject_class->get_property = gst_mythtv_src_get_property;
	gobject_class->finalize = gst_mythtv_src_finalize;

	g_object_class_install_property(gobject_class, PROP_LOCATION,
		 g_param_spec_string("location", "Location",
			 "The location. In the form:"
			 "\n\t\t\tmyth://user:pass@xxx.xxx.xxx.xxx/channels/7.ts"
			 "\n\t\t\tmyth://user:pass@xxx.xxx.xxx.xxx/recordings/17413_20120823161000.mpg",
			 "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	gstbasesrc_class->start = GST_DEBUG_FUNCPTR(gst_mythtv_src_start);
	gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(gst_mythtv_src_stop);
	gstpushsrc_class->create = GST_DEBUG_FUNCPTR(gst_mythtv_src_create);
	gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR(gst_mythtv_src_get_size);
	gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_mythtv_src_is_seekable);
	gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR(gst_mythtv_src_do_seek);
	
	GST_DEBUG_CATEGORY_INIT(gst_mythtv_src_debug, "mythtvsrc", 0, "mythtv element");
}


static gboolean gst_mythtv_src_get_size(GstBaseSrc *bsrc, guint64 *size) {
	GstMythtvSrc *src = GST_MYTHTV_SRC(bsrc);
	
	src->size = cmyth_proginfo_length(src->prog);
	*size = src->size;
	if (src->rec != NULL && src->size == 0) {
		// live
		*size = GST_CLOCK_TIME_NONE;
	}

	return TRUE;
}


static gboolean gst_mythtv_src_is_seekable(GstBaseSrc *push_src)
{
	return TRUE;
}


static void gst_mythtv_src_init(GstMythtvSrc *this, GstMythtvSrcClass *g_class)
{
	this->control = NULL;
	this->file = NULL;
	this->rec = NULL;
	this->prog = NULL;
	this->uri = NULL;
	gst_base_src_set_format(GST_BASE_SRC(this), GST_FORMAT_BYTES);
}


static void gst_mythtv_src_finalize(GObject * gobject)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC(gobject);

	if(src->uri != NULL) {
		g_free(src->uri);
		src->uri = NULL;
	}
	if(src->rec != NULL) {
		cmyth_proginfo_rec_end(src->prog);
		ref_release(src->rec);
		src->rec = NULL;
	}
	if(src->prog != NULL) {
		ref_release(src->prog);
		src->prog = NULL;
	}
	if (src->file != NULL) {
		ref_release(src->file);
		src->file = NULL;
	}
	if (src->control != NULL) {
		ref_release(src->control);
		src->control = NULL;
	}

	G_OBJECT_CLASS(parent_class)->finalize(gobject);
}


static gboolean gst_mythtv_src_do_seek(GstBaseSrc *base, GstSegment *segment)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC(base);

	GST_DEBUG("Seek %ld %d, Size: %ld\n", segment->start, segment->format == GST_FORMAT_BYTES, cmyth_proginfo_length(src->prog));

	src->pos = cmyth_file_seek(src->file, segment->start, SEEK_SET);
	if (src->pos != segment->start) {
		return FALSE;
	}

	return TRUE;
}


static GstFlowReturn gst_mythtv_src_create(GstPushSrc *psrc, GstBuffer **outbuf)
{
	GstMythtvSrc *src;
	GstFlowReturn ret = GST_FLOW_OK;
	guint8 *buf;
	int num=0, len;
	GstBaseSrc *basesrc;
	GstMessage *msg;

	src = GST_MYTHTV_SRC(psrc);
	basesrc = GST_BASE_SRC_CAST(src);

	buf = g_malloc(16*1024);
	len = cmyth_file_request_block(src->file, 16*1024);
	num = cmyth_file_get_block(src->file,(char *)buf, len);
	if(num < 0) {
		GST_ELEMENT_ERROR(src, RESOURCE, READ,(NULL),("Could not read any bytes(%d, %s)", num, src->uri));
		g_free(buf);
		return GST_FLOW_ERROR;
	}

	GST_DEBUG("read: %d at %lld\n", num, src->pos);

	if(num == 0 && src->rec != NULL) {
		// try new stream
		GST_DEBUG("Trying next stream\n");
		src->prog = cmyth_recorder_get_cur_proginfo(src->rec);
		cmyth_proginfo_rec_start(src->prog);
		if((src->file=cmyth_conn_connect_file(src->prog, src->control, 16*1024, 32*1024)) == NULL) {
			GST_INFO_OBJECT(src, "FileTransfer is NULL");
			g_free(buf);
			return GST_FLOW_ERROR;
		}
		src->pos = 0;
	}

	*outbuf = gst_buffer_new();
	GST_BUFFER_SIZE(*outbuf) = num;
	GST_BUFFER_MALLOCDATA(*outbuf) = buf;
	GST_BUFFER_DATA(*outbuf) = GST_BUFFER_MALLOCDATA(*outbuf);
	GST_BUFFER_OFFSET(*outbuf) = src->pos;
	GST_BUFFER_OFFSET_END(*outbuf) = src->pos + num;
	src->pos += num;

	if (src->pos > src->size) {
		GST_DEBUG("Updating size\n");
		
		gst_segment_set_duration(&basesrc->segment, GST_FORMAT_BYTES, src->pos);
		msg = gst_message_new_duration(GST_OBJECT(src), GST_FORMAT_BYTES, src->pos);
		gst_element_post_message(GST_ELEMENT(src), msg);

		src->size = src->pos;
	}
	
	return ret;
}


void prog_update_callback(cmyth_proginfo_t prog) {
	GST_DEBUG("prog_update_callback called\n");
	return;
}

/*
 * create a socket for connecting to remote server 
 */
static gboolean gst_mythtv_src_start(GstBaseSrc *bsrc)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC(bsrc);
	cmyth_proglist_t episodes;
	int i, count;
	char user[100];
	char pass[100];
	char host[100];
	char cat[100];
	char filename[100];
	char *pathname;
	char **err=NULL;
	gboolean ret = TRUE;
	char *c;

	src->size = 0;

	if(sscanf(src->uri, "myth://%99[^:]:%99[^@]@%99[^/]/%99[^/]%99[^\n]", user, pass, host, cat, filename) != 5) {
		return FALSE;
	}
	GST_DEBUG("Using URI: myth://%s:%s@%s/%s%s\n", user, pass, host, cat, filename);

	if((src->control=cmyth_conn_connect_ctrl(host, 6543, 16*1024, 4096)) == NULL) {
		return FALSE;
	}

	if(strcmp(cat, "recordings") == 0) {
		episodes = cmyth_proglist_get_all_recorded(src->control);
		if(episodes == NULL) {
			ref_release(src->control);
			return FALSE;
		}
		count = cmyth_proglist_get_count(episodes);

		for(i=0; i<count; i++) {
			src->prog = cmyth_proglist_get_item(episodes, i);

			pathname = cmyth_proginfo_pathname(src->prog);
			GST_DEBUG("Search file: %s %s %d\n", filename, pathname, strcmp(filename, pathname));
			if(strcmp(filename, pathname) == 0) {
				break;
			}

		}
		ref_release(episodes);
	} else {
		// fix channel string
		c = strstr(filename, ".ts");
		*c = '\0';
		// search recorder
		for(i=0; i<16; i++) {
			src->rec = cmyth_conn_get_recorder_from_num(src->control, i);
			if(src->rec) {
				GST_DEBUG("Channel: %d Is Recording: %d\n", i, cmyth_recorder_is_recording(src->rec));
				GST_DEBUG("Channel OK: %s %d\n", filename+1, cmyth_recorder_check_channel(src->rec, filename+1) == 0);
				if(cmyth_recorder_is_recording(src->rec) == 0 && cmyth_recorder_check_channel(src->rec, filename+1) == 0) {
					break;
				}
				ref_release(src->rec);
				src->rec = NULL;
			}
		}
		if(src->rec == NULL) {
			GST_DEBUG("No recorder\n");
			return FALSE;
		}

		src->rec = cmyth_spawn_live_tv(src->rec, 16*1024, 4096, prog_update_callback, err);

		cmyth_recorder_pause(src->rec);
		cmyth_recorder_set_channel(src->rec, filename+1);

		GST_DEBUG("Start recording..\n");
		src->prog = cmyth_recorder_get_cur_proginfo(src->rec);
		cmyth_proginfo_rec_start(src->prog);
		pathname = cmyth_proginfo_pathname(src->prog);
		GST_DEBUG("Rec: %s\n", pathname);
	}
	
	if((src->file=cmyth_conn_connect_file(src->prog, src->control, 16*1024, 32*1024)) == NULL) {
		GST_INFO_OBJECT(src, "FileTransfer is NULL");
		ret = FALSE;
	}

	ref_release(pathname);

	return ret;
}


/*
 * close the socket and associated resources used both to recover from
 * errors and go to NULL state 
 */
static gboolean gst_mythtv_src_stop(GstBaseSrc *bsrc)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC(bsrc);

	if(src->rec != NULL) {
		cmyth_proginfo_rec_end(src->prog);
		ref_release(src->rec);
		src->rec = NULL;
	}
	if(src->prog != NULL) {
		ref_release(src->prog);
		src->prog = NULL;
	}
	if (src->file != NULL) {
		ref_release(src->file);
		src->file = NULL;
	}
	return TRUE;
}


static GstStateChangeReturn gst_mythtv_src_change_state(GstElement * element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
	GstMythtvSrc *src = GST_MYTHTV_SRC(element);

	switch(transition) {
		case GST_STATE_CHANGE_NULL_TO_READY:
			break;
		case GST_STATE_CHANGE_READY_TO_PAUSED:
			if(!src->uri) {
				GST_WARNING_OBJECT(src, "Invalid location");
				return ret;
			}
			break;
		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			break;
		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
	if(ret == GST_STATE_CHANGE_FAILURE) {
		return ret;
	}

	switch(transition) {
		case GST_STATE_CHANGE_READY_TO_NULL:
			break;
		case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		case GST_STATE_CHANGE_PAUSED_TO_READY:
			break;
		default:
			break;
	}

	return ret;
}

static void gst_mythtv_src_set_property(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
	GstMythtvSrc *mythtvsrc = GST_MYTHTV_SRC(object);

	GST_OBJECT_LOCK(mythtvsrc);
	switch(prop_id) {
		case PROP_LOCATION:
			if(!g_value_get_string(value)) {
				GST_WARNING("location property cannot be NULL");
				break;
			}

			if(mythtvsrc->uri != NULL) {
				g_free(mythtvsrc->uri);
				mythtvsrc->uri = NULL;
			}
			mythtvsrc->uri = g_value_dup_string(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}

	GST_OBJECT_UNLOCK(mythtvsrc);
}

static void gst_mythtv_src_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	GstMythtvSrc *mythtvsrc = GST_MYTHTV_SRC(object);

	GST_OBJECT_LOCK(mythtvsrc);
	switch(prop_id) {
		case PROP_LOCATION:
			g_value_set_string(value, mythtvsrc->uri);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
	GST_OBJECT_UNLOCK(mythtvsrc);
}

static gboolean plugin_init(GstPlugin * plugin)
{
	return gst_element_register(plugin, "mythtvsrc", GST_RANK_NONE,
			GST_TYPE_MYTHTV_SRC);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		"mythtv",
		"lib MythTV src",
		plugin_init, VERSION, "GPL", "MythTV", "http://");


/*** GSTURIHANDLER INTERFACE *************************************************/
static guint gst_mythtv_src_uri_get_type(void)
{
	return GST_URI_SRC;
}

static gchar **gst_mythtv_src_uri_get_protocols(void)
{
	static const gchar *protocols[] = { "myth", "myths", NULL };

	return(gchar **) protocols;
}

static const gchar *gst_mythtv_src_uri_get_uri(GstURIHandler * handler)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC(handler);

	return src->uri;
}

static gboolean gst_mythtv_src_uri_set_uri(GstURIHandler * handler, const gchar * uri)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC(handler);

	gchar *protocol;

	protocol = gst_uri_get_protocol(uri);
	if((strcmp(protocol, "myth") != 0) &&(strcmp(protocol, "myths") != 0)) {
		g_free(protocol);
		return FALSE;
	}
	g_free(protocol);
	g_object_set(src, "location", uri, NULL);

	return TRUE;
}

static void gst_mythtv_src_uri_handler_init(gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface =(GstURIHandlerInterface *) g_iface;

	iface->get_type = gst_mythtv_src_uri_get_type;
	iface->get_protocols = gst_mythtv_src_uri_get_protocols;
	iface->get_uri = gst_mythtv_src_uri_get_uri;
	iface->set_uri = gst_mythtv_src_uri_set_uri;
}
