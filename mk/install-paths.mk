# Runtime paths shared by source-tree installs and Debian package staging.
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
LIBEXECDIR ?= $(PREFIX)/libexec/rustyrig
MULTIARCH ?= $(shell dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || gcc -print-multiarch)
LIBDIR ?= $(PREFIX)/lib/$(MULTIARCH)
MODULEDIR ?= $(PREFIX)/lib/rustyrig/modules
DATADIR ?= /var/lib/rustyrig
UI_SCRIPT_DIR ?= $(PREFIX)/share/rustyrig/scripts/client/ui
WEB_ROOT ?= $(DATADIR)/www
HELP_ROOT ?= $(DATADIR)/help
SYSCONFDIR ?= /etc/rustyrig
CONF_DIR ?= $(SYSCONFDIR)
DESTDIR ?=
