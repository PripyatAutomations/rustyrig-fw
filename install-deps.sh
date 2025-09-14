#!/bin/bash
set -e

[ -z "$PROFILE" ] && PROFILE=radio

DEBVER=/etc/debian_version

if [ -f "${DEBVER}" ]; then
    # Needed for eeprom tool
    #apt install \
    #   libjson-perl libterm-readline-perl-perl libhash-merge-perl \
    #   libjson-xs-perl libstring-crc32-perl libgpiod-dev gpiod \
    #   jq pkg-config libmbedtls-dev libopus-dev libgtk-3-dev \
    #   libgstreamer-plugins-base1.0-0 libgstreamer1.0-dev


    # Mojo::JSON::Pointer used by buildconf.pl
    #cpan install Mojo::JSON::Pointer

    apt install libgpiod-dev gpiod jq pkg-config libmbedtls-dev libgtk-3-dev libgstreamer-plugins-base1.0-0 libgstreamer1.0-dev

    #CONFIG="config/${PROFILE}.config.json"
    #
    #USE_HAMLIB=$(jq -er '.backend.hamlib // empty' "$CONFIG")
    #USE_SQLITE=$(jq -er '.features.sqlite // empty' "$CONFIG")
    #USE_GSTREAMER=$(jq -er '.features.gstreamer // empty' "$CONFIG")

    #[ "$USE_HAMLIB" = "true" ]   && apt install -y libhamlib-dev libhamlib-utils
    #[ "$USE_SQLITE" = "true" ]   && apt install -y sqlite3 libsqlite3-dev
    #[ "$USE_GSTREAMER" = "true" ] && apt install -y libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-tools
    apt install build-essential gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-rtp libgtk-3-dev make sqlite3 libsqlite3-dev
     
    #sudo apt install build-essential gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-rtp libgtk-3-dev make libgstreamer1.0-dev gstreamer1.0-tools gstreamer1.0-plugins-base-apps libmbedtls-dev libhamlib-dev libhamlib-dev jq libsqlite3-dev sqlite3-tools sqlite3 sqlite3-pcre libgpiod-dev libhamlib-utils
    #sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
fi
