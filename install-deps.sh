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
    if [ ! -f "$CONFIG" ]; then
        echo "Missing configuration: $CONFIG" >&2
        exit 1
    fi

    # Configuration values live under .features and .components.  Older
    # versions of this script looked under .use.* and silently returned null.
    USE_GTK=$(jq -r '.features.gtk // "false"' "$CONFIG")
    USE_LIBMBEDTLS=$(jq -r '.features.libmbedtls // "false"' "$CONFIG")
    USE_LIBNOTIFY=$(jq -r '.features.libnotify // "false"' "$CONFIG")
    USE_SQLITE=$(jq -r '.features.sqlite // "false"' "$CONFIG")
    BUILD_FWDSP=$(jq -r '.components.fwdsp // "false"' "$CONFIG")
    BUILD_CALLSIGN_LOOKUP=$(jq -r '.components."callsign-lookup" // "false"' "$CONFIG")

    PKG="build-essential jq pkg-config make libbsd-dev libncurses-dev"
    PKG="${PKG} ${PERL_DEPS}"

    # These sources are currently compiled and linked unconditionally.  They
    # can become conditional when GPIO and Hamlib move into addon packages.
    PKG="${PKG} libgpiod-dev libhamlib-dev libev-dev"

    [ "$USE_GTK" = "true" ] && PKG="${PKG} libgtk-3-dev"
    [ "$USE_SQLITE" = "true" ] && PKG="${PKG} sqlite3 libsqlite3-dev"
    [ "$USE_LIBNOTIFY" = "true" ] && PKG="${PKG} libnotify-dev"
    [ "$USE_LIBMBEDTLS" = "true" ] && PKG="${PKG} libmbedtls-dev"

    # fwdsp is a separate component, but its C sources always use GStreamer.
    if [ "$BUILD_FWDSP" = "true" ]; then
        PKG="${PKG} libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev"
        PKG="${PKG} gstreamer1.0-plugins-good gstreamer1.0-plugins-bad"
        PKG="${PKG} gstreamer1.0-tools gstreamer1.0-plugins-rtp"
    fi

    # callsign-lookup uses libcurl and libev directly.
    [ "$BUILD_CALLSIGN_LOOKUP" = "true" ] && PKG="${PKG} libcurl4-openssl-dev"

    echo "Please use the following command to install needed build-deps:"
    echo apt install ${PKG}
fi
