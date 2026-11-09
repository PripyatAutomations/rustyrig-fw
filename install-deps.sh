#!/usr/bin/env bash
# Print build dependency commands for the selected RustyRig profile.
# This script does not install packages automatically.
set -euo pipefail

PROFILE="${PROFILE:-radio}"
distro=""
if [[ "${1:-}" == "--distro" && -n "${2:-}" ]]; then
   distro="$2"
elif [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
   echo "Usage: PROFILE=radio $0 [--distro debian|arch|fedora]"
   exit 0
elif [[ $# -gt 0 ]]; then
   echo "Unknown option: $1" >&2
   exit 2
fi

if [[ -z "$distro" ]]; then
   if [[ -r /etc/os-release ]]; then
      . /etc/os-release
      case " ${ID:-} ${ID_LIKE:-} " in
         *" arch "*|*" manjaro "*|*" endeavouros "*) distro=arch ;;
         *" debian "*|*" ubuntu "*) distro=debian ;;
         *" fedora "*|*" rhel "*|*" centos "*|*" rocky "*|*" almalinux "*) distro=fedora ;;
      esac
   fi
fi
if [[ -z "$distro" ]]; then
   if command -v pacman >/dev/null 2>&1; then distro=arch
   elif command -v apt-get >/dev/null 2>&1; then distro=debian
   elif command -v dnf >/dev/null 2>&1; then distro=fedora
   else echo "Unsupported distribution; use --distro debian|arch|fedora" >&2; exit 1
   fi
fi

config="config/${PROFILE}.config.json"
if [[ ! -r "$config" ]]; then
   echo "Missing build profile: $config (run from the rustyrig-fw source root)" >&2
   exit 1
fi

# jq is a bootstrap dependency, so give a useful install command even without it.
case "$distro" in
   arch) manager="pacman -S --needed"; bootstrap="pacman -S --needed jq" ;;
   debian) manager="apt install"; bootstrap="apt install jq" ;;
   fedora|redhat|rhel) distro=fedora; manager="dnf install"; bootstrap="dnf install jq" ;;
   *) echo "Unsupported distribution: $distro" >&2; exit 1 ;;
esac
if ! command -v jq >/dev/null 2>&1; then
   echo "jq is required to read $config; install it first:"
   echo "${SUDO:-sudo} $bootstrap"
   exit 1
fi

enabled() { [[ "$(jq -r "$1 // false | tostring" "$config")" == true ]]; }
# Build features may be JSON booleans or the older "true"/"false" strings.
# Native TLS, Argon2 and encrypted backups do not depend on Mongoose.
mongoose_enabled=false
if enabled '.features.mongoose'; then mongoose_enabled=true; fi
case "$distro" in
   debian)
      packages=(build-essential git jq pkg-config make libbsd-dev libncurses-dev
         debhelper libglib2.0-dev libgpiod-dev libev-dev libcrypt-dev
         libssl-dev libwslay-dev libargon2-dev libarchive-dev libcurl4-openssl-dev
         libjson-perl libterm-readline-perl-perl libhash-merge-perl
         libjson-xs-perl libstring-crc32-perl libjson-validator-perl)
      enabled '.backend.hamlib' && packages+=(libhamlib-dev)
      enabled '.features.gtk' && packages+=(libgtk-3-dev libvte-2.91-dev)
      enabled '.features.sqlite' && packages+=(sqlite3 libsqlite3-dev libspatialite-dev)
      enabled '.features.libnotify' && packages+=(libnotify-dev)
      # Only the optional Mongoose TLS backend needs mbedTLS.
      if [[ "$mongoose_enabled" == true ]]; then
         packages+=(libmbedtls-dev mongoose-webserver-source)
      fi
      enabled '.features.rtp' && packages+=(libsrtp2-dev)
      if enabled '.components.fwdsp'; then
         packages+=(libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
            gstreamer1.0-pulseaudio gstreamer1.0-plugins-good
            gstreamer1.0-plugins-bad gstreamer1.0-tools)
      fi
      enabled '.components."callsign-lookup"' && packages+=(libcurl4-openssl-dev)
      ;;
   arch)
      packages=(base-devel git jq pkgconf make libbsd ncurses glib2 libgpiod libev
         openssl wslay argon2 libarchive curl
         perl perl-json perl-json-xs)
      enabled '.backend.hamlib' && packages+=(hamlib)
      enabled '.features.gtk' && packages+=(gtk3 vte3)
      enabled '.features.sqlite' && packages+=(sqlite libspatialite)
      enabled '.features.libnotify' && packages+=(libnotify)
      [[ "$mongoose_enabled" == true ]] && packages+=(mbedtls)
      enabled '.features.rtp' && packages+=(libsrtp)
      if enabled '.components.fwdsp'; then
         packages+=(gstreamer gst-plugins-base-libs gst-plugins-base
            gst-plugins-good gst-plugins-bad)
      fi
      enabled '.components."callsign-lookup"' && packages+=(curl)
      ;;
   fedora)
      packages=(gcc gcc-c++ make git jq pkgconf-pkg-config libbsd-devel
         ncurses-devel glib2-devel libgpiod-devel libev-devel
         openssl-devel libwslay-devel libargon2-devel libarchive-devel libcurl-devel
         perl perl-JSON perl-JSON-XS)
      enabled '.backend.hamlib' && packages+=(hamlib-devel)
      enabled '.features.gtk' && packages+=(gtk3-devel vte291-devel)
      enabled '.features.sqlite' && packages+=(sqlite sqlite-devel libspatialite-devel)
      enabled '.features.libnotify' && packages+=(libnotify-devel)
      [[ "$mongoose_enabled" == true ]] && packages+=(mbedtls-devel)
      enabled '.features.rtp' && packages+=(libsrtp-devel)
      if enabled '.components.fwdsp'; then
         packages+=(gstreamer1-devel gstreamer1-plugins-base-devel
            gstreamer1-plugins-base gstreamer1-plugins-good
            gstreamer1-plugins-bad-free)
      fi
      enabled '.components."callsign-lookup"' && packages+=(libcurl-devel)
      ;;
esac

# Preserve order but suppress duplicates.
declare -A seen=()
unique=()
for package in "${packages[@]}"; do
   [[ -n "${seen[$package]:-}" ]] && continue
   seen[$package]=1
   unique+=("$package")
done

echo "Detected distribution: $distro"
echo "RustyRig build profile: $config"
echo "Mongoose transport: $([[ "$mongoose_enabled" == true ]] && echo 'enabled (optional backend)' || echo 'disabled (native OpenSSL/wslay)')"
echo "Install build dependencies with:"
printf '%s %s' "${SUDO:-sudo}" "$manager"
printf ' %q' "${unique[@]}"
printf '\n'
