#!/bin/bash
set -euo pipefail

PAYLOAD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"

die() {
  echo "Error: $*" >&2
  exit 1
}

expand_home_path() {
  case "$1" in
    '~')
      printf '%s\n' "$HOME"
      ;;
    '~/'*)
      printf '%s/%s\n' "$HOME" "${1#\~/}"
      ;;
    '$HOME')
      printf '%s\n' "$HOME"
      ;;
    '$HOME/'*)
      printf '%s/%s\n' "$HOME" "${1#\$HOME/}"
      ;;
    *)
      printf '%s\n' "$1"
      ;;
  esac
}

documents_dir() {
  local configured=""

  if [ "$(uname -s)" != "Darwin" ]; then
    if [ -n "${XDG_DOCUMENTS_DIR:-}" ]; then
      configured="$XDG_DOCUMENTS_DIR"
    elif command -v xdg-user-dir >/dev/null 2>&1; then
      configured="$(xdg-user-dir DOCUMENTS 2>/dev/null || true)"
    else
      local user_dirs_file="${XDG_CONFIG_HOME:-${HOME}/.config}/user-dirs.dirs"
      if [ -f "$user_dirs_file" ]; then
        configured="$(sed -n 's/^XDG_DOCUMENTS_DIR="\([^"]*\)"$/\1/p' "$user_dirs_file" | tail -n 1)"
      fi
    fi
  fi

  if [ -n "$configured" ]; then
    expand_home_path "$configured"
  else
    printf '%s/Documents\n' "$HOME"
  fi
}

default_install_dir() {
  if [ -n "${HAMSTER_INSTALL_DIR:-}" ]; then
    expand_home_path "$HAMSTER_INSTALL_DIR"
  else
    printf '%s/Hamster\n' "$(documents_dir)"
  fi
}

state_file() {
  if [ "$(uname -s)" = "Darwin" ]; then
    printf '%s/Library/Application Support/Hamster/install-dir\n' "$HOME"
  elif [ -n "${XDG_STATE_HOME:-}" ]; then
    printf '%s/hamster/install-dir\n' "$XDG_STATE_HOME"
  else
    printf '%s/.local/state/hamster/install-dir\n' "$HOME"
  fi
}

choose_install_dir() {
  local requested="${1:-}"
  local default_dir
  default_dir="$(default_install_dir)"

  if [ -z "$requested" ]; then
    requested="$default_dir"
    if [ -t 0 ]; then
      printf 'Installation directory [%s]: ' "$default_dir" >&2
      local answer=""
      IFS= read -r answer || true
      if [ -n "$answer" ]; then
        requested="$answer"
      fi
    fi
  fi

  case "$requested" in
    *$'\n'*|*$'\r'*)
      die "the installation directory must not contain a newline"
      ;;
  esac

  requested="$(expand_home_path "$requested")"
  case "$requested" in
    /*) ;;
    *) requested="$HOME/$requested" ;;
  esac
  mkdir -p "$requested"
  (cd "$requested" && pwd -P)
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

json_escape() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  value="${value//$'\t'/\\t}"
  printf '%s' "$value"
}

write_browser_manifest() {
  local destination="$1"
  local template="$2"
  local executable_path="$3"
  local json_path
  local sed_path

  mkdir -p "$destination"
  cp -p "$PAYLOAD_DIR/$template" "$destination/hamster.json"

  json_path="$(json_escape "$executable_path")"
  sed_path="${json_path//\\/\\\\}"
  sed_path="${sed_path//&/\\&}"
  sed_path="${sed_path//|/\\|}"
  sed -i.bak \
    "s|\"path\"[[:space:]]*:[[:space:]]*\"[^\"]*\"|\"path\": \"${sed_path}\"|" \
    "$destination/hamster.json"
  rm -f "$destination/hamster.json.bak"

  grep -Fq "\"path\": \"${json_path}\"" "$destination/hamster.json" || \
    die "could not update $destination/hamster.json"

  # Remove the executable left by installers older than the shared-install layout.
  rm -f "$destination/hamster"
}

install_runtime() {
  local install_dir="$1"
  local installed_files="$install_dir/.hamster-installed-files"
  local installed_files_tmp="$installed_files.tmp"
  local runtime_files=("$PAYLOAD_DIR/hamster")
  local curl_files=()
  local source_file
  local relative_path

  [ -x "$PAYLOAD_DIR/hamster" ] || die "the installer payload does not contain hamster"

  shopt -s nullglob
  if [ "$(uname -s)" = "Darwin" ]; then
    runtime_files+=("$PAYLOAD_DIR"/*.dylib)
    curl_files=("$PAYLOAD_DIR"/libcurl-impersonate*.dylib)
  else
    runtime_files+=("$PAYLOAD_DIR"/*.so "$PAYLOAD_DIR"/*.so.*)
    curl_files=("$PAYLOAD_DIR"/libcurl-impersonate.so*)
  fi
  shopt -u nullglob

  [ "${#curl_files[@]}" -gt 0 ] || \
    die "the installer payload is missing the curl-impersonate runtime library"

  : > "$installed_files_tmp"
  for source_file in "${runtime_files[@]}"; do
    cp -pP "$source_file" "$install_dir/"
    printf '%s\n' "${source_file##*/}" >> "$installed_files_tmp"
  done

  if [ -d "$PAYLOAD_DIR/licenses/curl-impersonate" ]; then
    [ ! -L "$install_dir/licenses" ] || die "$install_dir/licenses must not be a symbolic link"
    [ ! -L "$install_dir/licenses/curl-impersonate" ] || \
      die "$install_dir/licenses/curl-impersonate must not be a symbolic link"
    mkdir -p "$install_dir/licenses/curl-impersonate"
    shopt -s nullglob
    for source_file in "$PAYLOAD_DIR"/licenses/curl-impersonate/*; do
      [ -f "$source_file" ] || continue
      relative_path="licenses/curl-impersonate/${source_file##*/}"
      cp -pP "$source_file" "$install_dir/$relative_path"
      printf '%s\n' "$relative_path" >> "$installed_files_tmp"
    done
    shopt -u nullglob
  fi

  mv -f "$installed_files_tmp" "$installed_files"
}

