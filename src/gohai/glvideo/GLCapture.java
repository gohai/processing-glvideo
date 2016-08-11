/* -*- mode: java; c-basic-offset: 2; indent-tabs-mode: nil -*- */

/*
  Copyright (c) The Processing Foundation 2016
  Developed by Gottfried Haider

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General
  Public License along with this library; if not, write to the
  Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  Boston, MA  02111-1307  USA
*/

package gohai.glvideo;

import processing.core.*;

/**
 *  @webref
 */
public class GLCapture extends GLVideo {

  public GLCapture(PApplet parent, int index, int flags) {
    super(parent, flags);
    String pipeline;

    if (PApplet.platform == LINUX) {
      pipeline = "v4l2src device=/dev/video";
    } else if (PApplet.platform == MACOSX) {
      pipeline = "qtkitvideosrc device-index=";
    } else {
      throw new RuntimeException("Currently not supported on Windows");
    }

    handle = gstreamer_open_pipeline(pipeline + index, flags);
    if (handle == 0) {
      throw new RuntimeException("Could not open capture device");
    }
  }

  public GLCapture(PApplet parent, int index) {
    this(parent, index, 0);
  }
}
