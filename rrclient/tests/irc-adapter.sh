# Source this after cd to the repository root. Build only the transport adapter.
test -f librustyirc.so && test -f librrprotocol.so
if pkg-config --exists librustyaxe; then
   axe_cflags=$(pkg-config --cflags librustyaxe)
   axe_libs=$(pkg-config --libs librustyaxe)
else
   test -f librustyaxe.so
   axe_cflags="-I."
   axe_libs="-L. -lrustyaxe"
fi
export LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Caller owns the temporary directory through its existing work-directory trap.
irc_adapter_dir="$work/irc-adapter"
mkdir -p "$irc_adapter_dir"
# Shared logger/event headers and adapter symbols now require GLib.
irc_glib_cflags=$(pkg-config --cflags glib-2.0)
irc_glib_libs=$(pkg-config --libs glib-2.0)
for source in irc irc.compat cli.irc irc.dcc irc.audio.session srv.irc.core; do
   ${CC:-cc} $axe_cflags $irc_glib_cflags -fPIC -ffunction-sections -fdata-sections -I. -Iinc -Ibuild/${PROFILE:-radio} -c "rrclient/$source.c" -o "$irc_adapter_dir/$source.o"
done
${CC:-cc} $irc_glib_cflags -fPIC -I. -Iinc -Ibuild/${PROFILE:-radio} -c rrclient/tests/irc_adapter_stubs.c -o "$irc_adapter_dir/stubs.o"
${CC:-cc} -shared -o "$irc_adapter_dir/librrirc-test.so" "$irc_adapter_dir"/*.o -L. -lrustyirc -lrrprotocol $axe_libs
export LDFLAGS="-L$irc_adapter_dir -Wl,-rpath,$irc_adapter_dir -lrrirc-test -lrustyirc -lrrprotocol $axe_libs ${LDFLAGS:-} $irc_glib_libs"
