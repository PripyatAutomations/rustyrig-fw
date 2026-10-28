#!/bin/sh
# Per-host self-signed TLS certificate with stable public-key fingerprint.
# No shared CA or system certificate store needed for provisioning.
set -eu
umask 077
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo "Usage: $0 OUTPUT_DIRECTORY DNS_NAME [IP_ADDRESS]" >&2
  exit 2
fi
dir=$1 dns=$2 ip=${3:-}
case "$dir" in /*) ;; *) echo "Use an absolute output directory" >&2; exit 2;; esac
case "$dns" in *[!A-Za-z0-9.-]* | "" | .* | *.) echo "Invalid DNS name" >&2; exit 2;; esac
[ ! -e "$dir/key.pem" ] && [ ! -e "$dir/cert.pem" ] || {
  echo "Refusing to overwrite an existing host identity" >&2; exit 1;
}
san="DNS:$dns"
if [ -n "$ip" ]; then
  case "$ip" in *[!0-9a-fA-F:.]* | "") echo "Invalid IP address" >&2; exit 2;; esac
  san="$san,IP:$ip"
fi
mkdir -p "$dir"
chmod 0700 "$dir"
openssl req -new -x509 -newkey ed25519 -nodes -days 397 \
  -subj "/CN=$dns" \
  -addext "basicConstraints=critical,CA:FALSE" \
  -addext "keyUsage=critical,digitalSignature" \
  -addext "extendedKeyUsage=serverAuth" \
  -addext "subjectAltName=$san" \
  -keyout "$dir/key.pem" -out "$dir/cert.pem"
chmod 0600 "$dir/key.pem"
chmod 0644 "$dir/cert.pem"
printf 'TLS SPKI SHA256 (base64): '
openssl x509 -in "$dir/cert.pem" -pubkey -noout |
  openssl pkey -pubin -outform DER |
  openssl dgst -sha256 -binary | openssl base64 -A
printf '\n'
echo "Verify fingerprint out-of-band before enrolling this host."
