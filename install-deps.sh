#!/bin/bash
set -e

[ -z "$PROFILE" ] && PROFILE=radio

DEBVER=/etc/debian_version

# Is this a debian offspring? If so, it'll use APT
# XXX: We need to work on the fact that package names vary (check actual distro later)
if [ -f "${DEBVER}" ]; then
    # Needed for eeprom tool
    apt install \
       libjson-perl libterm-readline-perl-perl libhash-merge-perl \
       libjson-xs-perl libstring-crc32-perl libgpiod-dev gpiod \
       jq pkg-config libmbedtls-dev libopus-dev libgtk-3-dev \
       libgstreamer-plugins-base1.0-0 libgstreamer1.0-dev


    # Mojo::JSON::Pointer used by buildconf.pl
    cpan install Mojo::JSON::Pointer

    CONFIG="config/${PROFILE}.config.json"
    USE_HAMLIB=$(jq -er '.backend.hamlib // empty' "$CONFIG")
    USE_GPIOD=$(jq -er '.use.gpio // empty' "$CONFIG")
    USE_LIBMBEDTLS=$(jq -er '.use.mbedtls // empty' "$CONFIG")
    USE_LIBNOTIFY=$(jq -er '.use.mbedtls // empty' "$CONFIG")
    USE_LIBEV=$(jq -er '.use.libev // empty' "$CONFIG")
    USE_SQLITE=$(jq -er '.use.sqlite // empty' "$CONFIG")
    USE_GSTREAMER=$(jq -er '.use.gstreamer // empty' "$CONFIG")
    USE_GTK=$(jq -er '.use.gtk // empty' "$CONFIG") && PKG="${PKG} libgtk-3-dev"

    [ "$USE_HAMLIB" = "true" ] && PKG="${PKG} libhamlib-dev libhamlib-utils"
    [ "$USE_SQLITE" = "true" ] && PKG="${PKG} sqlite3 libsqlite3-dev"
    [ "$USE_GSTREAMER" = "true" ] && PKG="${PKG} libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-tools gstreamer1.0-plugins-rtp"
    [ "$USE_LIBNOTIFY" = "true" ] && PKG="${PKG} libnotify-dev"
    [ "$USE_LIBEV" = "true" ] && PKG="${PKG} libev-dev"
    [ "$USE_LIBMEDTLS" = "true" ] && PKG="${PKG} libmedtls-dev"
    [ "$USE_GPIOD" = "true" ] && PKG="${PKG} libgpiod-dev gpiod"

    apt install build-essential jq pkg-config make libbsd-dev libncurses-dev ${PKG}
fi
