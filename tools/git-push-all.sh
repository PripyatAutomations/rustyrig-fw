#!/bin/bash
P=$(pwd)

# The repository version is a date plus a two-digit build sequence.  Advance
# it once for each push operation so all repositories in this checkout share
# the same package/build version.
version_date=$(date +%Y%m%d)
old_version=$(tr -d '[:space:]' < .version 2>/dev/null || true)
old_date=${old_version%%.*}
old_sequence=${old_version#*.}
if [[ "${old_date}" == "${version_date}" && "${old_sequence}" =~ ^[0-9]+$ ]]; then
   next_sequence=$((10#${old_sequence} + 1))
else
   next_sequence=1
fi
new_version=$(printf '%s.%02d' "${version_date}" "${next_sequence}")
printf '%s\n' "${new_version}" > .version
echo "VERSION: ${old_version:-unset} -> ${new_version}"

# Debian derives its package version from the first line of the changelog.
# Keep it synchronized when packaging metadata is present.
if [ -f debian/changelog ]; then
   sed -i -E "1s/^rustyrig-fw \([^)]*\)/rustyrig-fw (${new_version})/" debian/changelog
fi

for i in librustyaxe librrprotocol www callsign-lookup .; do
   cd $i
   echo "PUSH: $i"
   if [ -z "$1" ]; then
      git commit -a
   else
      git commit -a -m "${1}"
   fi
   git push
   cd "${P}"
done
