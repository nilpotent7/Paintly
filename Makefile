# ============================================================================
#  Paintly - Makefile
#
#  Targets:
#    make            build the app into build/paintly
#    make run        build and launch
#    make clean      delete all build output
#    make install    install binary + .desktop + icon  (PREFIX=/usr/local)
#    make uninstall  remove installed files
#
#  The only hard requirements are a C compiler, pkg-config, and the GTK4
#  development package (libgtk-4-dev on Debian/Ubuntu).  glib-compile-resources
#  ships with the GLib dev package, which GTK4 pulls in automatically.
# ============================================================================

APP     := paintly
BUILD   := build
PREFIX  ?= /usr/local

# Overridable so the app can be built against a non-system GTK if needed.
PKG_CFLAGS ?= $(shell pkg-config --cflags gtk4)
PKG_LIBS   ?= $(shell pkg-config --libs gtk4)
GLIB_COMPILE_RESOURCES ?= glib-compile-resources

VERSION := 0.1.0

CC     ?= gcc
# -Wno-missing-field-initializers: GActionEntry has private padding fields
# that idiomatic GLib code never initializes.
CFLAGS += -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter \
          -Wno-missing-field-initializers \
          $(PKG_CFLAGS) -Isrc -MMD -MP\
		  -DPAINTLY_VERSION=\"$(VERSION)\"
LDLIBS += $(PKG_LIBS) -lm

SRCS := $(wildcard src/*.c src/core/*.c src/tools/*.c src/ui/*.c)
OBJS := $(SRCS:src/%.c=$(BUILD)/obj/%.o) $(BUILD)/obj/resources.o
DEPS := $(OBJS:.o=.d)

APPID   := io.github.nilpotent7.Paintly
DATADIR := $(DESTDIR)$(PREFIX)/share

all: $(BUILD)/$(APP)

$(BUILD)/$(APP): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@echo "==> built $@"

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Embedded assets (CSS + icons) are compiled into C code so the final binary
# is fully self-contained - no files to locate at runtime, nothing to install
# besides the executable itself.
$(BUILD)/resources.c: data/resources.xml data/style.css $(wildcard data/icons/*.svg)
	@mkdir -p $(BUILD)
	$(GLIB_COMPILE_RESOURCES) --sourcedir=data --generate-source --target=$@ data/resources.xml

$(BUILD)/obj/resources.o: $(BUILD)/resources.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

run: all
	$(BUILD)/$(APP)

clean:
	rm -rf $(BUILD)

install: all
	install -Dm755 $(BUILD)/$(APP)  $(DESTDIR)$(PREFIX)/bin/$(APP)
	install -Dm644 data/$(APPID).desktop \
	        $(DATADIR)/applications/$(APPID).desktop
	install -Dm644 data/$(APPID).metainfo.xml \
	        $(DATADIR)/metainfo/$(APPID).metainfo.xml
	install -Dm644 data/icons/paintly-app.svg \
	        $(DATADIR)/icons/hicolor/scalable/apps/$(APPID).svg
	@echo "==> installed to $(PREFIX)"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(APP)
	rm -f $(DATADIR)/applications/$(APPID).desktop
	rm -f $(DATADIR)/metainfo/$(APPID).metainfo.xml
	rm -f $(DATADIR)/icons/hicolor/scalable/apps/$(APPID).svg

-include $(DEPS)
.PHONY: all run clean install uninstall
