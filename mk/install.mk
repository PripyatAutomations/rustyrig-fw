# Source installs deliberately target the files owned by Debian packages,
# not /usr/local shadow copies. Paths are in mk/install-paths.mk.
CONFIG_FILES = $(filter-out config/callsign-lookup.cfg,$(wildcard config/*.cfg config/*.md)) config/ua-bans
LOG_TOOLS = tools/rr-get-audit-log tools/rr-get-chat-log tools/rr-get-ptt-log
# Only libraries still owned by rustyrig-fw may be installed here.
RUSTYRIG_OWNED_LIBS = $(libs)

.PHONY: windows-install posix-install install uninstall install-dependency-check install-bin install-modules install-libs install-config install-assets install-tools install-sql install-dev install-check

windows-install:
	@echo "Use the win64-installer target for Windows"

posix-install: install

install-dependency-check:
	@echo 'Using installed librustyaxe, librrprotocol and librustyirc'

# No configuration is touched by the default install target.
install: install-dependency-check
	$(MAKE) install-bin install-modules install-libs install-assets install-tools install-sql
	@echo "Installed runtime files at Debian package paths. Existing /etc/rustyrig config is untouched."
	@echo "Run ldconfig after installing libraries on Linux."

install-bin: ${bins}
	@set -e; for src in $(filter-out %.so,${bins}); do \
		install -Dm755 "$$src" "$(DESTDIR)$(BINDIR)/$$(basename "$$src")"; \
	done
# Runtime modules are separate from executable binaries; show their destination
# so source installs are auditable rather than silently copying shared objects.
install-modules: $(filter %.so,${bins})
	@set -e; for src in $(filter %.so,${bins}); do \
		case "$$src" in \
		*rrbridge*.so) family=rrbridge ;; \
		*rrserver*.so) family=rrserver ;; \
		*rrclient*.so) family=rrclient ;; \
		*) echo "Unknown runtime module: $$src" >&2; exit 1 ;; \
		esac; \
		dst="$(DESTDIR)$(MODULEDIR)/$$family/$${src##*/}"; \
		install -Dm755 "$$src" "$$dst"; \
		echo "[install-module] $$src => $$dst"; \
	done

# Install real SONAME objects, never copy a symlink over a shared object.
install-libs: $(RUSTYRIG_OWNED_LIBS)
	@set -e; for src in $(RUSTYRIG_OWNED_LIBS); do \
		install -Dm755 "$$src" "$(DESTDIR)$(LIBDIR)/$$(basename "$$src").0"; \
	done

install-dev: install-libs
	@set -e; for src in $(RUSTYRIG_OWNED_LIBS); do \
		ln -sfn "$$(basename "$$src").0" "$(DESTDIR)$(LIBDIR)/$$(basename "$$src")"; \
	done
	@set -e; for component in libfwdspmgr; do \
		install -d "$(DESTDIR)$(PREFIX)/include/$$component"; \
		for header in $$component/*.h; do \
			[ -f "$$header" ] || continue; \
			[ "$$header" != librustyirc/internal.h ] || continue; \
			install -m644 "$$header" "$(DESTDIR)$(PREFIX)/include/$$component/"; \
		done; \
	done

install-assets:
	# Generated from the built-in stylesheet. Reference only; never loaded by GTK.
	install -d "$(DESTDIR)$(CONF_DIR)"
	perl tools/releng-dump-css.pl --css > "$(DESTDIR)$(CONF_DIR)/rrclient.css"
	chmod 644 "$(DESTDIR)$(CONF_DIR)/rrclient.css"
	install -Dm644 res/rustyrig.png "$(DESTDIR)$(PREFIX)/share/icons/hicolor/48x48/apps/rustyrig.png"
	install -Dm644 packaging/rustyrig-client.desktop "$(DESTDIR)$(PREFIX)/share/applications/rustyrig-client.desktop"
	install -d "$(DESTDIR)$(WEB_ROOT)"
	@if [ -d www ]; then cp -a www/. "$(DESTDIR)$(WEB_ROOT)/"; fi

# SQL templates are shipped runtime data, unlike operator-maintained config.
# Update them on every source install; never overwrite the live master.db.
install-sql:
	install -Dm644 sql/sqlite.master.sql "$(DESTDIR)$(DATADIR)/sql/sqlite.master.sql"
	install -Dm644 sql/sqlite.master.preload.sql "$(DESTDIR)$(DATADIR)/sql/sqlite.master.preload.sql"
	install -Dm644 sql/sqlite.client.sql "$(DESTDIR)$(DATADIR)/sql/sqlite.client.sql"
	@echo "[install-sql] schema and preload => $(DESTDIR)$(DATADIR)/sql/"

install-tools:
	@set -e; for src in ${LOG_TOOLS}; do \
		install -Dm755 "$$src" "$(DESTDIR)$(BINDIR)/$$(basename "$$src")"; \
	done

# Explicit opt-in: never alter configuration as a side effect of make install.
install-config:
	install -d "$(DESTDIR)$(CONF_DIR)"
	@set -e; for src in ${CONFIG_FILES}; do \
		dst="$(DESTDIR)$(CONF_DIR)/$$(basename "$$src")"; \
		if [ -e "$$dst" ]; then \
			if cmp -s "$$src" "$$dst"; then echo "unchanged: $$dst"; continue; fi; \
			printf 'Replace %s? [y/N] ' "$$dst"; \
			read answer || answer=n; \
			case "$$answer" in y|Y|yes|YES) ;; *) echo "keeping: $$dst"; continue;; esac; \
		fi; \
		install -Dm644 "$$src" "$$dst"; \
	done

# Report the actual effective paths, and reject common shadow-install traps.
install-check:
	@echo "Binaries:    $(BINDIR)"
	@echo "Libraries:   $(LIBDIR)"
	@echo "Modules:     $(MODULEDIR)"
	@echo "Config:      $(CONF_DIR) (install-config only)"
	@echo "Web UI:      $(WEB_ROOT)"
	@test "$(BINDIR)" = /usr/bin || { echo "Not Debian binary directory" >&2; exit 1; }
	@test "$(MODULEDIR)" = /usr/lib/rustyrig/modules || { echo "Not Debian module directory" >&2; exit 1; }
	@test "$(LIBDIR)" = "/usr/lib/$(MULTIARCH)" || { echo "Not Debian multiarch library directory" >&2; exit 1; }
	@test "$(CONF_DIR)" = /etc/rustyrig || { echo "Not Debian config directory" >&2; exit 1; }
	@test "$(WEB_ROOT)" = /var/lib/rustyrig/www || { echo "Not Debian WebUI directory" >&2; exit 1; }
	@echo "Runtime paths match Debian's installation layout."

# Reverse only files RustyRig owns. Never remove independently installed
# librustyaxe, librustyirc, librrprotocol, operator configs, or state databases.
# Installation of web assets may mingle with user data, so leave them in place.
uninstall:
	@set -e; for src in $(filter-out %.so,$(bins)); do \
		rm -f "$(DESTDIR)$(BINDIR)/$$(basename "$$src")"; \
	done
	@set -e; for src in $(filter %.so,$(bins)); do \
		case "$$src" in *rrbridge*.so) family=rrbridge ;; *rrserver*.so) family=rrserver ;; *rrclient*.so) family=rrclient ;; *) continue;; esac; \
		rm -f "$(DESTDIR)$(MODULEDIR)/$$family/$${src##*/}"; \
	done
	@set -e; for src in $(RUSTYRIG_OWNED_LIBS); do \
		rm -f "$(DESTDIR)$(LIBDIR)/$$(basename "$$src")" "$(DESTDIR)$(LIBDIR)/$$(basename "$$src").0"; \
	done
	@set -e; for src in $(LOG_TOOLS); do rm -f "$(DESTDIR)$(BINDIR)/$$(basename "$$src")"; done
	rm -f "$(DESTDIR)$(CONF_DIR)/rrclient.css"
	rm -f "$(DESTDIR)$(PREFIX)/share/icons/hicolor/48x48/apps/rustyrig.png"
	rm -f "$(DESTDIR)$(PREFIX)/share/applications/rustyrig-client.desktop"
	rm -f "$(DESTDIR)$(DATADIR)/sql/sqlite.master.sql" "$(DESTDIR)$(DATADIR)/sql/sqlite.master.preload.sql" "$(DESTDIR)$(DATADIR)/sql/sqlite.client.sql"
	@echo "Removed RustyRig-owned installed files; retained configs, state and web data."
