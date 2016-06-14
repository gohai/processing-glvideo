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
public class GLVideo extends PImage {

  /* flags */
  public static final int MUTE = 1;

  protected static boolean loaded = false;
  protected static boolean error = false;

  protected PApplet parent;
  protected long handle = 0;
  protected Texture texture;
  protected int flags = 0;
  protected boolean simulate = false;

  /**
   *  Datatype for playing video files, which can be located in the sketch's
   *  data folder, or on a remote URL. Since this library is using hardware
   *  accelerated video playback, it is necessary to use it in combination with
   *  the P2D or P3D renderers. Make sure the video file was prepared using a codec
   *  that the GPU can natively decode (e.g. H.264 on the Raspberry Pi).
   *  @param parent typically use "this"
   *  @param fn_or_uri filename or valid URL
   */
  public GLVideo(PApplet parent, String fn_or_uri) {
    this(parent, fn_or_uri, 0);
  }

  /**
   *  @param flags pass GLVideo.MUTE to disable audio playback
   */
  public GLVideo(PApplet parent, String fn_or_uri, int flags) {
    super(0, 0, ARGB);
    this.parent = parent;
    this.flags = flags;

    //if (PApplet.platform != LINUX ||
    //    !"arm".equals(System.getProperty("os.arch"))) {
    //  System.err.println("The GL Video library is not supported on this platform. Instead of the actual video, your sketch will only receive stand-in frames that allow you to test the remainder of its functionality.");
    //  simulate = true;
    //  return;
    //} else if (!loaded) {
      System.loadLibrary("glvideo");
      loaded = true;

      String jar = GLVideo.class.getProtectionDomain().getCodeSource().getLocation().getPath();
      String nativeLib = jar.substring(0, jar.lastIndexOf(File.separatorChar));
      // set a custom plugin path and prevent globally installed libraries from being loaded
      gstreamer_setEnvVar("GST_PLUGIN_SYSTEM_PATH_1_0", "");
      // the second plugin path is necessary since the directory structure for exported applications
      // doesn't contain a linux-armv6hf directory
      gstreamer_setEnvVar("GST_PLUGIN_PATH_1_0", nativeLib + "/macosx/gstreamer-1.0/:" + nativeLib + "/gstreamer-1.0/");
      // keep a local registry
      gstreamer_setEnvVar("GST_REGISTRY_1_0", nativeLib + "/macosx/gstreamer-1.0/registry");
      // XXX: add this for linux as well
      gstreamer_setEnvVar("GST_PLUGIN_SCANNER_1_0", nativeLib + "/macosx/gst-plugin-scanner");
      // XXX: DEBUG
      //gstreamer_setEnvVar("GST_DEBUG_NO_COLOR", "1");
      //gstreamer_setEnvVar("GST_DEBUG", "4");

      // we could also set GST_GL_API & GST_GL_PLATFORM here

      if (gstreamer_init() == false) {
        error = true;
      }
    //}

    if (error) {
      throw new RuntimeException("Could not load GStreamer");
    }

    if (fn_or_uri.indexOf("://") != -1) {
      // got URI, use as is
    } else {
      // get absolute path for fn
      // first, check Processing's dataPath
      File file = new File(parent.dataPath(fn_or_uri));
      if (file.exists() == false) {
        // next, the current directory
        file = new File(fn_or_uri);
      }
      if (file.exists()) {
        fn_or_uri = file.getAbsolutePath();
      }
    }

    handle = gstreamer_open(fn_or_uri, flags);
    if (handle == 0) {
      throw new RuntimeException("Could not load video");
    }
  }

  public void dispose() {
    close();
  }

  /**
   *  Returns whether there is a new frame waiting to be displayed.
   */
  public boolean available() {
    if (simulate) {
      return true;
    } else if (handle == 0) {
      return false;
    } else {
      return gstreamer_isAvailable(handle);
    }
  }

  /**
   *  Loads the most recent frame available.
   *  After calling this method, you can use the object like any
   *  PImage, e.g. by using the image function to draw it to the
   *  screen.
   */
  public void read() {
    if (simulate) {
      if (width == 0) {
        init(1280, 720, ARGB);
        loadPixels();
        for (int y=0; y < 720; y++) {
          for (int x=0; x < 1280; x++) {
            if (y < 720/2 && 1280/2 < x ||
                720/2 < y && x < 1280/2) {
              pixels[y * 1280 + x] = 0xFF000000;
            } else {
              pixels[y * 1280 + x] = 0xFFFFFFFF;
            }
          }
        }
        updatePixels();
      }
    } else if (handle != 0) {
      // get current texture name
      int texId = gstreamer_getFrame(handle);
      // allocate Texture if needed, or simply update the texture name
      if (texture == null) {
        int w = gstreamer_getWidth(handle);
        int h = gstreamer_getHeight(handle);
        PGraphicsOpenGL pg = (PGraphicsOpenGL)parent.g;
        Texture.Parameters params = new Texture.Parameters(ARGB, POINT, false, CLAMP);
        texture = new Texture(pg, w, h, params);
        this.width = texture.width;
        this.height = texture.height;
        pg.setCache(this, texture);
      } else {
        texture.glName = texId;
      }
    }
  }

