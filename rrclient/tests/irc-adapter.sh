# Source this after cd to the repository root. Build only the transport adapter.
irc_mode=$(sh tools/probe-librustyirc.sh)
if [ "$irc_mode" = none ]; then test -f librustyirc.so; fi
axe_mode=$(sh tools/probe-librustyaxe.sh)
proto_mode=$(sh tools/probe-librrprotocol.sh)
axe_cflags=$(pkg-config --cflags librustyaxe 2>/dev/null || :)
proto_cflags=$(pkg-config --cflags librrprotocol 2>/dev/null || :)
irc_cflags=$(pkg-config --cflags librustyirc 2>/dev/null || :)
irc_libs="$(pkg-config --libs-only-L librustyirc 2>/dev/null || :) -lrustyirc"
axe_libs="$(pkg-config --libs-only-L librustyaxe 2>/dev/null || :) -lrustyaxe"
proto_libs="$(pkg-config --libs-only-L librrprotocol 2>/dev/null || :) -lrrprotocol"
test_includes="-idirafter . -idirafter inc"
if [ "$axe_mode" = none ]; then
   test -f librustyaxe.so
   axe_libs="-L. -lrustyaxe"
   test_includes="-I. -Iinc"
fi
if [ "$proto_mode" = none ]; then
   test -f librrprotocol.so
   proto_libs="-L. -lrrprotocol"
   test_includes="-I. -Iinc"
fi
if [ "$irc_mode" = none ]; then irc_libs="-L. -lrustyirc"; fi
export LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Caller owns the temporary directory through its existing work-directory trap.
irc_adapter_dir="$work/irc-adapter"
mkdir -p "$irc_adapter_dir"
# Shared logger/event headers and adapter symbols now require GLib.
irc_glib_cflags=$(pkg-config --cflags glib-2.0)
irc_glib_libs=$(pkg-config --libs glib-2.0)
for source in irc irc.compat cli.irc irc.dcc irc.audio.session srv.irc.core; do
   ${CC:-cc} $axe_cflags $proto_cflags $irc_cflags $irc_glib_cflags -fPIC -ffunction-sections -fdata-sections $test_includes -Ibuild/${PROFILE:-radio} -c "rrclient/$source.c" -o "$irc_adapter_dir/$source.o"
done
${CC:-cc} $proto_cflags $irc_glib_cflags -fPIC $test_includes -Ibuild/${PROFILE:-radio} -c rrclient/tests/irc_adapter_stubs.c -o "$irc_adapter_dir/stubs.o"
${CC:-cc} -shared -o "$irc_adapter_dir/librrirc-test.so" "$irc_adapter_dir"/*.o $irc_libs $proto_libs $axe_libs
export LDFLAGS="-L$irc_adapter_dir -Wl,-rpath,$irc_adapter_dir -lrrirc-test $irc_libs $proto_libs $axe_libs ${LDFLAGS:-} $irc_glib_libs"
