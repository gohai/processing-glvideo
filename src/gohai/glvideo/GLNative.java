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
import processing.opengl.*;

/**
 *  @webref
 */
public class GLNative {
  public static native void gstreamer_setEnvVar(String name, String val);
  public static native boolean gstreamer_init();
  public static native long gstreamer_open(String fn_or_uri, int flags);
  public static native long gstreamer_open_capture(int index);
  public static native boolean gstreamer_isAvailable(long handle);
  public static native int gstreamer_getFrame(long handle);
  public static native void gstreamer_startPlayback(long handle);
  public static native boolean gstreamer_isPlaying(long handle);
  public static native void gstreamer_stopPlayback(long handle);
  public static native void gstreamer_setLooping(long handle, boolean looping);
  public static native boolean gstreamer_seek(long handle, float sec);
  public static native boolean gstreamer_setSpeed(long handle, float rate);
  public static native boolean gstreamer_setVolume(long handle, float vol);
  public static native float gstreamer_getDuration(long handle);
  public static native float gstreamer_getPosition(long handle);
  public static native int gstreamer_getWidth(long handle);
  public static native int gstreamer_getHeight(long handle);
  public static native float gstreamer_getFramerate(long handle);
  public static native void gstreamer_close(long handle);
}
