#!/bin/bash
P=$(pwd)

if [ ! -z "$1" ]; then
   MSG="-m \"$1\""
fi

cd librustyaxe
git commit -a ${MSG}
git push
cd "${P}"

cd librrprotocol
git commit -a ${MSG}
git push
cd "${P}"

cd www
git commit -a ${MSG}
git push
cd "${P}"

git commit -a ${MSG}
git push
