/*
This implementation is largely based on testegl.c from gst-omx
with modifications: Copyright (c) The Processing Foundation 2016
Developed by Gottfried Haider

Copyright (c) 2012, Broadcom Europe Ltd
Copyright (c) 2012, OtherCrashOverride
Copyright (C) 2013, Fluendo S.A.
   @author: Josep Torra <josep@fluendo.com>
Copyright (C) 2013, Video Experts Group LLC.
   @author: Ilya Smelykh <ilya@videoexpertsgroup.com>
Copyright (C) 2014 Julien Isorce <julien.isorce@collabora.co.uk>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define GST_USE_UNSTABLE_API
#include <gst/gst.h>
#include <gst/gl/gl.h>
#ifdef __APPLE__
#elif GLES2
#include <gst/gl/egl/gstgldisplay_egl.h>
#else
#include <GL/glx.h>
#include <gst/gl/x11/gstgldisplay_x11.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "impl.h"
#include "iface.h"

#define likely(x)   __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

// XXX: do we still need GST_PLAY_FLAG_SOFT_VOLUME on RPi?
typedef enum
{
  GST_PLAY_FLAG_VIDEO = (1 << 0),
  GST_PLAY_FLAG_AUDIO = (1 << 1),
  GST_PLAY_FLAG_TEXT = (1 << 2),
  GST_PLAY_FLAG_VIS = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD = (1 << 7),
  GST_PLAY_FLAG_BUFFERING = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE = (1 << 9),
  GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10)
} GstPlayFlags;

#ifdef __APPLE__
static GstGLDisplay *gst_display;
#elif GLES2
static GstGLDisplayEGL *gst_display;
#else
static GstGLDisplayX11 *gst_display;
#endif

static GThread *thread;
static GMainLoop *mainloop;
#ifdef __APPLE__
static guintptr context;
#elif GLES2
static EGLDisplay display;
static EGLSurface surface;
static EGLContext context;
#else
static Display* display;
static GLXContext context;
#endif

static void
handle_buffer (GLVIDEO_STATE_T * state, GstBuffer * buffer)
{
  g_mutex_lock (&state->buffer_lock);
  if (unlikely (state->next_buffer != NULL)) {
    gst_buffer_unref (state->next_buffer);
    state->next_buffer = NULL;
  }
  state->next_tex = 0;

  GstMemory *mem = gst_buffer_peek_memory (buffer, 0);

  if (unlikely (!gst_is_gl_memory (mem))) {
    g_printerr ("GLVideo: Not using GPU memory, unsupported\n");
    g_mutex_unlock (&state->buffer_lock);
    return;
  }

  state->next_buffer = gst_buffer_ref (buffer);
  state->next_tex = ((GstGLMemory *) mem)->tex_id;
  g_mutex_unlock (&state->buffer_lock);
}

static void
preroll_cb (GstElement * fakesink, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *) user_data;
  handle_buffer (state, buffer);
}

static void
buffers_cb (GstElement * fakesink, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *) user_data;
  handle_buffer (state, buffer);
}

static GstPadProbeReturn
events_cb (GstPad * pad, GstPadProbeInfo * probe_info, gpointer user_data)
{
  GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *) user_data;
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (probe_info);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      if (state->caps) {
        gst_caps_unref (state->caps);
        state->caps = NULL;
      }
      gst_event_parse_caps (event, &state->caps);
      if (state->caps) {
        // XXX: remove
        gchar * temp = gst_caps_to_string (state->caps);
        fprintf (stdout, "Final caps: %s\n", temp);
        fflush (stdout);
        g_free (temp);
        gst_caps_ref (state->caps);
      }
      break;
    }
    // this is handled in eos_cb
    //case GST_EVENT_EOS:
    //  break;
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
query_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *) user_data;
  GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
#if GST_VERSION_MAJOR <= 1 && GST_VERSION_MINOR <= 10
      if (gst_gl_handle_context_query (state->pipeline, query,
              (GstGLDisplay **) & gst_display,
              (GstGLContext **) & state->gl_context))
        return GST_PAD_PROBE_HANDLED;
#else
      if (gst_gl_handle_context_query (state->pipeline, query,
              (GstGLDisplay *) gst_display, NULL,
              (GstGLContext *) state->gl_context))
        return GST_PAD_PROBE_HANDLED;
#endif
      break;
    }
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, GstPipeline * data)
{
  return GST_BUS_PASS;
}

static void
error_cb (GstBus * bus, GstMessage * msg, GLVIDEO_STATE_T * state)
{
  GError *err;
  gchar *debug_info;

  gst_message_parse_error (msg, &err, &debug_info);
  g_printerr ("GLVideo: %s: %s\n",
    GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
  g_clear_error (&err);
  g_free (debug_info);
}

static void
buffering_cb (GstBus * bus, GstMessage * msg, GLVIDEO_STATE_T * state)
{
  gint percent;

  gst_message_parse_buffering (msg, &percent);
  if (percent < 100) {
    gst_element_set_state (state->pipeline, GST_STATE_PAUSED);
    state->buffering = true;
  } else {
    gst_element_set_state (state->pipeline, GST_STATE_PLAYING);
    state->buffering = false;
  }
}

static void
eos_cb (GstBus * bus, GstMessage * msg, GLVIDEO_STATE_T * state)
{
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (state->pipeline)) {
    if (state->looping) {
      GstEvent *event;
      event = gst_event_new_seek (state->rate,
        GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
      if (!gst_element_send_event (state->vsink, event)) {
        g_printerr ("GLVideo: Error rewinding video\n");
      }
    } else {
      gst_element_set_state (state->pipeline, GST_STATE_PAUSED);
    }
  }
}

static gboolean
init_pipeline_player (GLVIDEO_STATE_T * state, const gchar * pipeline)
{
  const char pipeline_vsink[] = "glupload name=glup ! glcolorconvert ! capsfilter name=filter ! fakesink name=vsink";
  char *pipeline_final = calloc (strlen (pipeline) + 3 + strlen (pipeline_vsink) + 1, sizeof (char));

  char *has_empty_videosink = strstr (pipeline, "video-sink=\"\"");
  if (has_empty_videosink) {
    // fill in our sink as the pipeline's (playbin's) video-sink property
    strncat (pipeline_final, pipeline, has_empty_videosink-pipeline+12);
    strcat (pipeline_final, pipeline_vsink);
    strcat (pipeline_final, has_empty_videosink+12);
  } else {
    // append our sink at the end
    strcat (pipeline_final, pipeline);
    strcat (pipeline_final, " ! ");
    strcat (pipeline_final, pipeline_vsink);
  }

  GError *error = NULL;
  state->pipeline = gst_parse_launch (pipeline_final, &error);
  if (error) {
    g_printerr ("Could not parse pipeline %s: %s\n", pipeline_final, error->message);
    free (pipeline_final);
    g_error_free (error);
    return FALSE;
  } else {
    free (pipeline_final);
  }

  // look for video sink elements
  GstElement *glup = gst_bin_get_by_name (GST_BIN (state->pipeline), "glup");
  GstElement *capsfilter = gst_bin_get_by_name (GST_BIN (state->pipeline), "filter");
  GstElement *vsink = gst_bin_get_by_name (GST_BIN (state->pipeline), "vsink");

  // if they're not in the main pipeline, look in its video-sink bin
  if (!glup) {
    GstElement *videosink;
    g_object_get (state->pipeline, "video-sink", &videosink, NULL);
    glup = gst_bin_get_by_name (GST_BIN (videosink), "glup");
    capsfilter = gst_bin_get_by_name (GST_BIN (videosink), "filter");
    vsink = gst_bin_get_by_name (GST_BIN (videosink), "vsink");
  }

  g_object_set (capsfilter, "caps",
      gst_caps_from_string ("video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D"), NULL);
  g_object_set (vsink, "silent", TRUE, "qos", TRUE,
      "enable-last-sample", FALSE, "max-lateness", 20 * GST_MSECOND,
      "signal-handoffs", TRUE, NULL);

  // handle NO_SYNC flag
  if ((state->flags & gohai_glvideo_GLVideo_NO_SYNC)) {
    g_object_set (vsink, "sync", FALSE, NULL);
  } else {
    g_object_set (vsink, "sync", TRUE, NULL);
  }

  g_signal_connect (vsink, "preroll-handoff", G_CALLBACK (preroll_cb), state);
  g_signal_connect (vsink, "handoff", G_CALLBACK (buffers_cb), state);

  GstPad *pad = gst_element_get_static_pad (vsink, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, events_cb, state,
      NULL);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, query_cb, state,
      NULL);
  gst_object_unref (pad);

  // this seems to be necessary, otherwise close will complain about
  // gst_object_unref with object == NULL
  state->vsink = gst_object_ref (vsink);
  return TRUE;
}

static gboolean
init_device_player (GLVIDEO_STATE_T * state, GstElement * src, const char * caps)
{
  state->pipeline = gst_pipeline_new (NULL);

  GstElement *caps_src = gst_element_factory_make ("capsfilter", NULL);
  GstElement *glup = gst_element_factory_make ("glupload", "glup");
  GstElement *glcolorconv = gst_element_factory_make ("glcolorconvert", NULL);
  GstElement *capsfilter = gst_element_factory_make ("capsfilter", "filter");
  GstElement *vsink = gst_element_factory_make ("fakesink", "vsink");

  gst_bin_add_many (GST_BIN (state->pipeline), src, caps_src, glup, glcolorconv, capsfilter, vsink, NULL);
  gst_element_link_many (src, caps_src, glup, glcolorconv, capsfilter, vsink, NULL);

  g_object_set (caps_src, "caps",
      gst_caps_from_string (caps), NULL);

  // the following is the same as in init_pipeline_player

  g_object_set (capsfilter, "caps",
      gst_caps_from_string ("video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D"), NULL);
  g_object_set (vsink, "silent", TRUE, "qos", TRUE,
      "enable-last-sample", FALSE, "max-lateness", 20 * GST_MSECOND,
      "signal-handoffs", TRUE, NULL);

  // handle NO_SYNC flag
  if ((state->flags & gohai_glvideo_GLVideo_NO_SYNC)) {
    g_object_set (vsink, "sync", FALSE, NULL);
  } else {
    g_object_set (vsink, "sync", TRUE, NULL);
  }

  g_signal_connect (vsink, "preroll-handoff", G_CALLBACK (preroll_cb), state);
  g_signal_connect (vsink, "handoff", G_CALLBACK (buffers_cb), state);

  GstPad *pad = gst_element_get_static_pad (vsink, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, events_cb, state,
      NULL);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, query_cb, state,
      NULL);
  gst_object_unref (pad);

  // this seems to be necessary, otherwise close will complain about
  // gst_object_unref with object == NULL
  state->vsink = gst_object_ref (vsink);

  return TRUE;
}

static void *
glvideo_mainloop (void * data) {
  mainloop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (mainloop);
  return NULL;
}

static void
wait_for_state_change (GLVIDEO_STATE_T * state) {
  // this waits until any asynchronous state changes have completed (or failed)
  gst_element_get_state (state->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  // DEBUG: output a .dot file with the current pipeline, trigger with one of many getters that call wait_for_state_change
  if (getenv ("GST_DEBUG_DUMP_DOT_DIR")) {
    GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (state->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "playing");
  }
}

JNIEXPORT void JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1setEnvVar
  (JNIEnv * env, jclass cls, jstring _name, jstring _val) {
    const char *name = (*env)->GetStringUTFChars (env, _name, JNI_FALSE);
    const char *val = (*env)->GetStringUTFChars (env, _val, JNI_FALSE);
    setenv (name, val, 1);
    (*env)->ReleaseStringUTFChars (env, _val, val);
    (*env)->ReleaseStringUTFChars (env, _name, name);
  }

JNIEXPORT jboolean JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1init
  (JNIEnv * env, jclass cls) {
    GError *error = NULL;

    // initialize GStreamer
    gst_init_check (NULL, NULL, &error);
    if (error) {
      g_printerr ("Could not initialize library: %s\n", error->message);
      g_error_free (error);
      return JNI_FALSE;
    }

    // check if the compile-time and runtime versions match
    guint major, minor, micro, nano;
    gst_version (&major, &minor, &micro, &nano);
    if (major != GST_VERSION_MAJOR || minor != GST_VERSION_MINOR) {
      fprintf (stderr, "The version of GStreamer on your system (%u.%u.%u) does not match the version "
                       "this library was compiled against (%u.%u). This is very likely to cause issues. "
                       "Consider compiling this library yourself, using the sources from "
                       "https://github.com/gohai/processing-glvideo.git\n",
                       major, minor, micro, GST_VERSION_MAJOR, GST_VERSION_MINOR);
    }
    // we could also try to ship different builds of libglvideo, compiled against different versions
    // in this case we should return the system's version from JNI to Java and see if there is a way
    // to load in a replacement

    // save the current EGL context
#ifdef __APPLE__
    context = gst_gl_context_get_current_gl_context (GST_GL_PLATFORM_CGL);
#elif GLES2
    display = eglGetCurrentDisplay ();
    surface = eglGetCurrentSurface (0);
    context = eglGetCurrentContext ();
#else
    display = glXGetCurrentDisplay ();
    context = glXGetCurrentContext ();
#endif

    if (!context) {
      g_printerr ("GLVideo requires the P2D or P3D renderer.\n");
      g_error_free (error);
      return JNI_FALSE;
    }
    //fprintf (stderr, "GLVideo: display %p, surface %p, context %p at init\n",
    //  (void *) display, (void *) surface, (void *) context);

    // start GLib main loop in a separate thread
    thread = g_thread_new ("glvideo-mainloop", glvideo_mainloop, NULL);

    return JNI_TRUE;
  }

JNIEXPORT jstring JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1filenameToUri
  (JNIEnv * env, jclass cls, jstring _fn) {
    const char *fn = (*env)->GetStringUTFChars (env, _fn, JNI_FALSE);
    gchar *uri = gst_filename_to_uri (fn, NULL);
    (*env)->ReleaseStringUTFChars (env, _fn, fn);
    jstring ret = (*env)->NewStringUTF (env, uri);
    g_free (uri);
    return ret;
  }

JNIEXPORT jobjectArray JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1getDevices
  (JNIEnv * env, jclass cls) {
    GList *devices = NULL;
    GList *iter = NULL;
    jclass stringClass = (*env)->FindClass (env, "java/lang/String");

    GstDeviceMonitor *monitor = gst_device_monitor_new ();

    gst_device_monitor_add_filter (monitor, "Video/Source", NULL);
    devices = gst_device_monitor_get_devices (monitor);

    // count number of results
    int num_devices = 0;
    for (iter = devices; iter != NULL; iter = iter->next) {
      num_devices++;
    }

    /// allocate result
    jobjectArray row = (*env)->NewObjectArray (env, 4, stringClass, 0);
#ifndef __APPLE__
    jobjectArray ret = (*env)->NewObjectArray (env, num_devices, (*env)->GetObjectClass(env, row), 0);
#else
    // HACK: pass the stringified element as the device name on macOS
    jobjectArray ret;
    if (num_devices == 0) {
      ret = (*env)->NewObjectArray (env, 1, (*env)->GetObjectClass(env, row), 0);
    } else {
      ret = (*env)->NewObjectArray (env, num_devices, (*env)->GetObjectClass(env, row), 0);
    }
#endif
    (*env)->DeleteLocalRef(env, row);

    int i = 0;
    for (iter = devices; iter != NULL; iter = iter->next) {
      GstDevice *device = iter->data;

      // allocate result row
      jobjectArray row = (*env)->NewObjectArray (env, 4, stringClass, 0);

      gchar *display_name = gst_device_get_display_name (device);
      (*env)->SetObjectArrayElement (env, row, 0, (*env)->NewStringUTF(env, display_name));
      g_free (display_name);

      gchar *class = gst_device_get_device_class (device);
      (*env)->SetObjectArrayElement (env, row, 1, (*env)->NewStringUTF(env, class));
      g_free (class);

      GstCaps *caps = gst_device_get_caps (device);
      (*env)->SetObjectArrayElement (env, row, 2, (*env)->NewStringUTF(env, gst_caps_to_string (caps)));
      gst_caps_unref (caps);

      GstStructure *props = gst_device_get_properties (device);
      if (props) {
        gchar *temp = gst_structure_to_string (props);
        (*env)->SetObjectArrayElement (env, row, 3, (*env)->NewStringUTF(env, temp));
        g_free (temp);
        gst_structure_free (props);
      }

      // add to result
      (*env)->SetObjectArrayElement (env, ret, i, row);
      i++;
    }

#ifdef __APPLE__
    // HACK: pass the stringified element as the device name on macOS
    if (num_devices == 0) {
      fprintf (stderr, "GLVideo: Device enumeration is currently not available on macOS. The device returned is a pure guesstimate.\n");
      jobjectArray row = (*env)->NewObjectArray (env, 4, stringClass, 0);
      (*env)->SetObjectArrayElement (env, row, 0, (*env)->NewStringUTF(env, "avfvideosrc device-index=0"));
      (*env)->SetObjectArrayElement (env, row, 2, (*env)->NewStringUTF(env, ""));
      (*env)->SetObjectArrayElement (env, ret, 0, row);
    }
#endif

    // XXX: unclear how teardown is supposed to work
    gst_device_monitor_stop (monitor);
    gst_object_unref (monitor);

    return ret;
  }

GLVIDEO_STATE_T* createGlPipeline(const char * pipeline, GstElement * src, const char * caps, int flags) {
    GLVIDEO_STATE_T *state = malloc (sizeof (GLVIDEO_STATE_T));
    if (!state) {
      return 0L;
    }
    memset (state, 0, sizeof (*state));
    state->flags = flags;
    state->rate = 1.0f;

    // setup context sharing
#ifdef __APPLE__
    if (!gst_display) {
      gst_display = gst_gl_display_new ();
    } else {
      g_object_ref (gst_display);
    }
    state->gl_context =
      gst_gl_context_new_wrapped (GST_GL_DISPLAY (gst_display),
      context, GST_GL_PLATFORM_CGL, gst_gl_context_get_current_gl_api (GST_GL_PLATFORM_CGL, NULL, NULL));
#elif GLES2
    if (!gst_display) {
      gst_display = gst_gl_display_egl_new_with_egl_display (display);
    } else {
      g_object_ref (gst_display);
    }
    state->gl_context =
      gst_gl_context_new_wrapped (GST_GL_DISPLAY (gst_display),
      (guintptr) context, GST_GL_PLATFORM_EGL, GST_GL_API_GLES2);
#else
    if (!gst_display) {
      gst_display = gst_gl_display_x11_new_with_display (display);
    } else {
      g_object_ref (gst_display);
    }
    state->gl_context =
      gst_gl_context_new_wrapped (GST_GL_DISPLAY (gst_display),
      (guintptr) context, GST_GL_PLATFORM_GLX, GST_GL_API_OPENGL);
#endif

    // setup mutex to protect double buffering scheme
    g_mutex_init (&state->buffer_lock);

    if (pipeline) {
      // instantiate pipeline string
      if (!init_pipeline_player (state, pipeline)) {
        free (state);
        return NULL;
      }
    } else if (src) {
      // instantiate pipeline around source element
      if (!init_device_player (state, src, caps)) {
        free (state);
        return NULL;
      }
    }

    // connect the bus handlers
    GstBus *bus = gst_element_get_bus (state->pipeline);

    gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, state,
      NULL);
    gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);
    gst_bus_enable_sync_message_emission (bus);

    g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb,
      state);
    g_signal_connect (G_OBJECT (bus), "message::buffering",
      (GCallback) buffering_cb, state);
    g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback) eos_cb, state);
    gst_object_unref (bus);

    // start paused
    gst_element_set_state (state->pipeline, GST_STATE_PAUSED);

    return state;
}

JNIEXPORT jlong JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1openPipeline
  (JNIEnv * env, jclass cls, jstring _pipeline, jint flags) {
    GLVIDEO_STATE_T *state;
    const char *pipeline = (*env)->GetStringUTFChars (env, _pipeline, JNI_FALSE);

    state = createGlPipeline(pipeline, NULL, NULL, flags);

    (*env)->ReleaseStringUTFChars (env, _pipeline, pipeline);
    return (intptr_t) state;
  }

GstElement* getDeviceSrcElement(const char * deviceName) {
  GList *iter;
  // XXX: or reuse the same monitor?
  GstDeviceMonitor *monitor = gst_device_monitor_new ();
  GstElement *src = NULL;

  gst_device_monitor_add_filter (monitor, "Video/Source", NULL);
  iter = gst_device_monitor_get_devices (monitor);

  for (; iter != NULL; iter = iter->next) {
    GstDevice *device = iter->data;
    gchar *display_name = gst_device_get_display_name (device);
    if (!strcmp (display_name, deviceName)) {
      // match, create element
      g_free (display_name);
      src = gst_device_create_element (device, NULL);
      break;
    }
    g_free (display_name);
  }

  gst_device_monitor_stop (monitor);
  gst_object_unref (monitor);

  return src;
}

JNIEXPORT jlong JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1openDevice
  (JNIEnv * env, jclass cls, jstring _deviceName, jstring _caps, jint flags) {
    GLVIDEO_STATE_T *state;
    GstElement *src;

    const char *deviceName = (*env)->GetStringUTFChars (env, _deviceName, JNI_FALSE);
#ifndef __APPLE__
    src = getDeviceSrcElement (deviceName);
#else
    // HACK: pass the stringified element as the device name on macOS
    GError *error = NULL;
    src = gst_parse_launch (deviceName, &error);
    if (!src) {
      g_printerr ("Could not parse source element %s: %s\n", deviceName, error->message);
      g_error_free (error);
    }
#endif
    (*env)->ReleaseStringUTFChars (env, _deviceName, deviceName);

    if (!src) {
      // device not found
      return 0L;
    }

    const char *caps = (*env)->GetStringUTFChars (env, _caps, JNI_FALSE);
    state = createGlPipeline (NULL, src, caps, flags);
    (*env)->ReleaseStringUTFChars (env, _caps, caps);

    return (intptr_t) state;
  }

JNIEXPORT jboolean JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1isAvailable
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    return (state->next_tex != 0) ? JNI_TRUE : JNI_FALSE;
  }

JNIEXPORT jint JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1getFrame
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    g_mutex_lock (&state->buffer_lock);
    if (likely (state->current_buffer != NULL)) {
      gst_buffer_unref (state->current_buffer);
    }
    state->current_buffer = state->next_buffer;
    state->current_tex = state->next_tex;
    state->next_buffer = NULL;
    state->next_tex = 0;
    g_mutex_unlock (&state->buffer_lock);
    return state->current_tex;
  }

JNIEXPORT void JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1startPlayback
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;

    gst_element_set_state (state->pipeline, GST_STATE_PLAYING);
  }

JNIEXPORT jboolean JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1isPlaying
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    GstState s;

    gst_element_get_state (state->pipeline, &s, NULL, 0);
    return (s == GST_STATE_PLAYING || (s == GST_STATE_PAUSED && state->buffering));
  }

JNIEXPORT void JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1stopPlayback
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;

    gst_element_set_state (state->pipeline, GST_STATE_PAUSED);
  }

JNIEXPORT void JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1setLooping
  (JNIEnv * env, jclass cls, jlong handle, jboolean looping) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    state->looping = looping;
  }

JNIEXPORT jboolean JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1seek
  (JNIEnv * env, jclass cls, jlong handle, jfloat sec) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    GstEvent *event;

    wait_for_state_change (state);

    event = gst_event_new_seek (state->rate,
      GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      GST_SEEK_TYPE_SET, (gint64)(sec * 1000000000), GST_SEEK_TYPE_SET,
      GST_CLOCK_TIME_NONE);
    return gst_element_send_event (state->vsink, event);
  }

JNIEXPORT jboolean JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1setSpeed
  (JNIEnv * env, jclass cls, jlong handle, jfloat rate) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    GstEvent *event;
    gint64 start = 0;
    gint64 stop = 0;

    if (rate == state->rate) {
      return true;
    }

    wait_for_state_change (state);

    if (0 < rate) {
      gst_element_query_position (state->vsink, GST_FORMAT_TIME, &start);
      stop = GST_CLOCK_TIME_NONE;
    } else {
      start = 0;
      gst_element_query_position (state->vsink, GST_FORMAT_TIME, &stop);
    }

    state->rate = rate;
    event = gst_event_new_seek (state->rate,
      GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, start, GST_SEEK_TYPE_SET,
      stop);

    return gst_element_send_event (state->vsink, event);
  }

JNIEXPORT jboolean JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1setVolume
  (JNIEnv * env, jclass cls, jlong handle, jfloat vol) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    if (vol == 0.0f) {
      g_object_set (state->pipeline, "volume", 0.0, NULL);
      g_object_set (state->pipeline, "mute", TRUE, NULL);
    } else {
      g_object_set (state->pipeline, "mute", FALSE, NULL);
      g_object_set (state->pipeline, "volume", (double)vol, NULL);
    }
    return true;
  }

JNIEXPORT jfloat JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1getDuration
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    gint64 duration = 0;

    wait_for_state_change (state);

    gst_element_query_duration (state->pipeline, GST_FORMAT_TIME, &duration);
    return duration/1000000000.0f;
  }

JNIEXPORT jfloat JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1getPosition
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    gint64 position = 0;

    gst_element_query_position (state->vsink, GST_FORMAT_TIME, &position);
    return position/1000000000.0f;
  }

JNIEXPORT jint JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1getWidth
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    const GstStructure *str;
    int width = 0;

    wait_for_state_change (state);

    if (!state->caps || !gst_caps_is_fixed (state->caps)) {
      return 0;
    }
    str = gst_caps_get_structure (state->caps, 0);
    gst_structure_get_int (str, "width", &width);
    return width;
  }

JNIEXPORT jint JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1getHeight
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    const GstStructure *str;
    int height = 0;

    wait_for_state_change (state);

    if (!state->caps || !gst_caps_is_fixed (state->caps)) {
      return 0;
    }
    str = gst_caps_get_structure (state->caps, 0);
    gst_structure_get_int (str, "height", &height);
    return height;
  }

JNIEXPORT jfloat JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1getFramerate
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;
    const GstStructure *str;
    int num = 0;
    int denom = 0;

    wait_for_state_change (state);

    if (!state->caps || !gst_caps_is_fixed (state->caps)) {
      return 0.0f;
    }
    str = gst_caps_get_structure (state->caps, 0);
    gst_structure_get_fraction (str, "framerate", &num, &denom);
    return (float)num/denom;
  }

JNIEXPORT void JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1close
  (JNIEnv * env, jclass cls, jlong handle) {
    GLVIDEO_STATE_T *state = (GLVIDEO_STATE_T *)(intptr_t) handle;

    // stop pipeline
    gst_element_set_state (state->pipeline, GST_STATE_NULL);

    // free both buffers
    g_mutex_lock (&state->buffer_lock);
    if (state->current_buffer) {
      gst_buffer_unref (state->current_buffer);
      state->current_buffer = NULL;
    }
    if (state->next_buffer) {
      gst_buffer_unref (state->next_buffer);
      state->next_buffer = NULL;
    }
    g_mutex_unlock (&state->buffer_lock);

    gst_object_unref (state->vsink);
    gst_object_unref (state->pipeline);

    if (state->caps) {
      gst_caps_unref (state->caps);
    }

    gst_object_unref (state->gl_context);
    gst_object_unref (gst_display);

    g_mutex_clear (&state->buffer_lock);

    free (state);
  }
