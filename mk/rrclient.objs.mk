rrclient_objs += rrclient/audio.o			# gstreamer based audio
rrclient_objs += rrclient/auth.o
rrclient_objs += rrclient/chat.o
rrclient_objs += rrclient/chat.cmd.o
rrclient_objs += rrclient/connman.o
rrclient_objs += rrclient/defconfig.o

# Eventually we will support a TUI as well as GTK...
ifeq (${USE_GTK},true)
rrclient_objs += rrclient/gtk.core.o              # Support for a GTK user interface
rrclient_objs += rrclient/gtk.admin.o		# Admin tab
rrclient_objs += rrclient/gtk.chat.o		# Chat related stuff
rrclient_objs += rrclient/gtk.codecpicker.o	# codec picker widget
rrclient_objs += rrclient/gtk.editcfg.o		# configuration tab
rrclient_objs += rrclient/gtk.fm-mode.o		# FM mode dialog
rrclient_objs += rrclient/gtk.freqentry.o		# Frequency Entry Widget
rrclient_objs += rrclient/gtk.hotkey.o		# Hotkey support
rrclient_objs += rrclient/gtk.mode-box.o		# Modulation Mode / width box
rrclient_objs += rrclient/gtk.ptt-btn.o		# Push To Talk (PTT) button in GUI
rrclient_objs += rrclient/gtk.txpower.o		# TX power box
rrclient_objs += rrclient/gtk.serverpick.o        # server picker
rrclient_objs += rrclient/gtk.syslog.o		# syslog tab
rrclient_objs += rrclient/gtk.vfo-box.o		# VFO box element
rrclient_objs += rrclient/gtk.userlist.o		# GTK part of the userlist
rrclient_objs += rrclient/gtk.vol-box.o		# Volume widget
rrclient_objs += rrclient/gtk.winmgr.o		# window management
endif

rrclient_objs += rrclient/main.o			# main loop
rrclient_objs += rrclient/userlist.o
rrclient_objs += rrclient/ui.o			# User interface wrapper (TUI/GTK)
rrclient_objs += rrclient/ui.help.o		# help texts
rrclient_objs += rrclient/ui.speech.o		# Support for screener readers
rrclient_objs += rrclient/win32.o			# support to run in windows
rrclient_objs += rrclient/ws.o			# Websocket transport general
rrclient_objs += rrclient/ws.alert.o		# Audio over websocket negotiation
rrclient_objs += rrclient/ws.audio.o		# Audio over websocket negotiation
rrclient_objs += rrclient/ws.auth.o		# protocol authentication
rrclient_objs += rrclient/ws.chat.o		# shared chatroom
rrclient_objs += rrclient/ws.error.o		# Error notices
rrclient_objs += rrclient/ws.file-xfer.o		# file transfer
rrclient_objs += rrclient/ws.media.o		# handle audio/video media transfer
rrclient_objs += rrclient/ws.notice.o		# Notices
rrclient_objs += rrclient/ws.ping.o		# handle ping messages
rrclient_objs += rrclient/ws.rigctl.o		# rig controls
rrclient_objs += rrclient/ws.syslog.o
