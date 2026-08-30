CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= $(shell pkg-config --cflags sdl2)
LDFLAGS ?=
LDLIBS ?= $(shell pkg-config --libs sdl2) -lz -lm
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
MAN1DIR ?= $(MANDIR)/man1
INSTALL ?= install

TARGET := hcc2d_streamer
SRC := single_file_c_hcc2d_streamer_v0.9.0.c
MANPAGE := hcc2d_streamer.1
TEST := test_single_file_streamer.sh
CHECKSUM_FILES := \
	.gitignore \
	CHANGELOG.md \
	LICENSE \
	Makefile \
	README.md \
	apt-repo/README.md \
	apt-repo/public/hcc2d-archive-keyring.gpg \
	apt-repo/repo.conf \
	apt-repo/scripts/check.sh \
	apt-repo/scripts/export-public-key.sh \
	apt-repo/scripts/publish.sh \
	debian/changelog \
	debian/compat \
	debian/control \
	debian/copyright \
	debian/docs \
	debian/hcc2d-streamer.lintian-overrides \
	debian/manpages \
	debian/rules \
	debian/source/format \
	debian/tests/control \
	debian/tests/smoke \
	$(MANPAGE) \
	$(SRC) \
	$(TEST)

.PHONY: all clean checksum test install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET) *.o

checksum:
	sha256sum $(CHECKSUM_FILES) > SHA256SUMS.txt

test: $(TEST) $(SRC)
	./$(TEST)

install: $(TARGET) $(MANPAGE)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 0755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	$(INSTALL) -d "$(DESTDIR)$(MAN1DIR)"
	$(INSTALL) -m 0644 $(MANPAGE) "$(DESTDIR)$(MAN1DIR)/$(MANPAGE)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f "$(DESTDIR)$(MAN1DIR)/$(MANPAGE)"
