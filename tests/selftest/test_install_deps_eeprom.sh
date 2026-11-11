#!/usr/bin/env bash
# Confirm install-deps never requests Perl outside an EEPROM/embedded build.
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! command -v jq >/dev/null 2>&1; then
   echo 'SKIP: jq is required for install-deps profile tests'
   exit 0
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for manager in apt-get dnf pacman; do
   cat >"$tmp/$manager" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$@" >"$RR_DEPS_CAPTURE"
SH
   chmod +x "$tmp/$manager"
done

test_packages() {
   local distro=$1 eeprom=$2 mongoose=$3
   RR_DEPS_CAPTURE="$tmp/packages" PROFILE=radio \
      USE_EEPROM="$eeprom" USE_MONGOOSE="$mongoose" \
      PATH="$tmp:$PATH" bash ./install-deps.sh "$distro" >"$tmp/output"
   if [[ $eeprom == true ]]; then
      grep -q '^perl$' "$tmp/packages"
      case "$distro" in
         deb)
            grep -q '^libhash-merge-perl$' "$tmp/packages"
            grep -q '^libmojolicious-perl$' "$tmp/packages" ;;
         arch)
            grep -q '^perl-hash-merge$' "$tmp/packages"
            grep -q '^perl-mojolicious$' "$tmp/packages" ;;
         rpm)
            grep -q '^perl-Hash-Merge$' "$tmp/packages"
            grep -q '^perl-Mojolicious$' "$tmp/packages" ;;
      esac
   elif grep -Eiq '^perl([-[:alnum:]]*)?$|^lib.*-perl$' "$tmp/packages"; then
      echo "Unexpected Perl dependency: distro=$distro mongoose=$mongoose" >&2
      cat "$tmp/packages" >&2
      return 1
   fi
}

for distro in deb arch rpm; do
   test_packages "$distro" false false
   test_packages "$distro" false true
   test_packages "$distro" true false
   test_packages "$distro" true true
done
echo 'PASS: Perl dependencies are EEPROM-only on Debian, Arch, and RPM'
