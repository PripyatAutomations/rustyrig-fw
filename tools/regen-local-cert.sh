#!/bin/bash
# XXX: This needs to extract site.name and domain.name
# XXX: from /etc/rustyrig/rrserver*.cfg

cd /var/tmp

sudo cp -a /var/lib/rustyrig/identity/tls-cert.pem \
  /var/lib/rustyrig/identity/tls-cert.pem.bak

sudo openssl req -new -x509 -days 397 \
  -key /var/lib/rustyrig/identity/tls-key.pem \
  -out /var/lib/rustyrig/identity/tls-cert.pem \
  -subj "/CN=ashtwv01.ham.istabpeople.com" \
  -addext "subjectAltName=DNS:ashtwv01.ham.istabpeople.com" \
  -addext "basicConstraints=critical,CA:FALSE" \
  -addext "keyUsage=critical,digitalSignature" \
  -addext "extendedKeyUsage=serverAuth"
  