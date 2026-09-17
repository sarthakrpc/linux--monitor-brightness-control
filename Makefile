# brightness-control - build into build/
#
#   make            release build: build/brightness-control
#   make deb        the installable package: build/brightness-control_<version>_amd64.deb
#   make run        build and start it from this folder
#   make clean

VERSION := 1.2

CFLAGS ?= -O2 -Wall
CFLAGS += -Wno-deprecated-declarations -DVERSION='"$(VERSION)"' $(shell pkg-config --cflags gtk+-3.0)
# libxapp1 is on every Mint install; its -dev symlink and headers usually are
# not, so link against the runtime library by its exact name.
LDLIBS += $(shell pkg-config --libs gtk+-3.0) -l:libxapp.so.1

.PHONY: all deb run clean

all: build/brightness-control

# Rebuilt when the Makefile changes too, so a version bump reaches the binary.
build/brightness-control: main.c Makefile
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ main.c $(LDLIBS)

run: all
	./build/brightness-control

# Everything a machine needs, in one file you can double-click. Once you have
# this, the source tree is no longer needed to install or reinstall.
deb: all
	./dist/build-deb.sh

clean:
	rm -rf build
