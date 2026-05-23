# Makefile — legacy build fallback (prefer CMake)
# Usage:
#   make              # build with auto-detected hidapi
#   make HIDAPI=libusb
#   make install
#   make clean

CXX      ?= g++
PREFIX   ?= /usr/local

# ── hidapi backend detection ─────────────────────────────────────────────────
HIDAPI   ?= hidraw
ifeq ($(shell uname),Darwin)
    HIDAPI = hidapi
endif

HIDAPI_CFLAGS  := $(shell pkg-config --cflags hidapi-$(HIDAPI) 2>/dev/null || \
                   pkg-config --cflags hidapi 2>/dev/null)
HIDAPI_LIBS    := $(shell pkg-config --libs   hidapi-$(HIDAPI) 2>/dev/null || \
                   pkg-config --libs   hidapi 2>/dev/null)

ifeq ($(HIDAPI_LIBS),)
    $(error "hidapi not found via pkg-config. Install libhidapi-dev (Debian/Ubuntu) or hidapi (Homebrew)")
endif

# ── Flags ────────────────────────────────────────────────────────────────────
CXXFLAGS := -std=c++14 -g -O2 -Wall -Wextra $(HIDAPI_CFLAGS)
LDFLAGS  := $(HIDAPI_LIBS)
NAME     := xbit_flasher
OBJS     := main.o

# ── Targets ──────────────────────────────────────────────────────────────────
.PHONY: all clean install uninstall

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $<

install: $(NAME)
	install -Dm755 $(NAME) $(DESTDIR)$(PREFIX)/bin/$(NAME)
ifeq ($(shell uname),Linux)
	install -Dm644 udev/99-xbit-flasher.rules \
	    $(DESTDIR)/etc/udev/rules.d/99-xbit-flasher.rules
	udevadm control --reload-rules || true
endif
	@echo "Installed to $(DESTDIR)$(PREFIX)/bin/$(NAME)"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(NAME)
	rm -f $(DESTDIR)/etc/udev/rules.d/99-xbit-flasher.rules

clean:
	rm -f *.o $(NAME)
