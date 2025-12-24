rrclient := bin/rrclient

bins += ${rrclient}

rrclient_objs += chat.o
rrclient_objs += chat.cmd.o
rrclient_objs += connman.o
rrclient_objs += defconfig.o

# Eventually we will support a TUI as well as GTK...
ifeq (${USE_GTK},true)
rrclient_objs += gtk.core.o             # Support for a GTK user interface
rrclient_objs += gtk.admin.o		# Admin tab
rrclient_objs += gtk.alertdialog.o	# alert/error/warning dialogs
rrclient_objs += gtk.chat.o		# Chat related stuff
rrclient_objs += gtk.codecpicker.o	# codec picker widget
rrclient_objs += gtk.editcfg.o		# configuration tab
rrclient_objs += gtk.fm-mode.o		# FM mode dialog
rrclient_objs += gtk.freqentry.o	# Frequency Entry Widget
rrclient_objs += gtk.hotkey.o		# Hotkey support
rrclient_objs += gtk.mode-box.o		# Modulation Mode / width box
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

rrclient_objs += main.o			# main loop
rrclient_objs += userlist.o
rrclient_objs += ui.o			# User interface wrapper (TUI/GTK)
rrclient_objs += ui.help.o		# help texts
rrclient_objs += ui.speech.o		# Support for screener readers
rrclient_objs += win32.o		# support to run in windows

rrclient_real_objs := $(foreach x, ${rrclient_objs}, ${OBJ_DIR}/rrclient/${x})
extra_clean += ${rrclient_real_objs}

CFLAGS_RRCLIENT += -I./modsrc/

${OBJ_DIR}/rrclient/%.o: rrclient/%.c ${BUILD_HEADERS}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRCLIENT} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 2

# as soon as we complete loadable modules, this must go away!
${OBJ_DIR}/rrclient/%.o: modsrc/mod.ui.gtk3/%.c ${BUILD_HEADERS} GNUmakefile rrclient/rules.mk #${librustyaxe_headers} $[librrprotocol_headers}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRCLIENT} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 2

bin/rrclient: ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${libmongoose} ${rrclient_real_objs}
	${CC} ${LDFLAGS} ${LDFLAGS_RRCLIENT} -o $@ ${rrclient_real_objs} -lrustyaxe -lrrprotocol -lev ${gtk_ldflags} ${gst_ldflags} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@
