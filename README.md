## OpenGL video playback for Processing

This library makes use of GStreamer and OpenGL hardware (or software) acceleration to display video in Processing's P2D or P3D renderers. It should run on Linux and Raspbian.

## Installing

### Linux

Install the GStreamer 1.x software from your distribution's repositories. The actual packages might be named differently between distributions, but you want the the equivalents of:

`gstreamer1.0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-gst-plugins-bad` and either `gstreamer1.0-ffmpeg` or `gstreamer1.0-libav`

Additional plugins, such as `omx` or `vaapi`, could additionally unlock hardware-accelerated decoding.

Install the library through the Processing's _Contribution Manager_. Try out the example sketches that it comes with. You might receive a warning if your version of GStreamer does not match the version that library was compiled against, which is currently 1.10. If this is the case, and you experiences errors or crashes, you might want to try re-building the library from source against your particular GStreamer version.

### Raspbian

*Do not enable* the KMS OpenGL driver, but stick with the _legacy_ one. Ironically, enabling this module disables GPU video support.

Install the library through the Processing's _Contribution Manager_. Try out the example sketches that it comes with. On Raspbian, a local copy of GStreamer 1.12 is bundled together with the library.
