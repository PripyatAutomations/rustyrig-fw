#!/bin/bash
dbus-launch --sh-syntax --exit-with-session
pipewire &
pipewire-pulse & 
pw-cli list-objects
