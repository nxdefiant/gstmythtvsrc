/*
 * GStreamer MythTV Plug-in 
 * Copyright (C) <2006> Rosfran Borges <rosfran.borges@indt.org.br>
 * Copyright (C) <2007> Renato Filho <renato.filho@indt.org.br>  
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

/**
 * SECTION:element-mythtvsrc
 * @see_also: nuvdemux
 *
 * MythTVSrc allows to access a remote MythTV backend streaming Video/Audio server,
 * and to render audio and video content through a TCP/IP connection to a specific
 * port on this server, and based on a known MythTV protocol that is based on 
 * some message passing, such as REQUEST_BLOCK on a specified number of bytes, to get
 * some chunk of remote file data.
 * You should pass the information aboute the remote MythTV backend server 
 * through the #GstMythtvSrc:location property.
 * 
 * <refsect2>
 * <title>Examples</title>
 * <para>
 * If you want to get the LiveTV content (set channel, TV tuner, RemoteEncoder, 
 * Recorder), use the following URI:
 * <programlisting>
 *  myth://xxx.xxx.xxx.xxx:6543/livetv?channel=BBC
 * </programlisting>
 *
 * This URI will configure the Recorder instance (used to change the channel,
 * start the TV multimedia content transmition, etc.), using
 * the IP address (xxx.xxx.xxx.xxx) and port number (6543) of the MythTV backend 
 * server, and setting the channel name to "BBC". 
 * 
 * To get a already recorded the MythTV NUV file, put the following URI:
 * <programlisting>
 *  myth://xxx.xxx.xxx.xxx:6543/filename.nuv
 * </programlisting>
 * 
 * Another possible way to use the LiveTV content, and just in the case you want to 
 * use the mysql database, put the location URI in the following format:
 * <programlisting>
 *  myth://mythtv:mythtv@xxx.xxx.xxx.xxx:6543/?mythconverg&channel=9
 * </programlisting>
 * 
 * Where the first field is the protocol (myth), the second and third are user 
 * name (mythtv) and password (mythtv), then backend host name and port number, 
 * and the last field is the database name (mythconverg).
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmythtvsrc.h"
#include "refmem/refmem.h"

#include <string.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC (mythtvsrc_debug);
#define GST_GMYTHTV_ID_NUM                  1
#define GST_GMYTHTV_CHANNEL_DEFAULT_NUM     (-1)
#define GMYTHTV_VERSION_DEFAULT             30
#define GST_FLOW_ERROR_NO_DATA              (-101)

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS_ANY);
enum
{
	PROP_0,
	PROP_LOCATION,
	PROP_GMYTHTV_VERSION,
	PROP_GMYTHTV_LIVE,
	PROP_GMYTHTV_LIVEID,
	PROP_GMYTHTV_LIVE_CHAINID,
	PROP_GMYTHTV_ENABLE_TIMING_POSITION,
	PROP_GMYTHTV_CHANNEL_NUM
};

static void gst_mythtv_src_finalize (GObject * gobject);

static GstFlowReturn gst_mythtv_src_create (GstPushSrc * psrc,
		GstBuffer ** outbuf);

static gboolean gst_mythtv_src_start (GstBaseSrc * bsrc);
static gboolean gst_mythtv_src_stop (GstBaseSrc * bsrc);

static GstStateChangeReturn
gst_mythtv_src_change_state (GstElement * element, GstStateChange transition);

static void gst_mythtv_src_set_property (GObject * object,
		guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_mythtv_src_get_property (GObject * object,
		guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_mythtv_src_uri_handler_init (gpointer g_iface,
		gpointer iface_data);

static void _urihandler_init (GType type)
{
	static const GInterfaceInfo urihandler_info = {
		gst_mythtv_src_uri_handler_init,
		NULL,
		NULL
	};

	g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);

	GST_DEBUG_CATEGORY_INIT (mythtvsrc_debug, "mythtvsrc", 0, "MythTV src");
}

GST_BOILERPLATE_FULL(GstMythtvSrc, gst_mythtv_src, GstPushSrc,
		GST_TYPE_PUSH_SRC, _urihandler_init)
static void gst_mythtv_src_base_init (gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

	gst_element_class_add_static_pad_template (element_class, &srctemplate);

	gst_element_class_set_details_simple (element_class, "MythTV client source",
			"Source/Network",
			"Control and receive data as a client over the network "
			"via raw socket connections using the MythTV protocol",
			"Rosfran Borges <rosfran.borges@indt.org.br>, "
			"Renato Filho <renato.filho@indt.org.br>");

	element_class->change_state = gst_mythtv_src_change_state;

}

	static void
gst_mythtv_src_class_init (GstMythtvSrcClass * klass)
{
	GObjectClass *gobject_class;
	GstPushSrcClass *gstpushsrc_class;
	GstBaseSrcClass *gstbasesrc_class;

	gobject_class = (GObjectClass *) klass;
	gstbasesrc_class = (GstBaseSrcClass *) klass;
	gstpushsrc_class = (GstPushSrcClass *) klass;

	gobject_class->set_property = gst_mythtv_src_set_property;
	gobject_class->get_property = gst_mythtv_src_get_property;
	gobject_class->finalize = gst_mythtv_src_finalize;

	g_object_class_install_property
		(gobject_class, PROP_LOCATION,
		 g_param_spec_string ("location", "Location",
			 "The location. In the form:"
			 "\n\t\t\tmyth://a.com/file.nuv"
			 "\n\t\t\tmyth://a.com:23223/file.nuv"
			 "\n\t\t\tmyth://a.com/?channel=123"
			 "\n\t\t\tmyth://a.com/?channel=Channel%203"
			 "\n\t\t\ta.com/file.nuv - default scheme 'myth'",
			 "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(gobject_class, PROP_GMYTHTV_VERSION,
		 g_param_spec_int ("mythtv-version", "mythtv-version",
			 "Change MythTV version", 26, 30, 26,
			 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(gobject_class, PROP_GMYTHTV_LIVEID,
		 g_param_spec_int ("mythtv-live-id", "mythtv-live-id",
			 "Change MythTV version",
			 0, 200, GST_GMYTHTV_ID_NUM,
			 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(gobject_class, PROP_GMYTHTV_LIVE_CHAINID,
		 g_param_spec_string ("mythtv-live-chainid", "mythtv-live-chainid",
			 "Sets the MythTV chain ID (from TV Chain)", "",
			 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(gobject_class, PROP_GMYTHTV_LIVE,
		 g_param_spec_boolean ("mythtv-live", "mythtv-live",
			 "Enable MythTV Live TV content streaming", FALSE,
			 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(gobject_class, PROP_GMYTHTV_ENABLE_TIMING_POSITION,
		 g_param_spec_boolean ("mythtv-enable-timing-position",
			 "mythtv-enable-timing-position",
			 "Enable MythTV Live TV content size continuous updating",
			 FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(gobject_class, PROP_GMYTHTV_CHANNEL_NUM,
		 g_param_spec_string ("mythtv-channel", "mythtv-channel",
			 "Change MythTV channel number", "",
			 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_mythtv_src_start);
	gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_mythtv_src_stop);
	gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_mythtv_src_create);

	GST_DEBUG_CATEGORY_INIT (mythtvsrc_debug, "mythtvsrc", 0,
			"MythTV Client Source");
}

static void gst_mythtv_src_init(GstMythtvSrc * this, GstMythtvSrcClass * g_class)
{
	this->file = NULL;
	this->mythtv_version = GMYTHTV_VERSION_DEFAULT;
	this->state = GST_MYTHTV_SRC_FILE_TRANSFER;
	this->bytes_read = 0;
	this->prev_content_size = 0;
	this->read_offset = 0;
	this->content_size_last = 0;
	this->enable_timing_position = FALSE;
	this->update_prog_chain = FALSE;
	this->user_agent = g_strdup ("mythtvsrc");
	this->update_prog_chain = FALSE;
	this->channel_name = NULL;
	this->eos = FALSE;
	this->wait_to_transfer = 0;
	gst_base_src_set_format (GST_BASE_SRC (this), GST_FORMAT_BYTES);
}

static void gst_mythtv_src_finalize(GObject * gobject)
{
	GstMythtvSrc *this = GST_MYTHTV_SRC (gobject);

	if (this->uri_name) {
		g_free (this->uri_name);
		this->uri_name = NULL;
	}

	if (this->user_agent) {
		g_free (this->user_agent);
		this->user_agent = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (gobject);
}


static GstFlowReturn gst_mythtv_src_create(GstPushSrc *psrc, GstBuffer **outbuf)
{
	GstMythtvSrc *src;
	GstFlowReturn ret = GST_FLOW_OK;
	guint8 *buf;
	int num=0, len;

	printf("Create buffer..\n");
	src = GST_MYTHTV_SRC(psrc);

	buf = g_malloc(16*1024);
	len = cmyth_file_request_block(src->file, 16*1024);
	num = cmyth_file_get_block(src->file, (char *)buf, len);
	if (num < 0) {
		goto read_error;
	}

	printf("read: %d\n", num);

	*outbuf = gst_buffer_new();
	GST_BUFFER_SIZE(*outbuf) = num;
	GST_BUFFER_MALLOCDATA(*outbuf) = buf;
	GST_BUFFER_DATA(*outbuf) = GST_BUFFER_MALLOCDATA (*outbuf);
	GST_BUFFER_OFFSET(*outbuf) = src->read_offset;
	GST_BUFFER_OFFSET_END(*outbuf) = src->read_offset + GST_BUFFER_SIZE (*outbuf);

	src->read_offset += GST_BUFFER_SIZE(*outbuf);
	src->bytes_read += GST_BUFFER_SIZE(*outbuf);
	
	GST_LOG_OBJECT (src, "Create finished: %d", ret);
	return ret;

read_error:
	GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), ("Could not read any bytes (%d, %s)", num, src->uri_name));
	g_free(buf);
	return GST_FLOW_ERROR;
}


void prog_update_callback(cmyth_proginfo_t prog) {
	printf("prog_update_callback called\n");
	return;
}

/*
 * create a socket for connecting to remote server 
 */
