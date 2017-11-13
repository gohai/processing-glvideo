/**
 *  Press the mouse button to change playback direction
 *  On Raspberry Pi: increase your GPU memory, to avoid
 *  OpenGL error 1285 at top endDraw(): out of memory
 */

import gohai.glvideo.*;
GLMovie video;
boolean forward = true;

void setup() {
  size(560, 406, P2D);
  video = new GLMovie(this, "launch1.mp4");
  video.loop();
}

void draw() {
  background(0);
  if (video.available()) {
    video.read();
  }
  image(video, 0, 0, width, height);
}

void mousePressed() {
  forward = !forward;
  if (forward == true) {
    video.speed(1.0);
  } else {
    video.speed(-1.0);
  }
}