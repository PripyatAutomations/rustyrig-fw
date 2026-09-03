rrclient := bin/rrclient
bins += ${rrclient}

rrclient_objs += audio.o
rrclient_objs += chat.whois.o
rrclient_objs += cfg.network.o
rrclient_objs += cmd.o
rrclient_objs += cmd.admin.o		# Server control tasks
rrclient_objs += cmd.chat.o		# chat commands
rrclient_objs += cmd.help.o		# help texts
rrclient_objs += cmd.misc.o		# unsorted commands
rrclient_objs += cmd.tabs.o		# tab/window switching
rrclient_objs += connman.o		# connection manager
rrclient_objs += defconfig.o		# default config values
rrclient_objs += events.o		# event handlers
ifeq (${USE_GTK},true)
rrclient_objs += gtk.core.o             # Support for a GTK user interface
rrclient_objs += gtk.admin.o		# Admin tab
rrclient_objs += gtk.alertdialog.o	# alert/error/warning dialogs
rrclient_objs += gtk.chat.o		# Chat related stuff
rrclient_objs += gtk.codecpicker.o	# codec picker widget
rrclient_objs += gtk.editcfg.o		# configuration tab
rrclient_objs += gtk.fm-mode.o		# FM mode dialog
rrclient_objs += gtk.freqentry.o	# Frequency Entry Widget
rrclient_objs += gtk.font.o		# Font stuff
rrclient_objs += gtk.hotkey.o		# Hotkey support
rrclient_objs += gtk.mode-box.o		# Modulation Mode / width box
ifeq (${USE_LIBNOTIFY},true)
rrclient_objs += gtk.notify.o		# Support for libnotify
notify_ldflags := $(shell pkg-config --libs libnotify)
endif
rrclient_objs += gtk.ptt-btn.o		# Push To Talk (PTT) button in GUI
rrclient_objs += gtk.txpower.o		# TX power box
rrclient_objs += gtk.serveredit.o	# Serve editor
rrclient_objs += gtk.serverpick.o       # server picker
rrclient_objs += gtk.syslog.o		# syslog tab
rrclient_objs += gtk.userlist.o		# GTK part of the userlist
rrclient_objs += gtk.vfo-box.o		# VFO box element
rrclient_objs += gtk.vol-box.o		# Volume widget
rrclient_objs += gtk.winmgr.o		# window management
endif

rrclient_objs += m_privmsg.o		# irc privmsg (NYI)
rrclient_objs += main.o			# main loop
rrclient_objs += userlist.o
rrclient_objs += ui.o			# User interface wrapper (TUI/GTK)
rrclient_objs += ui.bell.o		# Bell/sounds support for the UI
rrclient_objs += ui.colors.o		# User interface color handling
rrclient_objs += ui.speech.o		# Support for screener readers
rrclient_objs += win32.o		# support to run in windows

rrclient_real_objs := $(foreach x, ${rrclient_objs}, ${BUILD_DIR}/rrclient/${x})
extra_clean += ${rrclient_real_objs}
CFLAGS += -I./modsrc/ -I/usr/include/gstreamer-1.0/

${BUILD_DIR}/rrclient/%.o: rrclient/%.c ${BUILD_HEADERS} GNUmakefile rrclient/rules.mk ${librustyaxe_headers} ${librrprotocol_headers} ${BUILD_DIR}/build_config.h $(wildcard rrclient/*.h)
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 2

# as soon as we complete loadable modules, this must go away!
${BUILD_DIR}/rrclient/%.o: modsrc/mod.ui.gtk3/%.c ${BUILD_HEADERS} GNUmakefile rrclient/rules.mk ${BUILD_DIR}/build_config.h
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 2

bin/rrclient: ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${libmongoose} ${rrclient_real_objs}
	@echo "[link] $@ from $(words ${rrclient_real_objs}) objects"
	@${CC} ${LDFLAGS} ${LDFLAGS_RRCLI}-o $@ ${rrclient_real_objs} -lrustyaxe -lrrprotocol -lev ${gtk_ldflags} ${gst_ldflags} ${notify_ldflags} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@
