#!/bin/sh
set -e
cd ext/libmongoose
git pull origin master
cd ../..
git pull
