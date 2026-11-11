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
   echo "Only USE_EEPROM=true (or profile features.eeprom=true) installs EEPROM Perl modules."
   echo "Mongoose does not enable the EEPROM/Perl toolchain."
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
# EEPROM generation is an embedded-only feature, independent of Mongoose.
# Native TLS, Argon2 and encrypted backups do not depend on Mongoose.
mongoose_enabled=false
eeprom_enabled=false
if enabled '.features.mongoose'; then mongoose_enabled=true; fi
if enabled '.features.eeprom'; then eeprom_enabled=true; fi
# Make command-line overrides take precedence over profile defaults.
if [[ -n ${USE_MONGOOSE:-} ]]; then mongoose_enabled=$USE_MONGOOSE; fi
if [[ -n ${USE_EEPROM:-} ]]; then eeprom_enabled=$USE_EEPROM; fi
for option in "$mongoose_enabled" "$eeprom_enabled"; do
   [[ "$option" == true || "$option" == false ]] || {
      echo "USE_MONGOOSE and USE_EEPROM must be true or false" >&2
      exit 2
   }
done
case "$distro" in
   deb)
      packages=(build-essential git jq python3 pkg-config make libbsd-dev libncurses-dev
         debhelper libglib2.0-dev libgpiod-dev libev-dev libcrypt-dev
         libssl-dev libwslay-dev libargon2-dev libarchive-dev libcurl4-openssl-dev)
      # The embedded EEPROM packer is the only default-build consumer
      # of these Perl modules; desktop/server builds use Python instead.
      if [[ "$eeprom_enabled" == true ]]; then
         packages+=(perl libjson-perl libhash-merge-perl
            libstring-crc32-perl libmojolicious-perl)
      fi
      enabled '.components.rrclient' && packages+=(libflac-dev libogg-dev libvorbis-dev)
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
         openssl wslay argon2 libarchive curl python)
      if [[ "$eeprom_enabled" == true ]]; then
         packages+=(perl perl-json perl-hash-merge perl-string-crc32 perl-mojolicious)
      fi
      enabled '.components.rrclient' && packages+=(flac libogg libvorbis)
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
         python3)
      if [[ "$eeprom_enabled" == true ]]; then
         packages+=(perl perl-JSON perl-Hash-Merge perl-String-CRC32 perl-Mojolicious)
      fi
      enabled '.components.rrclient' && packages+=(flac-devel libogg-devel libvorbis-devel)
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
echo "EEPROM generation: $([[ "$eeprom_enabled" == true ]] && echo enabled || echo disabled)"
echo "EEPROM Perl toolchain: $([[ "$eeprom_enabled" == true ]] && echo enabled || echo not-required)"
printf "Installing build dependencies with:"
printf " %q" "${manager[@]}" "${unique[@]}"
printf "\n"
"${manager[@]}" "${unique[@]}"
