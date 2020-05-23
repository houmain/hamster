#!/usr/bin/bash
set -e -u

if [ $# == 0 ]; then
  echo "Installing..."

  dest="${HOME}/.mozilla"
  if [ -d "$dest" ]; then
    dest="${dest}/native-messaging-hosts"
    mkdir -p "$dest"
    cp -v bin/webrecorder "$dest"
    cp -v bin/hamster "$dest"
    cp -v hamster-mozilla.json "${dest}/hamster.json"
    sed -i "s|^\(.*\"path\":\s*\"\).*\(\",.*\)|\1${dest}/hamster\2|" "${dest}/hamster.json"
  fi

  echo "Done."
  echo
  echo "To uninstall call installer with \" -- uninstall\" (note the space after --)."

elif [ $# == 1 ] && [ "$1" == "uninstall" ]; then
  echo "Uninstalling..."

  dest="${HOME}/.mozilla/native-messaging-hosts"
  if [ -d "$dest" ]; then
    rm -vf "$dest/webrecorder"
    rm -vf "$dest/hamster"
    rm -vf "${dest}/hamster.json"
  fi
  echo "Done."
fi
