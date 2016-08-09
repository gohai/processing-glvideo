/**
 *  Video Mask, based on Image Mask example
 *  move the mouse to reveal the video through the dynamic mask
 *
 *  On Raspberry Pi: increase your GPU memory, to avoid
 *  OpenGL error 1285 at top endDraw(): out of memory
 */

import gohai.glvideo.GLMovie;

GLMovie video;
PShader maskShader;
PGraphics maskImage;

void setup() {
  size(560, 406, P2D);
  video = new GLMovie(this, "launch3.mp4");
  video.loop();
  maskImage = createGraphics(width, height, P2D);
  maskImage.noSmooth();
  maskShader = loadShader("mask.glsl");
  maskShader.set("mask", maskImage);
  background(255);
}

void draw() {
  background(0);
  if (video.available()) {
    video.read();
  }

  maskImage.beginDraw();
  if (mousePressed || frameCount == 1) {
    maskImage.background(0);
  }
  if (mouseX != 0 && mouseY != 0) {
    maskImage.noStroke();
    maskImage.fill(255, 0, 0);
    maskImage.ellipse(mouseX, mouseY, 50, 50);
  }
  maskImage.endDraw();

  shader(maskShader);
  image(video, 0, 0, width, height);
}
