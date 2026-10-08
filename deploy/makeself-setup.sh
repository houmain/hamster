#!/bin/bash
set -e -u

install_host() {
  dest="$1"
  manifest="$2"
  mkdir -p "$dest"
  cp -v hamster "$dest"
  cp -v "$manifest" "${dest}/hamster.json"
  sed -i.bak "s|^\(.*\"path\"[[:space:]]*:[[:space:]]*\"\).*\(\",[[:space:]]*$\)|\1${dest}/hamster\2|" "${dest}/hamster.json"
  rm -f "${dest}/hamster.json.bak"
}

browser_destinations() {
  if [ "$(uname -s)" = "Darwin" ]; then
    printf '%s\n' \
      "mozilla|${HOME}/Library/Application Support/Mozilla/NativeMessagingHosts" \
      "chromium|${HOME}/Library/Application Support/Google/Chrome/NativeMessagingHosts" \
      "chromium|${HOME}/Library/Application Support/Microsoft Edge/NativeMessagingHosts"
  else
    printf '%s\n' \
      "mozilla|${HOME}/.mozilla/native-messaging-hosts" \
      "chromium|${HOME}/.config/google-chrome/NativeMessagingHosts" \
      "chromium|${HOME}/.config/chromium/NativeMessagingHosts" \
      "chromium|${HOME}/.config/microsoft-edge/NativeMessagingHosts"
  fi
}

if [ $# == 0 ]; then
  echo "Installing..."
  while IFS='|' read -r kind dest; do
    install_host "$dest" "hamster-${kind}.json"
  done < <(browser_destinations)

  echo "Done."
  echo
  echo "To uninstall call installer with \" -- uninstall\" (note the space after --)."

elif [ $# == 1 ] && [ "$1" == "uninstall" ]; then
  echo "Uninstalling..."
  while IFS='|' read -r kind dest; do
    if [ -d "$dest" ]; then
      rm -vf "${dest}/hamster"
      rm -vf "${dest}/hamster.json"
    fi
  done < <(browser_destinations)
  echo "Done."
fi
