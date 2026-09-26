rm -f *.buildinfo *.changes
mv ../*.deb ../rustyrig*.buildinfo ../rustyrig*.changes ../releases/
sudo dpkg -i $(ls ../releases/*.deb | egrep -v '(dbgsym|-tui)')
