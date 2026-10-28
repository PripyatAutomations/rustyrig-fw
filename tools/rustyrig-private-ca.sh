#!/bin/sh
# Provision a private RustyRig CA and a server certificate with explicit DNS SAN.
# Run on a protected administration host. The CA private key MUST NOT be
# distributed to servers or clients.
set -eu
umask 077

usage() {
  echo "Usage: $0 init-ca CA_DIR | issue CA_DIR SERVER_DIR DNS_NAME [IP_ADDRESS] | fingerprint CERT_PEM" >&2
  exit 2
}
[ "$#" -ge 2 ] || usage
command=$1
shift

case "$command" in
  init-ca)
    [ "$#" -eq 1 ] || usage
    dir=$1
    [ ! -e "$dir/ca.key.pem" ] && [ ! -e "$dir/ca.cert.pem" ] || {
      echo "Refusing to replace an existing CA" >&2; exit 1;
    }
    mkdir -p "$dir"
    chmod 0700 "$dir"
    openssl req -new -x509 -newkey ed25519 -nodes -days 3650 \
      -subj "/CN=RustyRig Private Root CA" \
      -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
      -addext "keyUsage=critical,keyCertSign,cRLSign" \
      -keyout "$dir/ca.key.pem" -out "$dir/ca.cert.pem"
    chmod 0600 "$dir/ca.key.pem"
    chmod 0644 "$dir/ca.cert.pem"
    echo "Keep $dir/ca.key.pem OFFLINE; distribute only ca.cert.pem"
    ;;
  issue)
    [ "$#" -ge 3 ] && [ "$#" -le 4 ] || usage
    ca=$1; dir=$2; dns=$3; ip=${4:-}
    [ -f "$ca/ca.key.pem" ] && [ -f "$ca/ca.cert.pem" ] || {
      echo "CA key and certificate required" >&2; exit 1;
    }
    [ ! -e "$dir/key.pem" ] && [ ! -e "$dir/cert.pem" ] || {
      echo "Refusing to overwrite existing host certificate or key" >&2; exit 1;
    }
    case "$dns" in
      *[!A-Za-z0-9.-]* | "" | .* | *.) echo "Invalid DNS name" >&2; exit 2;;
    esac
    san="DNS:$dns"
    if [ -n "$ip" ]; then
      case "$ip" in *[!0-9a-fA-F:.]* | "") echo "Invalid IP address" >&2; exit 2;; esac
      san="$san,IP:$ip"
    fi
    mkdir -p "$dir"; chmod 0700 "$dir"
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT HUP INT TERM
    openssl req -new -newkey ed25519 -nodes \
      -subj "/CN=$dns" -keyout "$tmp/key" -out "$tmp/request"
    cat > "$tmp/ext" <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=$san
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid,issuer
EOF
    openssl x509 -req -in "$tmp/request" -CA "$ca/ca.cert.pem" \
      -CAkey "$ca/ca.key.pem" -CAcreateserial -days 397 \
      -extfile "$tmp/ext" -out "$tmp/cert"
    openssl verify -CAfile "$ca/ca.cert.pem" -purpose sslserver "$tmp/cert"
    install -m 0600 "$tmp/key" "$dir/key.pem"
    install -m 0644 "$tmp/cert" "$dir/cert.pem"
    echo "Host SPKI SHA-256 pin:"
    openssl x509 -in "$dir/cert.pem" -pubkey -noout |
      openssl pkey -pubin -outform DER |
      openssl dgst -sha256 -binary | openssl base64 -A
    echo
    ;;
  fingerprint)
    [ "$#" -eq 1 ] || usage
    openssl x509 -in "$1" -pubkey -noout |
      openssl pkey -pubin -outform DER |
      openssl dgst -sha256 -binary | openssl base64 -A
    echo
    ;;
  *) usage;;
esac
