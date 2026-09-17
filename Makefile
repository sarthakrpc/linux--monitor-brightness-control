CFLAGS ?= -O2 -Wall
CFLAGS += -Wno-deprecated-declarations $(shell pkg-config --cflags gtk+-3.0)
# libxapp1 is always installed on Mint, its -dev symlink/headers usually are not.
LDLIBS += $(shell pkg-config --libs gtk+-3.0) -l:libxapp.so.1

# Per-user install, no root needed.
PREFIX   ?= $(HOME)/.local
BIN       = $(PREFIX)/bin/brightness-control
APPS_DIR  = $(PREFIX)/share/applications
AUTOSTART = $(HOME)/.config/autostart
DESKTOP   = brightness-control.desktop

brightness-control: main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

install: brightness-control
	install -Dm755 brightness-control $(BIN)
	mkdir -p $(APPS_DIR) $(AUTOSTART)
	sed 's|@BIN@|$(BIN)|' $(DESKTOP).in > $(APPS_DIR)/$(DESKTOP)
	cp $(APPS_DIR)/$(DESKTOP) $(AUTOSTART)/$(DESKTOP)
	-update-desktop-database $(APPS_DIR) 2>/dev/null

uninstall:
	rm -f $(BIN) $(APPS_DIR)/$(DESKTOP) $(AUTOSTART)/$(DESKTOP)

# System-wide package: sudo apt install ./brightness-control_$(VERSION)_$(ARCH).deb
VERSION = 1.1.0
ARCH    = $(shell dpkg --print-architecture)
PKG     = brightness-control_$(VERSION)_$(ARCH)

deb: brightness-control
	rm -rf build/$(PKG)
	install -Dm755 brightness-control build/$(PKG)/usr/bin/brightness-control
	install -d -m755 build/$(PKG)/usr/share/applications build/$(PKG)/etc/xdg/autostart build/$(PKG)/DEBIAN
	sed 's|@BIN@|/usr/bin/brightness-control|' $(DESKTOP).in > build/$(DESKTOP)
	install -m644 build/$(DESKTOP) build/$(PKG)/usr/share/applications/$(DESKTOP)
	install -m644 build/$(DESKTOP) build/$(PKG)/etc/xdg/autostart/$(DESKTOP)
	sed -e 's|@VERSION@|$(VERSION)|' -e 's|@ARCH@|$(ARCH)|' debian-control.in > build/$(PKG)/DEBIAN/control
	echo /etc/xdg/autostart/$(DESKTOP) > build/$(PKG)/DEBIAN/conffiles
	chmod 644 build/$(PKG)/DEBIAN/control build/$(PKG)/DEBIAN/conffiles
	chmod -R go-w build/$(PKG)
	dpkg-deb --build --root-owner-group build/$(PKG) $(PKG).deb

clean:
	rm -rf brightness-control build *.deb

.PHONY: install uninstall deb clean
