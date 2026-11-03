# Trace traditional GLib log calls to their application origin.
# This is verbose; use only for short reproductions.
set pagination off
set breakpoint pending on
set print thread-events off

break g_logv
commands
  silent
  printf "\n=== g_logv ===\n"
  bt 12
  continue
end
