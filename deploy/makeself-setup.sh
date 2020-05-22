#!/usr/bin/bash
set -e -u

if [ $# == 0 ]; then

  dest="${HOME}/.mozilla"
  if [ -d "$dest" ]; then
    echo
    echo "Installing for Firefox..."
    dest="${dest}/native-messaging-hosts"
    mkdir -p "$dest"
    cp -v webrecorder "$dest"
    cp -v hamster "$dest"
    cp -v hamster-mozilla.json "${dest}/hamster.json"
    sed -i "s|^\(.*\"path\":\s*\"\).*\(\",.*\)|\1${dest}/hamster\2|" "${dest}/hamster.json"
    echo "Done."
  fi

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
