# Stop at common fatal signals; retain the program for inspection.
set pagination off
set logging file /tmp/rr-gdb-crashes.log
set logging overwrite on
set logging enabled on
set breakpoint pending on
set print thread-events off
set print pretty on

catch signal SIGSEGV SIGABRT SIGBUS
commands
  silent
  printf "\n=== fatal signal ===\n"
  bt full
  thread apply all bt 12
end