static gboolean gst_mythtv_src_start(GstBaseSrc *bsrc)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC(bsrc);
	cmyth_conn_t control;
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

	if (sscanf(src->uri_name, "myth://%99[^:]:%99[^@]@%99[^/]/%99[^/]%99[^\n]", user, pass, host, cat, filename) != 5) {
		return FALSE;
	}
	printf("Using URI: myth://%s:%s@%s/%s%s\n", user, pass, host, cat, filename);

	if ((control=cmyth_conn_connect_ctrl(host, 6543, 16*1024, 4096)) == NULL) {
		return FALSE;
	}

	if (strcmp(cat, "recordings") == 0) {
		episodes = cmyth_proglist_get_all_recorded(control);
		if (episodes == NULL) {
			ref_release(control);
			return FALSE;
		}
		count = cmyth_proglist_get_count(episodes);

		for (i=0; i<count; i++) {
			src->prog = cmyth_proglist_get_item(episodes, i);

			pathname = cmyth_proginfo_pathname(src->prog);
			printf("Search file: %s %s %d\n", filename, pathname, strcmp(filename, pathname));
			if (strcmp(filename, pathname) == 0) {
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
			src->rec = cmyth_conn_get_recorder_from_num(control, i);
			if (src->rec) {
				printf("Channel: %d Is Recording: %d\n", i, cmyth_recorder_is_recording(src->rec));
				printf("Channel OK: %s %d\n", filename+1, cmyth_recorder_check_channel(src->rec, filename+1) == 0);
				if (cmyth_recorder_is_recording(src->rec) == 0 && cmyth_recorder_check_channel(src->rec, filename+1) == 0) {
					break;
				}
				ref_release(src->rec);
				src->rec = NULL;
			}
		}
		if (src->rec == NULL) {
			printf("No recorder\n");
			ref_release(control);
			return FALSE;
		}

		src->rec = cmyth_spawn_live_tv(src->rec, 16*1024, 4096, prog_update_callback, err);

		cmyth_recorder_pause(src->rec);
		cmyth_recorder_set_channel(src->rec, filename+1);

		printf("Start recording..\n");
		src->prog = cmyth_recorder_get_cur_proginfo(src->rec);
		cmyth_proginfo_rec_start(src->prog);
		pathname = cmyth_proginfo_pathname(src->prog);
		printf("Rec: %s\n", pathname);
	}
	
	if ((src->file=cmyth_conn_connect_file(src->prog, control, 16*1024, 32*1024)) == NULL) {
		ret = FALSE;
	}

	if (src->file == NULL) {
		GST_INFO_OBJECT (src, "FileTransfer is NULL");
		ret = FALSE;
	}
	ref_release(pathname);
	ref_release(control);

	return ret;
}

