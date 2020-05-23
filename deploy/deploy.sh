#!/bin/bash
set -e -u

if [[ $(id -u) -eq 0 ]] ; then echo "Please do not run as root" ; exit 1 ; fi

#if [ -z ${GITHUB_USER+x} ] || \
#   [ -z ${GITHUB_PASSWORD+x} ] || \
#   [ -z ${MOZILLA_API_KEY+x} ] || \
#   [ -z ${MOZILLA_API_SECRET+x} ];  then
#  echo "Please provide the API access credentials"
#  exit 1
#fi

# cd to main directory
DEPLOY_DIR="$(realpath $(dirname $0))"
cd "${DEPLOY_DIR}/.."

function attach_release() {
  export GITHUB_USER GITHUB_PASSWORD &&
    (hub release create -m "Version $1" "$1" 2> /dev/null || true) &&
    (hub release edit -m "" -a "$2" "$1")
}

# tags should be major.minor.build (e.g. 1.12.15)
WEBEXT_VERSION=$(git describe --tags | sed "s/-.*//")

BOOKMARK_HAMSTER_XPI=$(realpath -m "webext/web-ext-artifacts/bookmark_hamster-${WEBEXT_VERSION}-an+fx.xpi")
#if [ ! -f "$BOOKMARK_HAMSTER_XPI" ]; then
#  echo "Building browser extension"
#  pushd webext
#
#  # build extension
#  sed -i "s/^\(.*\"version\":\s*\"\).*\(\",.*\)/\1$WEBEXT_VERSION\2/" manifest.json
#  web-ext build --overwrite-dest
#
#  # sign extension
#  web-ext sign --channel unlisted --api-key ${MOZILLA_API_KEY} --api-secret ${MOZILLA_API_SECRET}
#
#  # attach .xpi to GitHub release
#  attach_release "$WEBEXT_VERSION" "$BOOKMARK_HAMSTER_XPI"
#  popd
#fi

# only update native builds for build number 0
NATIVE_VERSION=$(echo $WEBEXT_VERSION | sed "s/\.[^.]*$//")
NATIVE_VERSION_TAG="${NATIVE_VERSION}.0"
if [ "${WEBEXT_VERSION}" == "$NATIVE_VERSION_TAG" ]; then

  BUILD_DIR=$(realpath _build_linux64)
  PACKAGE_PATH="$BUILD_DIR/bookmark-hamster-${NATIVE_VERSION}-linux64.run"
  if [ ! -f "$PACKAGE_PATH" ]; then
    echo "Building generic Linux installer"
    mkdir -p "$BUILD_DIR"
    pushd "$BUILD_DIR"

    # compile Linux build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_WEBRECORDER=true -DCMAKE_INSTALL_PREFIX=dist
    cmake --build .
    cmake --install .

    cp "$DEPLOY_DIR/makeself-setup.sh" "dist/setup.sh"
    cp "$DEPLOY_DIR/hamster-mozilla.json" "dist"
    makeself "dist" "$PACKAGE_PATH" "Bookmark Hamster" "./setup.sh"

    # attach package to GitHub release
    #attach_release "$NATIVE_VERSION_TAG" "$PACKAGE_PATH"
    popd
  fi

  BUILD_DIR=$(realpath _build_win64)
  PACKAGE_PATH="$BUILD_DIR/bookmark-hamster-${NATIVE_VERSION}-win64.msi"
  if [ ! -f "$PACKAGE_PATH" ]; then
    echo "Building Windows version"
    mkdir -p "$BUILD_DIR"
    pushd "$BUILD_DIR"

    # compile Windows build
    x86_64-w64-mingw32-cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_WEBRECORDER=true -DCMAKE_INSTALL_PREFIX=dist
    cmake --build .
    cmake --install .

    # build .msi installer
    peldd -a -t --ignore-errors dist/*.exe | xargs -r cp -t dist &> /dev/null || true
    /usr/x86_64-w64-mingw32/bin/strip --strip-all dist/*.exe
    find dist | wixl-heat -p dist/ --var var.SourceDir --directory-ref=INSTALLFOLDER --component-group Complete > hamster-files.wxs
    wixl -a x64 -D VERSION="${NATIVE_VERSION}" -D SourceDir=dist -o "$PACKAGE_PATH" "$DEPLOY_DIR/hamster.wxs" hamster-files.wxs

    # attach .msi to GitHub release
    attach_release "$NATIVE_VERSION_TAG" "$PACKAGE_PATH"
    popd
  fi
fi

echo "DONE."

