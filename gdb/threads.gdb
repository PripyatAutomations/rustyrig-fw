# Trace thread creation, including threads created by shared libraries.
set debuginfod enabled on
set pagination off
set logging file /tmp/rr-gdb-threads.log
set logging overwrite on
set logging enabled on
set breakpoint pending on
set print thread-events off

break pthread_create
commands
  silent
  printf "\n=== pthread_create ===\n"
  bt 12
  continue
end
