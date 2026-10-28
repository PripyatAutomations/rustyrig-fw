#!/bin/bash
git fetch origin
git switch mesh-linking
git pull --ff-only
git submodule update --init --recursive
