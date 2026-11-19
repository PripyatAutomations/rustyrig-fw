# Source this after cd to the repository root. Build only the transport adapter.
test "$(sh tools/probe-librustyirc.sh)" != none
test "$(sh tools/probe-librustyaxe.sh)" != none
test "$(sh tools/probe-librrprotocol.sh)" != none
axe_cflags=$(pkg-config --cflags librustyaxe 2>/dev/null || :)
proto_cflags=$(pkg-config --cflags librrprotocol 2>/dev/null || :)
irc_cflags=$(pkg-config --cflags librustyirc 2>/dev/null || :)
# Use full dependency metadata and keep the actual IRC library after the
# test adapter on the link line. --libs-only-L drops essential dependencies.
irc_libs="$(pkg-config --libs librustyirc 2>/dev/null || printf '%s' '-lrustyirc')"
axe_libs="$(pkg-config --libs librustyaxe 2>/dev/null || printf '%s' '-lrustyaxe')"
proto_libs="$(pkg-config --libs librrprotocol 2>/dev/null || printf '%s' '-lrrprotocol')"

# The public headers can be newer than the installed DSO. Verify symbols
# referenced by irc.compat.c before spawning dozens of noisy failing tests.
abi_check="$work/irc-abi-check.c"
cat > "$abi_check" <<'IRC_ABI'
#include <librustyirc/irc.h>
#include <librustyirc/protocol.h>
#include <librustyirc/connection.h>
int main(void) {
    volatile const void *symbols[] = {
        (const void *)&irc_parse_message,
        (const void *)&irc_message_free,
        (const void *)&rustyirc_capabilities_message,
        (const void *)&rustyirc_capabilities_poll,
        (const void *)&rustyirc_capabilities_reset,
        (const void *)&rustyirc_capabilities_shutdown,
        (const void *)&rustyirc_capability_enabled,
        (const void *)&rustyirc_connection_free,
        (const void *)&rustyirc_connection_new,
        (const void *)&rustyirc_connection_nick,
        (const void *)&rustyirc_connection_registered,
        (const void *)&rustyirc_connection_set_fd,
        (const void *)&rustyirc_connection_set_nick,
        (const void *)&rustyirc_connection_set_sasl,
        (const void *)&rustyirc_dcc_audio_decode,
        (const void *)&rustyirc_dcc_audio_encode,
        (const void *)&rustyirc_dcc_audio_message,
        (const void *)&rustyirc_dispatch_message,
        (const void *)&rustyirc_emit_message,
        (const void *)&rustyirc_line_limit,
        (const void *)&rustyirc_mode_has_argument,
        (const void *)&rustyirc_modes_symbol,
        (const void *)&rustyirc_prefix_mode,
        (const void *)&rustyirc_register_callback,
        (const void *)&rustyirc_register_default_callbacks,
        (const void *)&rustyirc_register_default_numeric_callbacks,
        (const void *)&rustyirc_remove_callback,
        (const void *)&rustyirc_send_dcc_audio,
        (const void *)&rustyirc_server_capab,
        (const void *)&rustyirc_server_capability,
        (const void *)&rustyirc_server_property,
        (const void *)&rustyirc_shutdown,
        (const void *)&rustyirc_supports_rustyrig
    };
    return symbols[0] == NULL;
}
IRC_ABI
if ! ${CC:-cc} $irc_cflags "$abi_check" -Wl,--no-as-needed $irc_libs $axe_libs -Wl,--as-needed -o "$work/irc-abi-check" >/dev/null 2>"$work/irc-abi.log"; then
    echo "ERROR: installed librustyirc lacks the API used by rustyrig-fw." >&2
    echo "Build/install librustyirc from the librustyaxe/librustyirc source tree," >&2
    echo "then rerun these tests (linker details follow):" >&2
    cat "$work/irc-abi.log" >&2
    return 1
fi
test_includes="-Iinc -I."
export LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Caller owns the temporary directory through its existing work-directory trap.
irc_adapter_dir="$work/irc-adapter"
mkdir -p "$irc_adapter_dir"
# Shared logger/event headers and adapter symbols now require GLib.
irc_glib_cflags=$(pkg-config --cflags glib-2.0)
irc_glib_libs=$(pkg-config --libs glib-2.0)
for source in irc irc.compat cli.irc irc.dcc irc.audio.session; do
   ${CC:-cc} $axe_cflags $proto_cflags $irc_cflags $irc_glib_cflags -fPIC -ffunction-sections -fdata-sections $test_includes -Ibuild/${PROFILE:-radio} -c "rrclient/$source.c" -o "$irc_adapter_dir/$source.o"
done
${CC:-cc} $proto_cflags $irc_glib_cflags -fPIC $test_includes -Ibuild/${PROFILE:-radio} -c rrclient/tests/irc_adapter_stubs.c -o "$irc_adapter_dir/stubs.o"
${CC:-cc} -shared -o "$irc_adapter_dir/librrirc-test.so" "$irc_adapter_dir"/*.o \
  -Wl,--no-as-needed $irc_libs $proto_libs $axe_libs -Wl,--as-needed $irc_glib_libs
export LDFLAGS="-L$irc_adapter_dir -Wl,-rpath,$irc_adapter_dir -Wl,--no-as-needed -lrrirc-test $irc_libs $proto_libs $axe_libs -Wl,--as-needed ${LDFLAGS:-} $irc_glib_libs"
