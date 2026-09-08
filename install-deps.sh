#!/bin/bash
[ -z "$PROFILE" ] && PROFILE=radio

DEBVER=/etc/debian_version

# Is this a debian offspring? If so, it'll use APT
# XXX: We need to work on the fact that package names vary (check actual distro later)
if [ -f "${DEBVER}" ]; then
    # Needed for eeprom tool / header generation
    PERL_DEPS="libjson-perl libterm-readline-perl-perl libhash-merge-perl"
    PERL_DEPS="${PERL_DEPS} libjson-xs-perl libstring-crc32-perl libjson-validator-perl"

    CONFIG="config/${PROFILE}.config.json"
    USE_HAMLIB=$(jq -r '.backend.hamlib' "$CONFIG")
    USE_GPIOD=$(jq -r '.use.gpio' "$CONFIG")
    USE_LIBMBEDTLS=$(jq -r '.use.mbedt_s' "$CONFIG")
    USE_LIBNOTIFY=$(jq -r '.use.libnotify' "$CONFIG")
    USE_SQLITE=$(jq -r '.use.sqlite' "$CONFIG")
    USE_GSTREAMER=$(jq -r '.use.gstreamer' "$CONFIG")
    USE_GTK=$(jq -r '.use.gtk' "$CONFIG") && PKG="${PKG} libgtk-3-dev"

    [ "$USE_HAMLIB" = "true" ] && PKG="${PKG} libhamlib-dev libhamlib-utils"
    [ "$USE_SQLITE" = "true" ] && PKG="${PKG} sqlite3 libsqlite3-dev"
    [ "$USE_GSTREAMER" = "true" ] && PKG="${PKG} libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-tools gstreamer1.0-plugins-rtp"
    [ "$USE_LIBNOTIFY" = "true" ] && PKG="${PKG} libnotify-dev"
    [ "$USE_LIBMEDTLS" = "true" ] && PKG="${PKG} libmedtls-dev"
    [ "$USE_GPIOD" = "true" ] && PKG="${PKG} libgpiod-dev gpiod"
    echo apt install build-essential jq pkg-config make libbsd-dev libncurses-dev ${PERL_DEPS} ${PKG}
fi
