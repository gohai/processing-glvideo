/**
 *  On Raspberry Pi: increase your GPU memory, to avoid
 *  OpenGL error 1285 at top endDraw(): out of memory
 */

import gohai.glvideo.GLMovie;
import java.awt.geom.Point2D;

PImage[] sources = new PImage[2];
int selSource = 0;
GLMovie video;
PShader shader;

PVector corners[] = new PVector[4];
int selCorner = -1;
PShape quad;

int lastMouseMove = 0;

void setup() {
  fullScreen(P2D);
  noCursor();

  video = new GLMovie(this, "launch2.mp4");
  video.loop();
  sources[0] = video;
  sources[1] = loadImage("checkerboard.png");
  shader = loadShader("quadtexfrag.glsl", "quadtexvert.glsl");

  corners[0] = new PVector(width/2 - 100, height/2 - 100);
  corners[1] = new PVector(width/2 + 100, height/2 - 100);
  corners[2] = new PVector(width/2 + 100, height/2 + 100);
  corners[3] = new PVector(width/2 - 100, height/2 + 100);

  quad = createMesh(sources[selSource], corners);
}

void draw() {
  background(0);

  if (selSource == 0 && video.available()) {
    video.read();
  }

  // regenerate mesh if we're dragging a corner
  if (selCorner != -1 && (pmouseX != mouseX || pmouseY != mouseY)) {
    corners[selCorner].x = mouseX;
    corners[selCorner].y = mouseY;
    // this improves performance, but will be replaced by a
    // more elegant way in a future release
    quad = null;
    System.gc();
    quad = createMesh(sources[selSource], corners);
  }

  // display
  if (quad != null) {
    shader(shader);
    shape(quad);
    resetShader();
  }

  // hide the mouse cursor after two seconds
  if (pmouseX != mouseX || pmouseY != mouseY) {
    cursor();
    lastMouseMove = millis();
  } else if (lastMouseMove != 0 && 2000 < millis() - lastMouseMove) {
    noCursor();
    lastMouseMove = 0;
  }
}

void mousePressed() {
  for (int i=0; i < corners.length; i++) {
    float dist = sqrt(pow(mouseX-corners[i].x, 2) + pow(mouseY-corners[i].y, 2));
    if (dist < 20) {
      selCorner = i;
      return;
    }
  }

  // no corner? then switch texture
  selSource = (selSource+1) % sources.length;
  quad = createMesh(sources[selSource], corners);
}

void mouseReleased() {
  selCorner = -1;
}

PShape createMesh(PImage tex, PVector[] corners) {
  float dx1 = corners[2].x - corners[0].x;
  float dy1 = corners[2].y - corners[0].y;
  float dx2 = corners[1].x - corners[3].x;
  float dy2 = corners[1].y - corners[3].y;
  float dx3 = corners[0].x - corners[3].x;
  float dy3 = corners[0].y - corners[3].y;

  float crs = dx1 * dy2 - dy1 * dx2;
  float cqpr = dx1 * dy3 - dy1 * dx3;
  float cqps = dx2 * dy3 - dy2 * dx3;

  float t = cqps / crs;
  float u = cqpr / crs;

  PShape quad = createShape();
  quad.beginShape(QUADS);
  quad.texture(tex);
  
  quad.attrib("texCoordQ", 1.0 / (1.0 - t));
  quad.vertex(corners[0].x, corners[0].y, 0, 0);

  quad.attrib("texCoordQ", 1.0 / (u));
  quad.vertex(corners[1].x, corners[1].y, tex.width, 0);

  quad.attrib("texCoordQ", 1.0 / (t));
  quad.vertex(corners[2].x, corners[2].y, tex.width, tex.height);

  quad.attrib("texCoordQ", 1.0 / (1.0 - u));
  quad.vertex(corners[3].x, corners[3].y, 0, tex.height);
  
  quad.endShape();
  
  return quad;
}