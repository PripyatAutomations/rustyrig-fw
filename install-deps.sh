[ -z "$PROFILE" ] && PROFILE=radio

apt install -y libjson-perl libterm-readline-perl-perl libhash-merge-perl libjson-xs-perl \
	       libjson-perl libstring-crc32-perl libgpiod-dev libjpeg-dev libpipewire-0.3-dev \
	       libjpeg-dev libopus-dev jq pkg-config libmbedtls-dev gpiod

cpan install Mojo::JSON::Pointer

# Optional - for use with hamlib
USE_HAMLIB=$(cat config/${PROFILE}.config.json  | jq -r '.backend.hamlib')
USE_SQLITE=$(cat config/${PROFILE}.config.json  | jq -r '.features.sqlite')

if [ "$USE_HAMLIB" == "true" ]; then
   apt install -y libhamlib-dev libhamlib-utils
fi


if [ "$USE_SQLITE" == "true" ]; then
   apt install -y sqlite3-tools libsqlite3-dev 
fi