  /**
   *  Starts or resumes video playback.
   *  The play method will play a video file till the end and then stop.
   *  GLVideo objects start out paused, so you might want to call this
   *  method, or loop.
   */
  public void play() {
    if (handle != 0) {
      gstreamer_setLooping(handle, false);
      gstreamer_startPlayback(handle);
    }
  }

  /**
   *  Starts or resumes looping video playback.
   *  The loop method will continuously play back a video file.
   *  GLVideo objects start out paused, so you might want to call this
   *  method, or play.
   */
  public void loop() {
    if (handle != 0) {
      gstreamer_setLooping(handle, true);
      gstreamer_startPlayback(handle);
    }
  }

  /**
   *  Returns true if the video is playing or if playback got interrupted by buffering.
   */
  public boolean playing() {
    if (simulate) {
      return true;
    } else if (handle == 0) {
      return false;
    } else {
      return gstreamer_isPlaying(handle);
    }
  }

  /**
   *  Stops a looping video after the end of its current iteration.
   */
  public void noLoop() {
    if (handle != 0) {
      gstreamer_setLooping(handle, false);
    }
  }

  /**
   *  Jumps to a specific time position in the video file.
   *  @param sec seconds from the start of the video
   */
  public void jump(float sec) {
    if (handle != 0) {
      if (!gstreamer_seek(handle, sec)) {
        System.err.println("Cannot jump to " + sec);
      }
    }
  }

  /**
   *  Changes the speed in which a video file plays.
   *  Values larger than 1.0 will play the video faster than real time,
   *  while values lower than 1.0 will play it slower. Values less or equal
   *  than zero are currently not supported.
   *  @param rate playback rate (1.0 is real time)
   */
  public void speed(float rate) {
    if (handle != 0) {
      if (!gstreamer_setSpeed(handle, rate)) {
        System.err.println("Cannot set speed to to " + rate);
      }
    }
  }

  /**
   *  Changes the volume of the video's audio track, if there is one.
   *  @param vol (0.0 is mute, 1.0 is 100%)
   */
  public void volume(float vol) {
    if (handle != 0) {
      if (!gstreamer_setVolume(handle, vol)) {
        System.err.println("Cannot set volume to to " + vol);
      }
    }
  }

  /**
   *  Pauses a video file.
   *  Playback can be resumed with the play or loop methods.
   */
  public void pause() {
    if (handle != 0) {
      gstreamer_stopPlayback(handle);
    }
  }

  /**
   *  Returns the total length of the movie file in seconds.
   */
  public float duration() {
    if (simulate) {
      return 999.0f;
    } else if (handle == 0) {
      return 0.0f;
    } else {
      return gstreamer_getDuration(handle);
    }
  }

  /**
   *  Returns the current time position in seconds.
   */
  public float time() {
    if (handle == 0) {
      return 0.0f;
    } else {
      return gstreamer_getPosition(handle);
    }
  }

  /**
   *  Returns the native width of the movie file in pixels.
   */
  public int width() {
    if (simulate) {
      return 1280;
    } else if (handle == 0) {
      return 0;
    } else {
      return gstreamer_getWidth(handle);
    }
  }

  /**
   *  Returns the native height of the movie file in pixels.
   */
  public int height() {
    if (simulate) {
      return 720;
    } else if (handle == 0) {
      return 0;
    } else {
      return gstreamer_getHeight(handle);
    }
  }

  /**
   *  Returns the native frame rate of the movie file in frames per second (fps).
   *  This is currently not implemented.
   */
  public float frameRate() {
    if (simulate) {
      return 30.0f;
    } else if (handle == 0) {
      return 0.0f;
    } else {
      return gstreamer_getFramerate(handle);
    }
  }

  /**
   *  Closes a movie file.
   *  This method releases all resources associated with the playback of a movie file.
   *  Call close before loading and playing back a second file. After calling close
   *  no other methods can be used anymore on this GLVideo instance.
   */
  public void close() {
    if (handle != 0) {
      gstreamer_close(handle);
      handle = 0;
    }
  }


  private static native void gstreamer_setEnvVar(String name, String val);
  private static native boolean gstreamer_init();
  private native long gstreamer_open(String fn_or_uri, int flags);
  private native boolean gstreamer_isAvailable(long handle);
  private native int gstreamer_getFrame(long handle);
  private native void gstreamer_startPlayback(long handle);
  private native boolean gstreamer_isPlaying(long handle);
  private native void gstreamer_stopPlayback(long handle);
  private native void gstreamer_setLooping(long handle, boolean looping);
  private native boolean gstreamer_seek(long handle, float sec);
  private native boolean gstreamer_setSpeed(long handle, float rate);
  private native boolean gstreamer_setVolume(long handle, float vol);
  private native float gstreamer_getDuration(long handle);
  private native float gstreamer_getPosition(long handle);
  private native int gstreamer_getWidth(long handle);
  private native int gstreamer_getHeight(long handle);
  private native float gstreamer_getFramerate(long handle);
  private native void gstreamer_close(long handle);
}
