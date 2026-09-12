#!/bin/bash
subdirs="librrprotocol librustyaxe www callsign-lookup"
subdirs_ext="libmongoose mbedtls sqlite wslay"

git pull
for i in ${subdirs}; do
   (cd $i; git pull)
done

for i in ${ext_subdirs}; do
   (cd ext/$i; git pull)
done
