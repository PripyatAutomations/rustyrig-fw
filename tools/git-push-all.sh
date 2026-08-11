#!/bin/bash
P=$(pwd)

cd librustyaxe
git commit -a
git push
cd "${P}"

cd librrprotocol
git commit -a
git push
cd "${P}"

cd www
git commit -a
git push
cd "${P}"

git commit -a
git push
