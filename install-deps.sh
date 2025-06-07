#!/bin/bash
set -e

[ -z "$PROFILE" ] && PROFILE=radio

apt install -y \
   libjson-perl libterm-readline-perl-perl libhash-merge-perl \
   libjson-xs-perl libstring-crc32-perl libgpiod-dev gpiod \
   jq pkg-config libmbedtls-dev libopus-dev

# Mojo::JSON::Pointer used by buildconf.pl
cpan install Mojo::JSON::Pointer

CONFIG="config/${PROFILE}.config.json"

USE_HAMLIB=$(jq -er '.backend.hamlib // empty' "$CONFIG")
USE_SQLITE=$(jq -er '.features.sqlite // empty' "$CONFIG")
USE_PIPEWIRE=$(jq -er '.features.pipewire // empty' "$CONFIG")
USE_OPUS=$(jq -er '.features.opus // empty' "$CONFIG")
USE_GSTREAMER=$(jq -er '.features.gstreamer // empty' "$CONFIG")

[ "$USE_HAMLIB" = "true" ]   && apt install -y libhamlib-dev libhamlib-utils
[ "$USE_SQLITE" = "true" ]   && apt install -y sqlite3 libsqlite3-dev
[ "$USE_PIPEWIRE" = "true" ] && apt install -y libpipewire-0.3-dev
[ "$USE_OPUS" = "true" ]     && apt install -y libopus-dev
[ "$USE_GSTREAMER" = "true" ] && apt install -y libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-tools
