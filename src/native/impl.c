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
#include <gst/gl/cocoa/gstglcontext_cocoa.h>
#elif
#include <gst/gl/egl/gstgldisplay_egl.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "impl.h"
#include "iface.h"

#define likely(x)   __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

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

static GThread *thread;
static GMainLoop *mainloop;
#ifdef __APPLE__
static guintptr context;
#elif
static EGLDisplay display;
static EGLSurface surface;
static EGLContext context;
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
  state->handled_frame = false;
  g_mutex_unlock (&state->buffer_lock);

  // wait for getFrame to read next_buffer before we return
  // this helps with 1080p25 videos on the Pi2
  bool do_exit = false;
  int tries = 20000;
  do {
#ifndef __APPLE__
    pthread_yield ();
#endif
    g_mutex_lock (&state->buffer_lock);
    if (state->handled_frame) {
      do_exit = true;
    }
    g_mutex_unlock (&state->buffer_lock);
    // give up after 20000 tries (~40ms on Pi2)
    // this does not work with clock_gettime ()
  } while (!do_exit && 0 < --tries);
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
      if (state->caps)
        gst_caps_ref (state->caps);
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
      if (gst_gl_handle_context_query (state->pipeline, query,
              (GstGLDisplay **) & state->gst_display,
              (GstGLContext **) & state->gl_context))
        return GST_PAD_PROBE_HANDLED;
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
init_playbin_player (GLVIDEO_STATE_T * state, const gchar * uri)
{
  GstPad *pad = NULL;
  GstPad *ghostpad = NULL;
  GstElement *vbin = gst_bin_new ("vbin");

  /* insert a gl filter so that the GstGLBufferPool
   * is managed automatically */
  GstElement *glup = gst_element_factory_make ("glupload", "glup");
  GstElement *capsfilter = gst_element_factory_make ("capsfilter", NULL);
  GstElement *vsink = gst_element_factory_make ("fakesink", "vsink");

  g_object_set (capsfilter, "caps",
      gst_caps_from_string ("video/x-raw(memory:GLMemory),format=RGBA"), NULL);
  g_object_set (vsink, "sync", TRUE, "silent", TRUE, "qos", TRUE,
      "enable-last-sample", FALSE, "max-lateness", 20 * GST_MSECOND,
      "signal-handoffs", TRUE, NULL);

  g_signal_connect (vsink, "preroll-handoff", G_CALLBACK (preroll_cb), state);
  g_signal_connect (vsink, "handoff", G_CALLBACK (buffers_cb), state);

  gst_bin_add_many (GST_BIN (vbin), glup, capsfilter, vsink, NULL);

  pad = gst_element_get_static_pad (glup, "sink");
  ghostpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (vbin, ghostpad);

  pad = gst_element_get_static_pad (vsink, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, events_cb, state,
      NULL);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, query_cb, state,
      NULL);
  gst_object_unref (pad);

  // this is for v4l2 devices that output YUV
  //if (strstr (uri, "v4l2://")) {
    GstElement *glcolorconvert = gst_element_factory_make ("glcolorconvert", NULL);
    gst_bin_add (GST_BIN (vbin), glcolorconvert);
    gst_element_link (glup, glcolorconvert);
    gst_element_link (glcolorconvert, capsfilter);
  //} else {
  //  gst_element_link (glup, capsfilter);
  //}

  gst_element_link (capsfilter, vsink);

  /* Instantiate and configure playbin */
  state->pipeline = gst_element_factory_make ("playbin", "player");
  GstPlayFlags flags = GST_PLAY_FLAG_NATIVE_VIDEO;
  if ((state->flags & 1) == 0) {
    flags |= GST_PLAY_FLAG_AUDIO;
    // this makes the sound work for alsasink on the Pi
    // not sure if it ideal for other sinks though
    flags |= GST_PLAY_FLAG_SOFT_VOLUME;
  }
  g_object_set (state->pipeline, "uri", uri,
      "video-sink", vbin, "flags",
      flags, NULL);

  state->vsink = gst_object_ref (vsink);
  return TRUE;
}

