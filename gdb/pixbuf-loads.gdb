# Trace decoded image requests, including plugins loaded after startup.
set pagination off
set logging file /tmp/rr-gdb-pixbuf.log
set logging overwrite on
set logging enabled on
set breakpoint pending on
set print thread-events off

break gdk_pixbuf_loader_close
commands
  silent
  printf "\n=== gdk_pixbuf_loader_close ===\n"
  bt 16
  continue
end

break gdk_pixbuf_new_from_stream
commands
  silent
  printf "\n=== gdk_pixbuf_new_from_stream ===\n"
  bt 16
  continue
end
