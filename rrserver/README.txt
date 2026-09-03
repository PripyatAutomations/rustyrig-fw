Sources
-------
    amp.c				Support for amplifier modules (NYI)
    atu.c				Support for antenna matching (ATU) units
    backend.c				Wrapper for various rig backends
    backend.dummy.c			Backend that does NOOP for everything
    backend.hamlib.c			Backend using hamlib
    backend.internal.c			Backend for real radios built on rrfw
    channels.c				Channel memory support
    console.c				Support for a control console using TUI (NYI)
    database.c				Support for sqlite3 database
    defconfig.c				Default configuration values
    events.c				Event handlers
    faults.c				Fault handling
    filters.c				Support for LPF/BPF/HPF filters
    gpio.c				Support for GPIO on various platforms 
    help.c				Help system
    http.bans.c				HTTP ban handling
    i2c.c				i2c support
    i2c.esp32.c				i2c support on esp32
    i2c.linux.c				i2c support on linux
    i2c.mux.c				i2c multiplexor
    i2c.stm32.c				i2c stm32
    main.c				Main loop
    mqtt.c				MQTT stuff
    network.c				Network configuration for embedded
    protection.c			Protection mechanisms
    ptt.c				Push To Talk management
    thermal.c				Thermal management
    timer.c				Timer management
    timer.clocktick.c			Periodic (1hz) timer
    unwind.c				Stack unwinding support
    webcam.c				Webcam support

