[ -z "$PROFILE" ] && PROFILE=radio

apt install -y libjson-perl libterm-readline-perl-perl libhash-merge-perl libjson-xs-perl \
	       libjson-perl libstring-crc32-perl libgpiod-dev gpiod jq pkg-config libmbedtls-dev

# Used in buildconf.pl to grok json
cpan install Mojo::JSON::Pointer

# Optional - for use with hamlib
USE_HAMLIB=$(cat config/${PROFILE}.config.json  | jq -r '.backend.hamlib')
USE_SQLITE=$(cat config/${PROFILE}.config.json  | jq -r '.features.sqlite')
USE_PIPEWIRE=$(cat config/${PROFILE}.config.json  | jq -r '.features.pipewire')
USE_OPUS=$(cat config/${PROFILE}.config.json  | jq -r '.features.opus')

if [ "$USE_HAMLIB" == "true" ]; then
   apt install -y libhamlib-dev libhamlib-utils
fi


if [ "$USE_SQLITE" == "true" ]; then
   apt install -y sqlite3-tools libsqlite3-dev 
fi

if [ "$USE_PIPEWIRE" == "true" ]; then
   apt install -y libpipewire-0.3-dev
fi

if [ "$USE_OPUS" == "true" ]; then
   apt install -y libopus-dev
fi
