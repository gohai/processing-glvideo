/**
 *  Video Edge Detect, based on Edge Detect example
 *
 *  On Raspberry Pi: increase your GPU memory, to avoid
 *  OpenGL error 1285 at top endDraw(): out of memory
 */

import gohai.glvideo.GLMovie;

GLMovie video;
PShader edges;
boolean enabled = true;

void setup() {
  size(560, 406, P2D);
  video = new GLMovie(this, "launch2.mp4");
  video.loop();
  edges = loadShader("edges.glsl");
}

void draw() {
  background(0);
  if (video.available()) {
  	video.read();
  }
  if (enabled == true) {
    shader(edges);
  }
  image(video, 0, 0, width, height);
}

void mousePressed() {
  enabled = !enabled;
  if (!enabled == true) {
    resetShader();
  }
}
