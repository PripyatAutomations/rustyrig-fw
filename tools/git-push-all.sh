#!/bin/bash
P=$(pwd)

cd librustyaxe
if [ -z "$1" ]; then
   git commit -a
else
   git commit -a -m "${1}"
fi

git push
cd "${P}"

cd librrprotocol
if [ -z "$1" ]; then
   git commit -a
else
   git commit -a -m "${1}"
fi

git push
cd "${P}"

cd www
if [ -z "$1" ]; then
   git commit -a
else
   git commit -a -m "${1}"
fi
git push
cd "${P}"

if [ -z "$1" ]; then
   git commit -a
else
   git commit -a -m "${1}"
fi
git push
