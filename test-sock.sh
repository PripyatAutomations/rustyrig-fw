#!/bin/bash
rm -f /tmp/fwdsp-rx.sock /tmp/fwdsp-tx.sock

# Start two socat listeners in background to create the sockets:
socat -d -d UNIX-LISTEN:/tmp/fwdsp-rx.sock,reuseaddr,fork EXEC:"cat" &
socat -d -d UNIX-LISTEN:/tmp/fwdsp-tx.sock,reuseaddr,fork EXEC:"cat"
