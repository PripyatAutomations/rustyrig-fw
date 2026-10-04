###################
# install targets #
###################
CONFIG_FILES=config/rrserver.cfg config/rrclient.cfg \
	config/callsign-lookup.cfg config/callsign-lookup.srv.cfg \
	config/callsign-lookup.cli.cfg ${CF}
LOG_TOOLS=tools/rr-get-audit-log tools/rr-get-chat-log tools/rr-get-ptt-log
WEB_ROOT ?= /var/lib/rustyrig/www

.PHONY: windows-install posix-install install install-config install-assets install-tools

windows-install:
	@echo "Windows builds do not need to install. A NSIS installer can be built using win64-installer target"

posix-install: install-config install-assets install-tools
	mkdir -p ${INSTALL_DIR}/bin ${INSTALL_DIR}/etc ${INSTALL_DIR}/share
	install -Dm755 ${bin} ${INSTALL_DIR}/bin/$(shell basename "${bin}")

install: install-config install-assets install-tools
	mkdir -p ${INSTALL_DIR}/bin ${INSTALL_DIR}/lib ${INSTALL_DIR}/etc ${INSTALL_DIR}/share
	cp -av $(filter-out %.so,${bins}) ${INSTALL_DIR}/bin
	# Frontend and GPS shared objects also go in the module directory.
	install -d ${INSTALL_DIR}/lib/rustyrig/modules/rrserver ${INSTALL_DIR}/lib/rustyrig/modules/rrclient
	for m in ${bins}; do case $$m in *rrserver*.so) install -Dm755 $$m ${INSTALL_DIR}/lib/rustyrig/modules/rrserver/$$(basename $$m);; *rrclient*.so) install -Dm755 $$m ${INSTALL_DIR}/lib/rustyrig/modules/rrclient/$$(basename $$m);; esac; done
	# Shared libs (librustyaxe.so, librrprotocol.so, libfwdspmgr.so) all build
	# at the top of the tree and land in ${libs}; install them so the binaries
	# can find them (see -Wl,-rpath in mk/compile.mk)
	cp -av ${libs} ${INSTALL_DIR}/lib
#	cp -av archive-config.sh *-rigctld.sh killall.sh rrgtk.sh test-run.sh ${INSTALL_DIR}/bin
#	cp -aiv config/${PROFILE}.*.json config/client.config.json ${INSTALL_DIR}/etc

# Install the native icon, desktop entry, and WebUI bundle (including the
# browser notification sounds). WEB_ROOT matches the packaged server config;
# the HTTP server still falls back to ./www for source-tree runs.
install-assets:
	install -Dm644 res/rustyrig.png "${INSTALL_DIR}/share/icons/hicolor/48x48/apps/rustyrig.png"
	install -Dm644 res/rrclient.desktop "${INSTALL_DIR}/share/applications/rustyrig-client.desktop"
	install -d "${WEB_ROOT}"
	cp -a www/. "${WEB_ROOT}/"

install-tools:
	@for src in ${LOG_TOOLS}; do \
		install -Dm755 "$$src" "${INSTALL_DIR}/bin/$$(basename "$$src")"; \
	done

# Install tracked runtime configurations without silently overwriting a
# user's existing file. Package managers provide equivalent conffile handling;
# this prompt provides the same protection for a source-tree `make install`.
install-config:
	mkdir -p "${CONF_DIR}"
	@for src in ${CONFIG_FILES}; do \
		dst="${CONF_DIR}/$$(basename "$$src")"; \
		if test -e "$$dst"; then \
			if cmp -s "$$src" "$$dst"; then \
				echo "unchanged: $$dst"; \
				continue; \
			fi; \
			printf 'Replace %s? [y/N] ' "$$dst"; \
			if ! read answer; then answer=n; fi; \
			case "$$answer" in \
				y|Y|yes|YES) ;; \
				*) echo "keeping: $$dst"; continue ;; \
			esac; \
		fi; \
		install -Dm644 "$$src" "$$dst"; \
	done
