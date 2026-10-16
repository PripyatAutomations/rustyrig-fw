# Internal radio hardware

The attached-hardware registry is owned by rrserver (`rig.hardware`). It is a
foundation for homebrew radios assembled from named devices: oscillators,
filter banks, relays, audio endpoints, sensors and local controls. The existing
internal backend still provides its previous in-memory VFO behavior. It does
not yet tune an oscillator or claim a GPIO line.

Radio signal processing will run in a **separate `sbitxdsp` process**, with its
own lifecycle and IPC contract. It does not belong in `fwdsp`. Existing media
transport can consume/produce audio at that boundary. The DSP process and sBitx board controller are not implemented yet.
[Si5351 support](si5351.md) provides the first oscillator driver and a bring-up tool.

## Registry ownership

`hardware.h` is private to rrserver. Each entry has a unique ID, kind, driver,
path, optional parent, exclusive owner and last claim/release error. Metadata
is copied; driver-private data transfers to the registry only after successful
registration. Parent devices must be registered first. There is no fixed chip
or device count limit.

Create/register/claim/release operations run on the server thread. Once claimed,
a device belongs to its owning backend worker; do not operate it from another
worker or the main loop. Stop that worker before releasing or destroying its
resources. GPIO calls must never be performed directly from network callbacks.
An unsuccessful release retains the owner and error, preventing reassignment.
Shutdown frees hardware after backend workers, in reverse registration order.
Driver callback tables must outlive their entries.

Driver callbacks and registry APIs return true for success. GPIO reads return
0/1 for logical inactive/active, or -1 on failure. Errors preserve errno.
Devices without claim callbacks are inventory-only. Registry entries are not
remote controls: future exposure must use the existing object/event machinery
and normal authorization. No new wire semantics or client UI are introduced.

## Linux GPIO

Enable `features.gpio` in the selected `config/$PROFILE.config.json`. Building
this adapter requires libgpiod **2.0 or newer** development files. Disabled GPIO
builds do not link libgpiod. An explicitly configured mapping in a disabled
build fails initialization rather than silently pretending it is available.
The current Debian build dependencies still include libgpiod-dev; splitting
hardware drivers into optional packages is subsequent work.

Discovery examines every `/dev/gpiochip*`, including kernel-supported GPIO
expanders, and reports chip labels and line counts in the server log. Permission
failures are logged. GPIO numbering is chip-relative: neither header pin numbers
nor WiringPi numbers are accepted as a separate numbering scheme. I2C and SPI
peripherals require their respective bus adapters; libgpiod addresses GPIO
controllers, including GPIO controllers connected over those buses.

Configure named mappings in the server configuration, for example:

```ini
[general]
hardware.gpio-lines=ptt paddle

; Illustrative offsets only -- replace with the board's actual wiring.
[gpio:ptt]
chip=/dev/gpiochip0
offset=17
direction=output
active-low=false
safe-value=0

[gpio:paddle]
chip=/dev/gpiochip0
offset=22
direction=input
active-low=true
```

Mapping fields are registered/documented in `defconfig.c`; named sections use
an extensible config callback. `chip` and `offset` are required. Direction
otherwise defaults to input, active-low to false, and safe-value to logical 0.
Unknown fields, malformed values, missing chips, invalid offsets and duplicate
physical mappings fail initialization. Paths are canonicalized so symlink
aliases cannot bypass duplicate mapping detection. Changes require restart.

Discovery and mapping validation never request lines or change their direction.
A future board controller finds `gpio:ptt`, calls `rr_hardware_claim()` with its
rig owner ID, then accesses it through `rr_gpio_read()`/`rr_gpio_write()` on that
worker. Claiming requests the line exclusively through libgpiod, applying the
initial safe value as part of the request. Other applications' claims are
rejected by the kernel. Release restores that same logical safe value before
releasing the request. Physical polarity is handled by libgpiod.

Safe-value applies while the request is held; software cannot promise an output
state after release, process crash or loss of power. Board-level pulls and PA
inhibit circuitry must establish the appropriate unowned state. Bias, drive
mode, edge/debounce configuration and hotplug refresh are not implemented yet.

## Next integration steps

1. Extend the initial Si5351A driver and I2C adapter to further oscillator
   families and capabilities. Each physical chip has one owner; outputs share
   its PLL resources.
2. Bind named devices to internal rig instances. Implement board-specific
   frequency plans and sequencing above individual drivers. Device status must
   distinguish configured, successfully controlled, and faulted hardware.
3. Implement `sbitxdsp` and its explicit audio/control boundary. Keep hardware
   sample rates, channels and IF routing separate from network audio formats.
4. Bring up an sBitx v3 board profile in receive mode, then add TX sequencing
   with filter selection, audio readiness, PA control and fault handling.

The existing per-rig backend workers remain the control execution model; see
[backend-workers.md](backend-workers.md). A future shared I2C adapter must
serialize transactions across its devices, even when different rigs own them.

## Validation

`bash tests/with-xvfb.sh bash rrserver/tests/test_hardware.sh` checks leases,
failed claim/release behavior, teardown and unavailable GPIO devices with both
adapter variants when libgpiod v2 is installed. The root test runner discovers
this component wrapper automatically. These checks do not drive physical pins.
Pi hardware testing is still required before connecting a transmitting board.
