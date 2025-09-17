#!/bin/bash
SESSION="rustyrig"

if command -v tmux >/dev/null 2>&1; then
   if ! tmux has-session -t "$SESSION" 2>/dev/null; then
      tmux new-session -d -s "$SESSION" './test-server.sh'   # window 0 for server
      tmux new-window -t "$SESSION" -n client                 # creates window 1, just a shell
      tmux send-keys -t "$SESSION:client" "echo 'Waiting 10s before starting client...'; sleep 10; ./test-client.sh" C-m
      tmux attach -t "$SESSION"
   else
      echo "Tmux session '$SESSION' already exists."
      tmux attach -t "$SESSION"
   fi
elif command -v screen >/dev/null 2>&1; then
   if ! screen -list | grep -q "$SESSION"; then
      # Window 0: server
      screen -S "$SESSION" -d -m bash -c './test-server.sh; exec bash'
      
      # Window 1: client (just opens a shell first)
      screen -S "$SESSION" -X screen bash
      # Send commands to the last created window
      screen -S "$SESSION" -p 1 -X stuff $'echo "Waiting 20s before starting client..."\nsleep 20\n./test-client.sh\n'
      
      # Attach
      screen -r "$SESSION"
   else
      echo "Screen session '$SESSION' already exists."
      screen -r "$SESSION"
   fi
else
   echo "Neither tmux nor screen found."
   exit 1
fi
