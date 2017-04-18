### Processing-GLVideo for Archlinux

Strictly speaking this fork should not be necessary, if for example processing core was available from maven central, there would be no need to customise the build.xml (it would be pom.xml anyway beacuse we would be doing a maven build) to take account of where processing core is on your system. It is required that you rebuild the native library, beacause on Archlinux we are at `GStreamer-1.10.4` and the gohai build is at `GStreamer-1.8.*`.

So clone this repo:-

```bash
cd repo/src/native
make
cd ../../
ant clean
ant dist
```

Then install the zip file you find in your home directory int sketchbook libraries
