rrgtk := bin/rrgtk

bins += ${rrgtk}

rrgtk_objs += audio.o
rrgtk_objs += chat.o
rrgtk_objs += chat.cmd.o
rrgtk_objs += connman.o
rrgtk_objs += defconfig.o
rrgtk_objs += events.o

# Eventually we will support a TUI as well as GTK...
ifeq (${USE_GTK},true)
rrgtk_objs += gtk.core.o             # Support for a GTK user interface
rrgtk_objs += gtk.admin.o		# Admin tab
rrgtk_objs += gtk.alertdialog.o	# alert/error/warning dialogs
rrgtk_objs += gtk.chat.o		# Chat related stuff
rrgtk_objs += gtk.codecpicker.o	# codec picker widget
rrgtk_objs += gtk.editcfg.o		# configuration tab
rrgtk_objs += gtk.fm-mode.o		# FM mode dialog
rrgtk_objs += gtk.freqentry.o	# Frequency Entry Widget
rrgtk_objs += gtk.hotkey.o		# Hotkey support
rrgtk_objs += gtk.mode-box.o		# Modulation Mode / width box
rrgtk_objs += gtk.ptt-btn.o		# Push To Talk (PTT) button in GUI
rrgtk_objs += gtk.txpower.o		# TX power box
rrgtk_objs += gtk.serveredit.o	# Serve editor
rrgtk_objs += gtk.serverpick.o       # server picker
rrgtk_objs += gtk.syslog.o		# syslog tab
rrgtk_objs += gtk.userlist.o		# GTK part of the userlist
rrgtk_objs += gtk.vfo-box.o		# VFO box element
rrgtk_objs += gtk.vol-box.o		# Volume widget
rrgtk_objs += gtk.winmgr.o		# window management
endif

rrgtk_objs += main.o			# main loop
rrgtk_objs += userlist.o
rrgtk_objs += ui.o			# User interface wrapper (TUI/GTK)
rrgtk_objs += ui.help.o		# help texts
rrgtk_objs += ui.speech.o		# Support for screener readers
rrgtk_objs += win32.o		# support to run in windows

rrgtk_real_objs := $(foreach x, ${rrgtk_objs}, ${OBJ_DIR}/rrgtk/${x})
extra_clean += ${rrgtk_real_objs}

CFLAGS += -I./modsrc/ -I/usr/include/gstreamer-1.0/

${OBJ_DIR}/rrgtk/%.o: rrgtk/%.c ${BUILD_HEADERS} GNUmakefile rrgtk/rules.mk ${librustyaxe_headers} ${librrprotocol_headers}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 2

# as soon as we complete loadable modules, this must go away!
${OBJ_DIR}/rrgtk/%.o: modsrc/mod.ui.gtk3/%.c ${BUILD_HEADERS} GNUmakefile rrgtk/rules.mk #${librustyaxe_headers} $[librrprotocol_headers}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRCLI} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 2

bin/rrgtk: ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${libmongoose} ${rrgtk_real_objs}
	${CC} ${LDFLAGS} ${LDFLAGS_RRCLI} -o $@ ${rrgtk_real_objs} -lrustyaxe -lrrprotocol -lev ${gtk_ldflags} ${gst_ldflags} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@
