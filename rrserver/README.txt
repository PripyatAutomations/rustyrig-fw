Sources
-------
    acl.c
    amp.c				Support for amplifier modules (NYI)
    atu.c				Support for antenna matching (ATU) units
    au.c
    audit.c
    au.pcm5102.c
    au.pipe.c
    au.recording.c
    backend.c				Wrapper for various rig backends
    backend.hamlib.c			Backend using hamlib
    backend.internal.c			Backend for real radios built on rrfw
    backend.instance.c
    backend.internal-parts.c
    backend.register.c
    backend.worker.c
    backend.yaesu.c
    bridge.access.c
    bridge.hints.c
    bridge.manager.c
    cfg.acl.c
    cfg.files.c
    cfg.gpio.c
    cfg.peer.c
    cfg.radio.c
    cfg.rig.c
    cfg.rtp.c
    cfg.si5351.c
    channels.c				Channel memory support
    console.c				Support for a control console using TUI (NYI)
    database.c				Support for sqlite3 database
    database.accounting.c
    database.privileges.c
    database.rooms.c
    database.users.c
    defconfig.c				Default configuration values
    events.c				Event handlers
    events.admin.c
    events.quota.c
    events.rig.c
    events.rooms.c
    events.users.c
    faults.c				Fault handling
    files.c
    filters.c				Support for LPF/BPF/HPF filters
    gps.c
    gpio.c				Support for GPIO on various platforms 
    help.c				Help system
    hardware.c
    hardware.dummy.c
    hardware.si5351.c
    hostlog.c
    http.bans.c				HTTP ban handling
    i2c.c				i2c support
    i2c.esp32.c				i2c support on esp32
    i2c.linux.c				i2c support on linux
    i2c.mux.c				i2c multiplexor
    i2c.stm32.c				i2c stm32
    identity.c
    link.admin.c
    link.c
    link.mesh.c
    link.native.c
    link.tree.c
    main.c				Main loop
    media.c
    media.direct.c
    mesh.auth.c
    mesh.bootstrap.c
    mesh.enroll.c
    mesh.enroll.native.c
    mesh.native.client.c
    mesh.native.listener.c
    mesh.wss.c
    mesh.wss.http.c
    module.gpsd.c
    module.gps-nmea.c
    module.hamlib.c
    mqtt.c				MQTT stuff
    network.c				Network configuration for embedded
    objects.c
    performance.c
    platform.backlight.c
    platform.controls.c
    protection.c			Protection mechanisms
    ptt.c				Push To Talk management
    reactor.glib.c
    receiver.directory.c
    relay.c
    relay.catalog.c
    relay.media.c
    relay.sessions.c
    relay.users.c
    rig.compat.c
    rig.properties.c
    rig.registry.c
    rig.rooms.c
    rx.slice.c
    serial.c
    si5351.c
    si5351-tool.c
    supervision.c
    thermal.c				Thermal management
    timer.c				Timer management
    timer.clocktick.c			Periodic (1hz) timer
    unwind.c				Stack unwinding support
    usage.c
    waterfall.c
    webcam.c				Webcam support
