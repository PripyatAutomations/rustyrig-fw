
Source Layout
-------------
    audio.c				Audio core stuff (NYI)
    cfg.network.c			Config handler: [network]
    chat.whois.c			Chat WHOIS related stuff
    cmd.admin.c				Admin commands
    cmd.c				Command parser
    cmd.chat.c				Chat (!commands)
    cmd.help.c				/help and !help
    cmd.misc.c				Misc commands like (dis)connect
    cmd.tabs.c				Tab/window switching
    connman.c				Connection manager stuff specific to client
    defconfig.c				Default configuration values
    events.c				Event handlers
    gtk.admin.c				GTK UI: Admin tab
    gtk.alertdialog.c			GTK UI: Alert dialogs
    gtk.chat.c				GTK UI: Chat view widget
    gtk.codecpicker.c			GTK UI: Codec selector widget
    gtk.core.c				GTK UI: Core stuff
    gtk.editcfg.c			GTK UI: Configuration (text) editor
    gtk.fm-mode.c			GKT UI: FM mode controls dialog
    gtk.font.c				GTK UI: Font handling
    gtk.freqentry.c			GTK UI: Frequency entry widget
    gtk.hotkey.c			GTK UI: Hotkey handling
    gtk.mode-box.c			GTK UI: VFO Mode box widget
    gtk.notify.c			GTK UI: libnotify support
    gtk.ptt-btn.c			GTK UI: PTT button with state feedback
    gtk.serveredit.c			GTK UI: Server editor (disabled)
    gtk.serverpick.c			GTK UI: Server selector (disabled)
    gtk.syslog.c			GTK UI: Syslog tab
    gtk.txpower.c			GTK UI: TX power widget
    gtk.userlist.c			GTK UI: Userlist window
    gtk.vfo-box.c			GTK UI: VFO box widget (with all controls)
    gtk.vol-box.c			GTK UI: VFO Volume box widget
    gtk.winmgr.c			GTK UI: Window management
    main.c				Main loop and timers
    m_privmsg.c				IRC privmsg
    ui.bell.c				Bell support for UI (GTK and TUI)
    ui.c				User interface wrapper
    ui.colors.c				User interface color handling
    ui.speech.c				User interface speech
    userlist.c				Userlist stuff (common + TUI)
    win32.c				Windows support


You may notice there's not much TUI code here, that's because it belongs to
librustyaxe. The TUI interface is designed to be reusable, whereas the GTK3
interface is designed to have components that could later be reused such as
gtk-freqentry widget.
