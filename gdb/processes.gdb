# Trace common child process creation paths.
set debuginfod enabled on
set pagination off
set logging file /tmp/rr-gdb-processes.log
set logging overwrite on
set logging enabled on
set breakpoint pending on
set print thread-events off
set follow-fork-mode parent
set detach-on-fork on

catch fork
commands
  silent
  printf "\n=== fork ===\n"
  bt 12
  continue
end

catch vfork
commands
  silent
  printf "\n=== vfork ===\n"
  bt 12
  continue
end

break posix_spawn
commands
  silent
  printf "\n=== posix_spawn ===\n"
  bt 12
  continue
end
