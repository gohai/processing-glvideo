/**
 *  On Raspberry Pi: increase your GPU memory, to avoid
 *  OpenGL error 1285 at top endDraw(): out of memory
 */

import gohai.glvideo.GLVideo;
import gohai.glvideo.PerspectiveTransform;
import gohai.glvideo.WarpPerspective;
import java.awt.geom.Point2D;

PImage[] sources = new PImage[2];
int selSource = 0;
GLVideo video;

PVector corners[] = new PVector[4];
int selCorner = -1;
PShape quads[];
int res = 5;	// number of subdivisions (e.g. 5x5)

int lastMouseMove = 0;

void setup() {
  fullScreen(P2D);
  noCursor();

  video = new GLMovie(this, "launch2.mp4");
  video.loop();
  sources[0] = video;
  sources[1] = loadImage("checkerboard.png");

  corners[0] = new PVector(width/2 - 100, height/2 - 100);
  corners[1] = new PVector(width/2 + 100, height/2 - 100);
  corners[2] = new PVector(width/2 + 100, height/2 + 100);
  corners[3] = new PVector(width/2 - 100, height/2 + 100);

  quads = createMesh(sources[selSource], corners, res);
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
    quads = null;
    System.gc();
    quads = createMesh(sources[selSource], corners, res);
  }

  // display
  if (quads != null) {
    for (int i=0; i < quads.length; i++) {
      shape(quads[i]);
    }
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
  quads = createMesh(sources[selSource], corners, res);
}

void mouseReleased() {
  selCorner = -1;
}

PShape[] createMesh(PImage tex, PVector[] corners, int res) {
  PerspectiveTransform transform = PerspectiveTransform.getQuadToQuad(
    0, 0, tex.width, 0,                   // top left, top right
    tex.width, tex.height, 0, tex.height, // bottom right, bottom left
    corners[0].x, corners[0].y, corners[1].x, corners[1].y, 
    corners[2].x, corners[2].y, corners[3].x, corners[3].y);

  WarpPerspective warpPerspective = new WarpPerspective(transform);

  float xStep = (float) tex.width / res;
  float yStep = (float) tex.height / res;

  PShape quads[] = new PShape[res*res];
  for (int y=0; y < res; y++) {
    for (int x=0; x < res; x++) {

      textureMode(NORMAL);
      PShape sh = createShape();
      sh.beginShape(QUAD);
      sh.noStroke();
      sh.texture(tex);
      sh.normal(0, 0, 1);
      Point2D point = warpPerspective.mapDestPoint(x*xStep, y*yStep);
      sh.vertex((float)point.getX(), (float)point.getY(), 0, (float)x / res, (float)y / res);
      point = warpPerspective.mapDestPoint((x+1)*xStep, y*yStep);
      sh.vertex((float)point.getX(), (float)point.getY(), 0, (float)(x+1) / res, (float)y / res);
      point = warpPerspective.mapDestPoint((x+1)*xStep, (y+1)*yStep);
      sh.vertex((float)point.getX(), (float)point.getY(), 0, (float)(x+1) / res, (float)(y+1) / res);
      point = warpPerspective.mapDestPoint(x*xStep, (y+1)*yStep);
      sh.vertex((float)point.getX(), (float)point.getY(), 0, (float)x / res, (float)(y+1) / res);
      sh.endShape();

      quads[y * res + x] = sh;
    }
  }
  return quads;
}