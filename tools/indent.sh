#!/bin/bash
C_SRC=$(find . -name \*.\[ch\] | grep -v mongoose | grep -v '^./ext/')

echo "${C_SRC}"
