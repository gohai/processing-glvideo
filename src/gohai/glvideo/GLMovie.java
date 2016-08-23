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

import java.io.File;
import processing.core.*;

/**
 *  @webref
 */
public class GLMovie extends GLVideo {

  protected String uri;

  public GLMovie(PApplet parent, String fn_or_uri, int flags) {
    super(parent, flags);

    if (fn_or_uri.indexOf("://") != -1) {
      uri = fn_or_uri;
    } else {
      uri = filenameToUri(fn_or_uri);
    }

    handle = gstreamer_open_pipeline("uridecodebin uri=" + uri, flags);
    if (handle == 0) {
      throw new RuntimeException("Could not load video");
    }
  }

  public GLMovie(PApplet parent, String fn_or_uri) {
    this(parent, fn_or_uri, 0);
  }

  protected String filenameToUri(String fn) {
    // get absolute path for fn
    // first, check Processing's dataPath
    File file = new File(parent.dataPath(fn));
    if (file.exists() == false) {
      // next, the current directory
      file = new File(fn);
    }
    if (file.exists()) {
      fn = file.getAbsolutePath();
    }
    // use GStreamer to encode the filename into a URI it likes
    return gstreamer_filenameToUri(fn);
  }
}
