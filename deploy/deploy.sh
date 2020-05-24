#!/bin/bash
set -e -u

function attach_github_release() {
  local version="$1"
  local filename="$2"
  export GITHUB_USER GITHUB_PASSWORD &&
    (hub release create -m "Version $version" "$version" 2> /dev/null || true) &&
    (hub release edit -m "" -a "$filename" "$version")
}

function deploy_extension() {
  BOOKMARK_HAMSTER_XPI=$(realpath -m "webext/web-ext-artifacts/bookmark_hamster-${WEBEXT_VERSION}-an+fx.xpi")
  if [ ! -f "$BOOKMARK_HAMSTER_XPI" ]; then
    echo "Building browser extension"
    pushd webext > /dev/null

    # build extension
    sed -i "s/^\(.*\"version\":\s*\"\).*\(\",.*\)/\1$WEBEXT_VERSION\2/" manifest.json
    web-ext build --overwrite-dest

    # sign extension
    web-ext sign --channel unlisted --api-key ${MOZILLA_API_KEY} --api-secret ${MOZILLA_API_SECRET}

    # attach .xpi to GitHub release
    attach_github_release "$WEBEXT_VERSION" "$BOOKMARK_HAMSTER_XPI"
    popd > /dev/null
  fi
}

function deploy_linux64() {
  BUILD_DIR=$(realpath _build_linux64)
  PACKAGE_PATH="$BUILD_DIR/bookmark-hamster-${NATIVE_VERSION}-linux64.run"
  if [ ! -f "$PACKAGE_PATH" ]; then
    echo "Building generic Linux installer"
    mkdir -p "$BUILD_DIR"
    pushd "$BUILD_DIR" > /dev/null

    # compile Linux build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_WEBRECORDER=true -DCMAKE_INSTALL_PREFIX=dist
    cmake --build .
    cmake --install .

    cp "$DEPLOY_DIR/makeself-setup.sh" "dist/setup.sh"
    cp "$DEPLOY_DIR/hamster-mozilla.json" "dist"
    makeself "dist" "$PACKAGE_PATH" "Bookmark Hamster" "./setup.sh"

    # attach package to GitHub release
    attach_github_release "$NATIVE_VERSION_TAG" "$PACKAGE_PATH"
    popd > /dev/null
  fi
}

function deploy_win64() {
  BUILD_DIR=$(realpath _build_win64)
  PACKAGE_PATH="$BUILD_DIR/bookmark-hamster-${NATIVE_VERSION}-win64.msi"
  if [ ! -f "$PACKAGE_PATH" ]; then
    echo "Building Windows version"
    mkdir -p "$BUILD_DIR"
    pushd "$BUILD_DIR" > /dev/null

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
    attach_github_release "$NATIVE_VERSION_TAG" "$PACKAGE_PATH"
    popd > /dev/null
  fi
}


if [[ $(id -u) -eq 0 ]] ; then echo "Please do not run as root" ; exit 1 ; fi

# check credentials
if [ ! -f _credentials.conf ]; then
  cat <<EOF > _credentials.conf
GITHUB_USER=""
GITHUB_PASSWORD=""
MOZILLA_API_KEY=""
MOZILLA_API_SECRET=""
EOF
fi
source _credentials.conf
if [ -z "$GITHUB_USER" ] || \
   [ -z "$GITHUB_PASSWORD" ] || \
   [ -z "$MOZILLA_API_KEY" ] || \
   [ -z "$MOZILLA_API_SECRET" ];  then
  echo "Please complete the API access credentials in _credentials.conf"
  exit
fi

# cd to main directory
DEPLOY_DIR="$(realpath $(dirname $0))"
cd "${DEPLOY_DIR}/.."

# tags should be major.minor.build (e.g. 1.12.15)
WEBEXT_VERSION=$(git describe --tags | sed "s/-.*//")

# only update native builds for build number 0
NATIVE_VERSION=$(echo $WEBEXT_VERSION | sed "s/\.[^.]*$//")
NATIVE_VERSION_TAG="${NATIVE_VERSION}.0"

deploy_extension

if [ "${WEBEXT_VERSION}" == "$NATIVE_VERSION_TAG" ]; then
  deploy_linux64
  deploy_win64
fi

echo "DONE."