/*
 * close the socket and associated resources used both to recover from
 * errors and go to NULL state 
 */
static gboolean gst_mythtv_src_stop(GstBaseSrc *bsrc)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC(bsrc);

	if (src->rec != NULL) {
		cmyth_proginfo_rec_end(src->prog);
		ref_release(src->rec);
		src->rec = NULL;
	}
	ref_release(src->prog);
	ref_release(src->file);
	return TRUE;
}

static GstStateChangeReturn gst_mythtv_src_change_state(GstElement * element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
	GstMythtvSrc *src = GST_MYTHTV_SRC (element);

	switch (transition) {
		case GST_STATE_CHANGE_NULL_TO_READY:
			break;
		case GST_STATE_CHANGE_READY_TO_PAUSED:
			if (!src->uri_name) {
				GST_WARNING_OBJECT (src, "Invalid location");
				return ret;
			}
			break;
		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			break;
		default:
			break;
	}

	ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		return ret;
	}

	switch (transition) {
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

static void gst_mythtv_src_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
	GstMythtvSrc *mythtvsrc = GST_MYTHTV_SRC (object);

	GST_OBJECT_LOCK (mythtvsrc);
	switch (prop_id) {
		case PROP_LOCATION:
			if (!g_value_get_string (value)) {
				GST_WARNING ("location property cannot be NULL");
				break;
			}

			if (mythtvsrc->uri_name != NULL) {
				g_free (mythtvsrc->uri_name);
				mythtvsrc->uri_name = NULL;
			}
			mythtvsrc->uri_name = g_value_dup_string (value);
			break;
		case PROP_GMYTHTV_VERSION:
			mythtvsrc->mythtv_version = g_value_get_int (value);
			break;
		case PROP_GMYTHTV_LIVEID:
			mythtvsrc->live_tv_id = g_value_get_int (value);
			break;
		case PROP_GMYTHTV_ENABLE_TIMING_POSITION:
			mythtvsrc->enable_timing_position = g_value_get_boolean (value);
			break;
		case PROP_GMYTHTV_LIVE_CHAINID:
			if (!g_value_get_string (value)) {
				GST_WARNING_OBJECT (object,
						"MythTV Live chainid property cannot be NULL");
				break;
			}

			if (mythtvsrc->live_chain_id != NULL) {
				g_free (mythtvsrc->live_chain_id);
				mythtvsrc->live_chain_id = NULL;
			}
			mythtvsrc->live_chain_id = g_value_dup_string (value);
			break;
		case PROP_GMYTHTV_CHANNEL_NUM:
			mythtvsrc->channel_name = g_value_dup_string (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}

	GST_OBJECT_UNLOCK (mythtvsrc);
}

static void gst_mythtv_src_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	GstMythtvSrc *mythtvsrc = GST_MYTHTV_SRC (object);

	GST_OBJECT_LOCK (mythtvsrc);
	switch (prop_id) {
		case PROP_LOCATION:
			g_value_set_string (value, mythtvsrc->uri_name);
			break;
		case PROP_GMYTHTV_VERSION:
			g_value_set_int (value, mythtvsrc->mythtv_version);
			break;
		case PROP_GMYTHTV_LIVEID:
			g_value_set_int (value, mythtvsrc->live_tv_id);
			break;
		case PROP_GMYTHTV_ENABLE_TIMING_POSITION:
			g_value_set_boolean (value, mythtvsrc->enable_timing_position);
			break;
		case PROP_GMYTHTV_LIVE_CHAINID:
			g_value_set_string (value, mythtvsrc->live_chain_id);
			break;
		case PROP_GMYTHTV_CHANNEL_NUM:
			g_value_set_string (value, mythtvsrc->channel_name);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
	GST_OBJECT_UNLOCK (mythtvsrc);
}

static gboolean plugin_init(GstPlugin * plugin)
{
	return gst_element_register (plugin, "mythtvsrc", GST_RANK_NONE,
			GST_TYPE_MYTHTV_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		"mythtv",
		"lib MythTV src",
		plugin_init, VERSION, "BSD", "MythTV", "http://");


/*** GSTURIHANDLER INTERFACE *************************************************/
static guint gst_mythtv_src_uri_get_type(void)
{
	return GST_URI_SRC;
}

static gchar **gst_mythtv_src_uri_get_protocols(void)
{
	static const gchar *protocols[] = { "myth", "myths", NULL };

	return (gchar **) protocols;
}

static const gchar *gst_mythtv_src_uri_get_uri(GstURIHandler * handler)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC (handler);

	return src->uri_name;
}

static gboolean gst_mythtv_src_uri_set_uri(GstURIHandler * handler, const gchar * uri)
{
	GstMythtvSrc *src = GST_MYTHTV_SRC(handler);

	gchar *protocol;

	protocol = gst_uri_get_protocol(uri);
	if ((strcmp(protocol, "myth") != 0)
			&& (strcmp(protocol, "myths") != 0)) {
		g_free(protocol);
		return FALSE;
	}
	g_free(protocol);
	g_object_set(src, "location", uri, NULL);

	return TRUE;
}

static void gst_mythtv_src_uri_handler_init(gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = gst_mythtv_src_uri_get_type;
	iface->get_protocols = gst_mythtv_src_uri_get_protocols;
	iface->get_uri = gst_mythtv_src_uri_get_uri;
	iface->set_uri = gst_mythtv_src_uri_set_uri;
}
