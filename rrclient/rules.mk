rrclient_headers += $(wildcard rrclient/*.h)
rrclient_src = $(rrclient_objs:.o=.c)
gtk_headers := $(wildcard rrclient/gtk/*.h)

rrclient := bin/rrclient
rrclient_objs += objects.o objects.events.o
bins += ${rrclient}

rrclient_objs += audio.o
rrclient_objs += cat.o
rrclient_objs += cat.kpa500.o
rrclient_objs += serial.o sercom.o
rrclient_objs += cat.pty.o		# PTY CAT interface (./dev/ttyCAT0)
rrclient_objs += cat.yaesu.o
rrclient_objs += chat.whois.o
rrclient_objs += cfg.network.o
rrclient_objs += cmd.completion.o
rrclient_objs += cmd.o
rrclient_objs += cmd.admin.o		# Server control tasks
rrclient_objs += cmd.chat.o		# chat commands
rrclient_objs += cmd.help.o		# help texts
rrclient_objs += cmd.misc.o		# unsorted commands
rrclient_objs += cmd.tabs.o		# tab/window switching
rrclient_objs += connman.o		# connection manager
rrclient_objs += defconfig.o		# default config values
rrclient_objs += events.o		# event handlers
rrclient_objs += rrclient.o		# client connection state & core connect/disconnect
rrclient_objs += m_privmsg.o		# irc privmsg (NYI)
rrclient_objs += main.o			# main loop
rrclient_objs += media.o		# media channel subscribe handling
rrclient_objs += userlist.o
rrclient_objs += rooms.o		# joined room tracking
rrclient_objs += ui.statusbar.o
rrclient_objs += ui.o			# User interface wrapper (TUI/GTK)
rrclient_objs += ui.bell.o		# Bell/sounds support for the UI
rrclient_objs += ui.colors.o		# User interface color handling
rrclient_objs += frontend.o		# Frontend module host interface
rrclient_objs += vfo.o			# VFO management
rrclient_objs += webcam.o		# client-side v4l2 webcam video source
rrclient_objs += win32.o		# support to run in windows

#######
# GTK #
#######
# The GTK frontend is built as a dynamically loaded module (bin/rrgtk.so).
# Core rrclient never links GTK; the module carries the GTK dependency.
ifneq (${USE_GTK},false)
USE_GTK := ${USE_GTK}
gtk_module := bin/rrclient-gtk.so
bins += ${gtk_module}

gtk_module_objs += gtk/cfg.gtkcss.o	# GTK CSS from config file
gtk_module_objs += gtk/gtk.core.o             # Support for a GTK user interface
gtk_module_objs += gtk/gtk.admin.o	# Admin tab
gtk_module_objs += gtk/gtk.alertdialog.o	# alert/error/warning dialogs
gtk_module_objs += gtk/gtk.chat.o	# Chat related stuff
gtk_module_objs += gtk/gtk.codecpicker.o	# codec picker widget
gtk_module_objs += gtk/gtk.editcfg.o	# configuration tab
gtk_module_objs += gtk/gtk.fm-mode.o	# FM mode dialog
gtk_module_objs += gtk/gtk.freqentry.o	# Frequency Entry Widget
gtk_module_objs += gtk/gtk.hotkey.o	# Hotkey support
gtk_module_objs += gtk/gtk.mode-box.o	# Modulation Mode / width box
ifeq (${USE_LIBNOTIFY},true)
gtk_module_objs += gtk/gtk.notify.o	# Support for libnotify
notify_ldflags := $(shell pkg-config --libs libnotify)
endif
gtk_module_objs += gtk/gtk.ptt-btn.o	# Push To Talk (PTT) button in GUI
gtk_module_objs += gtk/gtk.txpower.o	# TX power box
gtk_module_objs += gtk/gtk.serveredit.o	# Serve editor
gtk_module_objs += gtk/gtk.serverpick.o	# server picker window
gtk_module_objs += gtk/gtk.syslog.o	# syslog tab
gtk_module_objs += gtk/gtk.userlist.o	# GTK part of the userlist
gtk_module_objs += gtk/gtk.vfo-box.o	# VFO box element
gtk_module_objs += gtk/gtk.vol-box.o	# Volume widget
gtk_module_objs += gtk/gtk.winmgr.o	# window management
gtk_module_objs += gtk/gtk.webcam.o	# webcam/video viewer window
gtk_module_objs += gtk/ui.speech.o	# Support for screener readers
gtk_module_objs += gtk/module.o	# Module entry points
endif

###########################################

rrclient_real_objs := $(foreach x, ${rrclient_objs}, ${BUILD_DIR}/rrclient/${x})
extra_clean += ${rrclient_real_objs}

gtk_module_real_objs := $(foreach x, ${gtk_module_objs}, ${BUILD_DIR}/rrclient/${x})
extra_clean += ${gtk_module_real_objs} ${gtk_module}

# GTK module objects need GTK cflags: compile with USE_GTK defined only for
# these files.
GTK_CFLAGS := ${GTK_MODULE_CFLAGS}
GTK_LDFLAGS := $(shell pkg-config --libs gtk+-3.0)

${BUILD_DIR}/rrclient/gtk/%.o: rrclient/gtk/%.c ${BUILD_HEADERS} GNUmakefile rrclient/rules.mk ${librustyaxe_headers} ${librrprotocol_headers} ${BUILD_DIR}/build_config.h $(wildcard rrclient/*.h) ${gtk_headers}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile-gtk] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} ${GTK_CFLAGS} -o $@ -c $< || exit 2

${BUILD_DIR}/rrclient/gtk/cfg.gtkcss.o: rrclient/gtk/cfg.gtkcss.c ${gtk_headers} ${BUILD_HEADERS} GNUmakefile rrclient/rules.mk ${BUILD_DIR}/build_config.h
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile-gtk] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} ${GTK_CFLAGS} -o $@ -c $< || exit 2

${BUILD_DIR}/rrclient/gtk/module.o: rrclient/gtk/module.c ${gtk_headers} ${BUILD_HEADERS} GNUmakefile rrclient/rules.mk ${BUILD_DIR}/build_config.h
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile-gtk] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} ${GTK_CFLAGS} -o $@ -c $< || exit 2

${BUILD_DIR}/rrclient/gtk/ui.speech.o: rrclient/gtk/ui.speech.c ${gtk_headers} ${BUILD_HEADERS} GNUmakefile rrclient/rules.mk ${BUILD_DIR}/build_config.h
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile-gtk] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} ${GTK_CFLAGS} -o $@ -c $< || exit 2

# The GTK frontend module. It resolves the frontend host API (and all core
# client API it uses) from the rrclient executable itself, which is linked
# with -rdynamic so the module's undefined symbols bind against the host at
# dlopen() time. Only GTK libraries are linked directly here.
${gtk_module}: ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${gtk_module_real_objs}
	@echo "[link] $@ from $(words ${gtk_module_real_objs}) objects"
	@${CC} ${LIB_LDFLAGS} -o $@ ${gtk_module_real_objs} ${GTK_LDFLAGS} ${notify_ldflags} || exit 2
	@ls -a1ls $@
	@file $@

${BUILD_DIR}/rrclient/%.o: rrclient/%.c ${BUILD_HEADERS} GNUmakefile rrclient/rules.mk ${librustyaxe_headers} ${librrprotocol_headers} ${BUILD_DIR}/build_config.h $(wildcard rrclient/*.h)
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 2

${BUILD_DIR}/rrclient/%.o: ${BUILD_HEADERS} GNUmakefile rrclient/rules.mk ${BUILD_DIR}/build_config.h
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 2

bin/rrclient: ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${libmongoose} ${rrclient_real_objs} ${libfwdspmgr}
	@echo "[link] $@ from $(words ${rrclient_real_objs}) objects"
	@${CC} ${LDFLAGS_RRCLI} -rdynamic -o $@ ${rrclient_real_objs} -lrustyaxe -lrrprotocol -Wl,--no-as-needed -lfwdspmgr -Wl,--as-needed ${LDFLAGS} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@
