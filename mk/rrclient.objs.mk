objs += audio.o			# gstreamer based audio
objs += auth.o
objs += chat.o
objs += chat.cmd.o
objs += codecneg.o
objs += config.o
objs += debug.o			# Debug stuff
objs += defconfig.o
objs += dict.o			# dictionary object

# Eventually we will support a TUI as well as GTK...
ifeq (${USE_GTK},true)
objs += gtk.core.o              # Support for a GTK user interface
objs += gtk.admin.o		# Admin tab
objs += gtk.codecpicker.o	# codec picker widget
objs += gtk.editcfg.o		# configuration tab
objs += gtk.fm-mode.o		# FM mode dialog
objs += gtk.freqentry.o		# Frequency Entry Widget
objs += gtk.mode-box.o		# Modulation Mode / width box
objs += gtk.ptt-btn.o		# Push To Talk (PTT) button in GUI
objs += gtk.txpower.o		# TX power box
objs += gtk.serverpick.o        # server picker
objs += gtk.syslog.o		# syslog tab
objs += gtk.vfo-box.o		# VFO box element
objs += gtk.vol-box.o		# Volume widget
objs += gtk.winmgr.o		# window management
endif

objs += logger.o		# Logging facilities
objs += main.o			# main loop
objs += mongoose.o		# Mongoose http/websocket/mqtt library
objs += posix.o			# support for POSIX hosts (linux or perhaps others)
objs += userlist.o
objs += ui.o			# User interface wrapper (TUI/GTK)
objs += ui.help.o		# help texts
objs += ui.speech.o		# Support for screener readers
objs += util.file.o		# Misc file functions
objs += util.math.o		# Misc math functions
objs += util.string.o		# String utility functions
objs += win32.o			# support to run in windows
objs += ws.o			# Websocket transport general
objs += ws.alert.o		# Audio over websocket negotiation
objs += ws.audio.o		# Audio over websocket negotiation
objs += ws.auth.o		# protocol authentication
objs += ws.chat.o		# shared chatroom
objs += ws.error.o		# Error notices
objs += ws.file-xfer.o		# file transfer
objs += ws.media.o		# handle ping messages
objs += ws.notice.o		# Notices
objs += ws.ping.o		# handle ping messages
objs += ws.rigctl.o		# rig controls
objs += ws.syslog.o
objs += ws.tx-audio.o		# support for SENDING audio to the server