static gboolean
init_pipeline_player (GLVIDEO_STATE_T * state, const gchar * pipeline)
{
  const char pipeline_tail[] =
    " ! glupload name=glup ! glcolorconvert ! capsfilter name=filter ! fakesink name=vsink";

  // assemble final pipeline
  char *pipeline_final = calloc (strlen (pipeline) + strlen (pipeline_tail) + 1, sizeof (char));
  strcat (pipeline_final, pipeline);
  strcat (pipeline_final, pipeline_tail);

  GError *error = NULL;
  state->pipeline = gst_parse_launch (pipeline, &error);
  if (error) {
    g_printerr ("Could not parse pipeline %s: %s\n", pipeline_final, error->message);
    free (pipeline_final);
    g_error_free (error);
    return FALSE;
  } else {
    free (pipeline_final);
  }

  GstElement *glup = gst_bin_get_by_name (GST_BIN (state->pipeline), "glup");
  GstElement *capsfilter = gst_bin_get_by_name (GST_BIN (state->pipeline), "filter");
  GstElement *vsink = gst_bin_get_by_name (GST_BIN (state->pipeline), "vsink");

  g_object_set (capsfilter, "caps",
      gst_caps_from_string ("video/x-raw(memory:GLMemory),format=RGBA"), NULL);
  g_object_set (vsink, "sync", TRUE, "silent", TRUE, "qos", TRUE,
      "enable-last-sample", FALSE, "max-lateness", 20 * GST_MSECOND,
      "signal-handoffs", TRUE, NULL);

  g_signal_connect (vsink, "preroll-handoff", G_CALLBACK (preroll_cb), state);
  g_signal_connect (vsink, "handoff", G_CALLBACK (buffers_cb), state);

  GstPad *pad = gst_element_get_static_pad (vsink, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, events_cb, state,
      NULL);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, query_cb, state,
      NULL);
  gst_object_unref (pad);

  // XXX: still needed?
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
  // DEBUG: make sure to set GST_DEBUG_DUMP_DOT_DIR environment variable as well
  //GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (state->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "playing");
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

    // save the current EGL context
#ifdef __APPLE__
    context = gst_gl_context_cocoa_get_current_context ();
#elif
    display = eglGetCurrentDisplay ();
    surface = eglGetCurrentSurface (0);
    context = eglGetCurrentContext ();
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

JNIEXPORT jlong JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1open
  (JNIEnv * env, jclass cls, jstring _uri, jint flags) {
    GLVIDEO_STATE_T *state = malloc (sizeof (GLVIDEO_STATE_T));
    if (!state) {
      return 0L;
    }
    memset (state, 0, sizeof (*state));
    state->flags = flags;
    state->rate = 1.0f;

    // setup context sharing
#ifdef __APPLE__
    state->gst_display = gst_gl_display_new ();
    state->gl_context =
      gst_gl_context_new_wrapped (GST_GL_DISPLAY (state->gst_display),
      context, GST_GL_PLATFORM_CGL, gst_gl_context_get_current_gl_api (GST_GL_PLATFORM_CGL, NULL, NULL));
#elif
    state->gst_display = gst_gl_display_egl_new_with_egl_display (display);
    state->gl_context =
      gst_gl_context_new_wrapped (GST_GL_DISPLAY (state->gst_display),
      (guintptr) context, GST_GL_PLATFORM_EGL, GST_GL_API_GLES2);
#endif

    // setup mutex to protect double buffering scheme
    g_mutex_init (&state->buffer_lock);

    const char *uri = (*env)->GetStringUTFChars (env, _uri, JNI_FALSE);

    // instantiate pipeline
    if (!init_playbin_player (state, uri)) {
      free (state);
      (*env)->ReleaseStringUTFChars (env, _uri, uri);
      return 0L;
    }

    (*env)->ReleaseStringUTFChars (env, _uri, uri);

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

    return (intptr_t) state;
  }

JNIEXPORT jlong JNICALL Java_gohai_glvideo_GLVideo_gstreamer_1open_1pipeline
  (JNIEnv * env, jclass cls, jstring _pipeline) {
    GLVIDEO_STATE_T *state = malloc (sizeof (GLVIDEO_STATE_T));
    if (!state) {
      return 0L;
    }
    memset (state, 0, sizeof (*state));
    // state->flags = flags;
    state->rate = 1.0f;

    // setup context sharing
#ifdef __APPLE__
    state->gst_display = gst_gl_display_new ();
    state->gl_context =
      gst_gl_context_new_wrapped (GST_GL_DISPLAY (state->gst_display),
      context, GST_GL_PLATFORM_CGL, gst_gl_context_get_current_gl_api (GST_GL_PLATFORM_CGL, NULL, NULL));
#elif
    state->gst_display = gst_gl_display_egl_new_with_egl_display (display);
    state->gl_context =
      gst_gl_context_new_wrapped (GST_GL_DISPLAY (state->gst_display),
      (guintptr) context, GST_GL_PLATFORM_EGL, GST_GL_API_GLES2);
#endif

    // setup mutex to protect double buffering scheme
    g_mutex_init (&state->buffer_lock);

    // instantiate pipeline
    const char *pipeline = (*env)->GetStringUTFChars (env, _pipeline, JNI_FALSE);
    if (!init_pipeline_player (state, pipeline)) {
      (*env)->ReleaseStringUTFChars (env, _pipeline, pipeline);
      free (state);
      return 0L;
    }
    (*env)->ReleaseStringUTFChars (env, _pipeline, pipeline);

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
    state->handled_frame = true;
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
      /*
      start = 0;
      gst_element_query_position (state->vsink, GST_FORMAT_TIME, &stop);
      */
      // this currently freezes the application
      return false;
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

    gst_object_unref (state->gl_context);
    gst_object_unref (state->gst_display);

    gst_object_unref (state->vsink);
    gst_object_unref (state->pipeline);

    if (state->caps) {
      gst_caps_unref (state->caps);
    }

    g_mutex_clear (&state->buffer_lock);

    free (state);
  }
