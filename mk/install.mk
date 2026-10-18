# Source installs deliberately target the files owned by Debian packages,
# not /usr/local shadow copies. Paths are in mk/install-paths.mk.
CONFIG_FILES = config/rrserver.cfg $(wildcard config/rrserver.*.cfg) \
    config/rrclient.cfg config/callsign-lookup.cfg \
    config/callsign-lookup.cli.cfg $(CF)
LOG_TOOLS = tools/rr-get-audit-log tools/rr-get-chat-log tools/rr-get-ptt-log

.PHONY: windows-install posix-install install install-bin install-modules install-libs install-config install-assets install-tools install-dev install-check

windows-install:
	@echo "Use the win64-installer target for Windows"

posix-install: install

# No configuration is touched by the default install target.
install: install-bin install-modules install-libs install-assets install-tools
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
		dst="$(DESTDIR)$(MODULEDIR)/$family/$(basename "$src")"; \
		install -Dm755 "$$src" "$$dst"; \
		echo "[install-module] $$src => $$dst"; \
	done

# Install real SONAME objects, never copy a symlink over a shared object.
install-libs: ${libs}
	@set -e; for src in ${libs}; do \
		install -Dm755 "$$src" "$(DESTDIR)$(LIBDIR)/$$(basename "$$src").0"; \
	done

install-dev: install-libs
	@set -e; for src in ${libs}; do \
		ln -sfn "$$(basename "$$src").0" "$(DESTDIR)$(LIBDIR)/$$(basename "$$src")"; \
	done
	@set -e; for component in librustyaxe librrprotocol libfwdspmgr; do \
		install -d "$(DESTDIR)$(PREFIX)/include/$$component"; \
		for header in $$component/*.h; do \
			[ -f "$$header" ] || continue; \
			install -m644 "$$header" "$(DESTDIR)$(PREFIX)/include/$$component/"; \
		done; \
	done

install-assets:
	install -Dm644 res/rustyrig.png "$(DESTDIR)$(PREFIX)/share/icons/hicolor/48x48/apps/rustyrig.png"
	install -Dm644 packaging/rustyrig-client.desktop "$(DESTDIR)$(PREFIX)/share/applications/rustyrig-client.desktop"
	install -d "$(DESTDIR)$(WEB_ROOT)"
	cp -a www/. "$(DESTDIR)$(WEB_ROOT)/"

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
