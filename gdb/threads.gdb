# Trace thread creation, including threads created by shared libraries.
set pagination off
set breakpoint pending on
set print thread-events off

break pthread_create
commands
  silent
  printf "\n=== pthread_create ===\n"
  bt 12
  continue
end
