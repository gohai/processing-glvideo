/**
 *  you might need to increase your GPU memory, to avoid:
 *  OpenGL error 1285 at top endDraw(): out of memory
 */

import gohai.glvideo.*;
GLMovie video1;
GLMovie video2;

void setup() {
  size(560, 203, P2D);
  video1 = new GLMovie(this, "launch2.mp4", GLVideo.MUTE);
  video2 = new GLMovie(this, "launch2.mp4", GLVideo.MUTE);
  video2.jump(2.0);
  video1.loop();
  video2.loop();
}

void draw() {
  background(0);
  if (video1.available()) {
    video1.read();
  }
  if (video2.available()) {
    video2.read();
  }
  image(video1, 0, 0, width/2, height);
  image(video2, width/2, 0, width/2, height);
}
