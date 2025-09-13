rrclient_objs += audio.o			# gstreamer based audio
rrclient_objs += auth.o
rrclient_objs += chat.o
rrclient_objs += chat.cmd.o
rrclient_objs += connman.o
rrclient_objs += defconfig.o

# Eventually we will support a TUI as well as GTK...
ifeq (${USE_GTK},true)
rrclient_objs += gtk.core.o              # Support for a GTK user interface
rrclient_objs += gtk.admin.o		# Admin tab
rrclient_objs += gtk.chat.o		# Chat related stuff
rrclient_objs += gtk.codecpicker.o	# codec picker widget
rrclient_objs += gtk.editcfg.o		# configuration tab
rrclient_objs += gtk.fm-mode.o		# FM mode dialog
rrclient_objs += gtk.freqentry.o		# Frequency Entry Widget
rrclient_objs += gtk.hotkey.o		# Hotkey support
rrclient_objs += gtk.mode-box.o		# Modulation Mode / width box
rrclient_objs += gtk.ptt-btn.o		# Push To Talk (PTT) button in GUI
rrclient_objs += gtk.txpower.o		# TX power box
rrclient_objs += gtk.serverpick.o        # server picker
rrclient_objs += gtk.syslog.o		# syslog tab
rrclient_objs += gtk.vfo-box.o		# VFO box element
rrclient_objs += gtk.userlist.o		# GTK part of the userlist
rrclient_objs += gtk.vol-box.o		# Volume widget
rrclient_objs += gtk.winmgr.o		# window management
endif

rrclient_objs += main.o			# main loop
rrclient_objs += userlist.o
rrclient_objs += ui.o			# User interface wrapper (TUI/GTK)
rrclient_objs += ui.help.o		# help texts
rrclient_objs += ui.speech.o		# Support for screener readers
rrclient_objs += win32.o			# support to run in windows
rrclient_objs += ws.o			# Websocket transport general
rrclient_objs += ws.alert.o		# Audio over websocket negotiation
rrclient_objs += ws.audio.o		# Audio over websocket negotiation
rrclient_objs += ws.auth.o		# protocol authentication
rrclient_objs += ws.chat.o		# shared chatroom
rrclient_objs += ws.error.o		# Error notices
rrclient_objs += ws.file-xfer.o		# file transfer
rrclient_objs += ws.media.o		# handle audio/video media transfer
rrclient_objs += ws.notice.o		# Notices
rrclient_objs += ws.ping.o		# handle ping messages
rrclient_objs += ws.rigctl.o		# rig controls
rrclient_objs += ws.syslog.o
