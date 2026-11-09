#!/usr/bin/env bash
# Install build dependencies for the selected RustyRig profile.
# Invokes the distro package manager directly; caller controls privileges.
set -euo pipefail

PROFILE="${PROFILE:-radio}"
usage() {
   echo "Usage: PROFILE=radio $0 [deb|rpm|arch]"
   echo "       PROFILE=radio $0 [--distro deb|rpm|arch]"
   echo "Auto-detects from /etc/os-release; defaults to deb if unknown."
   echo "Runs package manager as the invoking user (use sudo externally if needed)."
}
distro=""
case "${1:-}" in
   -h|--help) usage; exit 0 ;;
   "")
      [[ $# -eq 0 ]] || { usage >&2; exit 2; }
      ;;
   --distro)
      [[ $# -eq 2 ]] || { usage >&2; exit 2; }
      distro="$2"
      ;;
   *)
      [[ $# -eq 1 ]] || { usage >&2; exit 2; }
      distro="$1"
      ;;
esac

if [[ -z "$distro" ]]; then
   # OS identity takes priority over installed package-manager commands.
   if [[ -r /etc/os-release ]]; then
      . /etc/os-release
      for family in "${ID:-}" ${ID_LIKE:-}; do
         case "$family" in
            arch|manjaro|endeavouros|artix) distro=arch; break ;;
            debian|devuan|ubuntu|linuxmint|pop|raspbian|kali) distro=deb; break ;;
            fedora|rhel|centos|rocky|almalinux|ol) distro=rpm; break ;;
         esac
      done
   fi
   distro="${distro:-deb}"
fi

# Old --distro values remain accepted for existing wrappers.
case "$distro" in
   deb|debian|ubuntu) distro=deb ;;
   rpm|fedora|redhat|rhel) distro=rpm ;;
   arch) ;;
   *) echo "Unknown distro family: $distro (use deb, rpm, arch)" >&2; exit 2 ;;
esac

config="config/${PROFILE}.config.json"
if [[ ! -r "$config" ]]; then
   echo "Missing build profile: $config (run from the rustyrig-fw source root)" >&2
   exit 1
fi

# Run the package manager directly. The caller decides whether to use sudo.
case "$distro" in
   deb) manager=(apt-get install); bootstrap=(apt-get install jq) ;;
   rpm) manager=(dnf install); bootstrap=(dnf install jq) ;;
   arch) manager=(pacman -S --needed); bootstrap=(pacman -S --needed jq) ;;
esac
if ! command -v jq >/dev/null 2>&1; then
   echo "Installing jq to read $config..."
   "${bootstrap[@]}"
   command -v jq >/dev/null 2>&1 || { echo "jq installation did not provide jq" >&2; exit 1; }
fi

enabled() { [[ "$(jq -r "$1 // false | tostring" "$config")" == true ]]; }
# Build features may be JSON booleans or the older "true"/"false" strings.
# Native TLS, Argon2 and encrypted backups do not depend on Mongoose.
mongoose_enabled=false
if enabled '.features.mongoose'; then mongoose_enabled=true; fi
case "$distro" in
   deb)
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
   rpm)
      packages=(gcc gcc-c++ make git jq pkgconf-pkg-config libbsd-devel
         ncurses-devel glib2-devel libgpiod-devel libev-devel
         openssl-devel wslay-devel libargon2-devel libarchive-devel libcurl-devel
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

echo "Detected package family: $distro"
echo "RustyRig build profile: $config"
echo "Mongoose transport: $([[ "$mongoose_enabled" == true ]] && echo enabled || echo disabled)"
printf "Installing build dependencies with:"
printf " %q" "${manager[@]}" "${unique[@]}"
printf "\n"
"${manager[@]}" "${unique[@]}"
