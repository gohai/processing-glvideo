TARGET := libglvideo.so
OBJS := impl.o
CC := gcc
PLATFORM := $(shell uname -s)


CFLAGS := -fPIC -O2 -mtune=native
ifeq ($(PLATFORM),Linux)
	CFLAGS += -I$(shell dirname $(shell realpath $(shell which javac)))/../include
	CFLAGS += -I$(shell dirname $(shell realpath $(shell which javac)))/../include/linux
	CFLAGS += -I/opt/vc/include
	CFLAGS += -I/opt/vc/include/interface/vcos/pthreads
	CFLAGS += $(shell pkg-config gstreamer-1.0 --cflags-only-I)
endif
ifeq ($(PLATFORM),Darwin)
	# this is currently 64-bit only
	CFLAGS += -I/Library/Frameworks/GStreamer.framework/Versions/1.0/Headers
	CFLAGS += -I/System/Library/Frameworks/JavaVM.framework/Versions/Current/Headers
endif

LDFLAGS := -shared
ifeq ($(PLATFORM),Linux)
	LDFLAGS += -L/opt/vc/lib
	LDFLAGS += $(shell pkg-config gstreamer-1.0 --libs)
	LDFLAGS += $(shell pkg-config gstreamer-gl-1.0 --libs)
	LDFLAGS += -L../../library/linux-armv6hf
	LDFLAGS += -Wl,-R,'$$ORIGIN'
	TARGET_DIR = linux-armv6hf
endif
ifeq ($(PLATFORM),Darwin)
	# this is currently 64-bit only
	# download and install latest gstreamer-1.0-devel package for x86_64 from
	# https://gstreamer.freedesktop.org/data/pkg/osx/1.8.1/
	LDFLAGS += -L/Library/Frameworks/GStreamer.framework/Versions/1.0/lib
	LDFLAGS += -lgstgl-1.0 -lgstreamer-1.0 -lgstapp-1.0 -lglib-2.0 -lgobject-2.0
	# XXX: set rpath
	TARGET_DIR = macosx
	# XXX: extension needs to be .jnilib instead of .so
endif

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	mkdir -p ../../library/$(TARGET_DIR)
	cp $(TARGET) ../../library/$(TARGET_DIR)

iface.h:
	javah -classpath .. -o iface.h gohai.glvideo.GLVideo

clean:
	rm -f $(TARGET) $(OBJS)
