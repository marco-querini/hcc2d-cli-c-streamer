CC ?= cc
CFLAGS ?= -O2 -Wall
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
CHECKSUM_FILES := $(SRC) $(TEST) LICENSE README.md CHANGELOG.md Makefile $(MANPAGE)

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
