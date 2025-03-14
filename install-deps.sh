[ -z "$PROFILE" ] && PROFILE=radio

apt install -y libjson-perl libterm-readline-perl-perl libhash-merge-perl libjson-xs-perl libjson-perl libstring-crc32-perl libgpiod-dev libjpeg-dev libpipewire-0.3-dev libjpeg-dev libopus-dev
apt install -y jq pkg-config libmbedtls-dev
cpan install Mojo::JSON::Pointer

# Optional - for use with hamlib
USE_HAMLIB=$(cat config/${PROFILE}.config.json  | jq -r '.features.backend_hamlib')

if [ "$USE_HAMLIB" == "true" ]; then
   apt install -y libhamlib-dev libhamlib-utils
fi

