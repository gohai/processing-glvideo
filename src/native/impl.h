#ifndef GLUE_H
#define GLUE_H

typedef struct {
  GstElement *pipeline;
  GstElement *vsink;
  GstCaps *caps;

#ifdef __APPLE__
  GstGLDisplay *gst_display;
#elif
  GstGLDisplayEGL *gst_display;
#endif
  GstGLContext *gl_context;

  GMutex buffer_lock;
  // protects the following
  GstBuffer *current_buffer;
  GLuint current_tex;
  GstBuffer *next_buffer;
  GLuint next_tex;
  bool handled_frame;

  int flags;

  bool looping;
  float rate;
  bool buffering;
} GLVIDEO_STATE_T;

#endif
