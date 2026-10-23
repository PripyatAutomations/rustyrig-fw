# Source this after cd to the repository root. Build only the transport adapter.
test -f librustyirc.so && test -f librustyaxe.so && test -f librrprotocol.so
export LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Caller owns the temporary directory through its existing work-directory trap.
irc_adapter_dir="$work/irc-adapter"
mkdir -p "$irc_adapter_dir"
for source in irc irc.compat cli.irc irc.dcc irc.audio.session srv.irc.core; do
   ${CC:-cc} -fPIC -ffunction-sections -fdata-sections -I. -Iinc -Ibuild/${PROFILE:-radio} -c "rrclient/$source.c" -o "$irc_adapter_dir/$source.o"
done
${CC:-cc} -shared -o "$irc_adapter_dir/librrirc-test.so" "$irc_adapter_dir"/*.o -L. -lrustyirc -lrrprotocol -lrustyaxe
export LDFLAGS="-L$irc_adapter_dir -Wl,-rpath,$irc_adapter_dir -lrrirc-test -lrustyirc -lrrprotocol -lrustyaxe ${LDFLAGS:-}"
