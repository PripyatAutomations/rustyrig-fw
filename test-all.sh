#!/bin/bash

SESSION="rustyrig"

if command -v tmux >/dev/null 2>&1; then
   if ! tmux has-session -t "$SESSION" 2>/dev/null; then
      tmux new-session -d -s "$SESSION" './test-server.sh'
      tmux new-window -t "$SESSION" "sleep 20; ./test-client.sh"
      tmux attach -t "$SESSION"
   else
      echo "Tmux session '$SESSION' already exists."
      tmux attach -t "$SESSION"
   fi
elif command -v screen >/dev/null 2>&1; then
   if ! screen -list | grep -q "$SESSION"; then
      screen -S "$SESSION" -d -m bash -c './test-server.sh'
      screen -S "$SESSION" -X screen bash -c 'sleep 20; ./test-client.sh'
      screen -r "$SESSION"
   else
      echo "Screen session '$SESSION' already exists."
      screen -r "$SESSION"
   fi
else
   echo "Neither tmux nor screen found."
   exit 1
fi
