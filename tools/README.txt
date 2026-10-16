This directory contains various random scripts. Here's some descriptions.
If not listed, look at the top of the script for more info. Names should be clear...

    archive-config.sh		Backup the configure to config/archive/
    count-lines.sh		Show raw lines of source in the project
    source-size.py		Count owned C/header code excluding comments/blanks;
                                list large files/functions and optionally enforce a cap
    dummy-rigctld.sh		Start rigctld with dummy backend, if not running
    find-all.sh			Find all instances of a string in the tree,
                                except third party (ext/) sources.
    flrig-rigctld.sh		Start rigctld with flrig backend, if not running
    ft891-rigctld.sh		Start rigctld with ft891 backend, if not running
    git-push-all.sh		Push changes to all of my submodules to github
    gst-test.sh			gstreamer test - ignore this
    indent.sh			Format owned C sources with GNU indent, then Uncrustify
    longline.sh			Print a long line (# characters as first arg || 256)
    pipewire-test.sh		Launch pipewire user session
    rename-header.sh		Renames a header and replaces all #include references - not finished.
    rename-symbol.sh		Rename a symnbol through the source (needs rescan-tree.sh run)
    rescan-tree.sh		Scan the tree and generate compile_commands.json for rename-symbol.sh
    show-orphan-bugs.sh		Show orphan bugs (XXX: in source tree)
    start-pulse-loopback.sh	Set up pulseaudio loopback
    stop-pulse-loopback.sh	Stop pulseaudio loopback
    update-tree.sh		Update all of the tree, including third-party submodules

./tools/dump-css.pl > gtk-css.cfg
  Extract the native client's compiled default CSS into a standalone [gtk-css]
  block, including escaped ID selectors. Copy that block into rrclient.cfg,
  replacing the existing [gtk-css] section, then edit and run /css-reload.
  The helper reads rrclient/defconfig.c relative to its own location, so it can
  run from any directory. It never reads user configuration or overwrites it.
