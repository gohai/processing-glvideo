/**
 *  This will display live video from a GoPro camera connected via WiFi.
 *  Code & detective work by Gal Nissim & Andres Colubri.
 *
 *  More about GStreamer pipelines:
 *  https://gstreamer.freedesktop.org/data/doc/gstreamer/head/manual/html/
 */

import gohai.glvideo.*;
GLVideo video;

void setup() {
  size(320, 240, P2D);
  // we connect to the camera via this custom GStreamer pipeline
  video = new GLVideo(this, "udpsrc port=8554 ! tsdemux ! decodebin");
  video.play();
}

void draw() {
  background(0);
  if (video.available()) {
    video.read();
  }
  image(video, 0, 0, width, height);
}
