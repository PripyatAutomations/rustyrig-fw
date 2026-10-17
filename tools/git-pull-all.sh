#!/bin/bash
subdirs="librrprotocol librustyaxe www callsign-lookup"
subdirs_ext="libmongoose mbedtls sqlite wslay"

git pull
git submodule init

git submodule update --remote --recursive

for i in ${subdirs}; do
   echo "pull $i"
   (cd $i; git pull)
done

for i in ${ext_subdirs}; do
   echo "ext pull: $i"
   (cd ext/$i; git pull)
done