verify_runtime() {
  local install_dir="$1"
  local missing=""

  if [ "$(uname -s)" = "Linux" ] && command -v ldd >/dev/null 2>&1; then
    missing="$(LD_LIBRARY_PATH="${install_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
      ldd "$install_dir/hamster" 2>/dev/null | \
      awk '$2 == "not" && $3 == "found" { print $1 }')"
    if [ -n "$missing" ]; then
      die "the installed program still has missing libraries: $(printf '%s' "$missing" | tr '\n' ' ')"
    fi
  fi
}

remember_install_dir() {
  local install_dir="$1"
  local file
  file="$(state_file)"
  mkdir -p "$(dirname "$file")"
  printf '%s\n' "$install_dir" > "$file"
}

install_hamster() {
  local install_dir
  local kind
  local destination
  install_dir="$(choose_install_dir "${1:-}")"

  echo "Installing Hamster in $install_dir ..."
  install_runtime "$install_dir"
  verify_runtime "$install_dir"

  while IFS='|' read -r kind destination; do
    write_browser_manifest \
      "$destination" "hamster-${kind}.json" "$install_dir/hamster"
  done < <(browser_destinations)

  remember_install_dir "$install_dir"
  echo "Done."
  echo
  echo "To uninstall, call this installer with \" -- uninstall\" (note the space after --)."
}

remove_installed_runtime() {
  local install_dir="$1"
  local installed_files="$install_dir/.hamster-installed-files"
  local relative_path

  if [ ! -f "$installed_files" ]; then
    echo "No managed Hamster installation found in $install_dir; leaving it unchanged."
    return
  fi

  while IFS= read -r relative_path; do
    case "$relative_path" in
      ''|/*|..|../*|*/../*|*/..)
        echo "Ignoring unsafe uninstall entry: $relative_path" >&2
        ;;
      *)
        rm -f "$install_dir/$relative_path"
        ;;
    esac
  done < "$installed_files"
  rm -f "$installed_files"
  rmdir "$install_dir/licenses/curl-impersonate" 2>/dev/null || true
  rmdir "$install_dir/licenses" 2>/dev/null || true
  rmdir "$install_dir" 2>/dev/null || true
}

uninstall_hamster() {
  local kind
  local destination
  local file
  local install_dir=""
  file="$(state_file)"

  if [ -n "${HAMSTER_INSTALL_DIR:-}" ]; then
    install_dir="$(expand_home_path "$HAMSTER_INSTALL_DIR")"
  elif [ -f "$file" ]; then
    IFS= read -r install_dir < "$file" || true
  else
    install_dir="$(default_install_dir)"
  fi

  echo "Uninstalling Hamster ..."
  while IFS='|' read -r kind destination; do
    rm -f "$destination/hamster.json"
    # Also clean up copies made by installers older than the shared-install layout.
    rm -f "$destination/hamster"
  done < <(browser_destinations)

  if [ -n "$install_dir" ]; then
    remove_installed_runtime "$install_dir"
  fi
  rm -f "$file"
  rmdir "$(dirname "$file")" 2>/dev/null || true
  echo "Done."
}

case "$#" in
  0)
    install_hamster
    ;;
  1)
    case "$1" in
      uninstall|--uninstall)
        uninstall_hamster
        ;;
      *)
        install_hamster "$1"
        ;;
    esac
    ;;
  *)
    die "usage: $0 [INSTALL_DIR | uninstall]"
    ;;
esac
