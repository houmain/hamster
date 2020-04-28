#!/bin/bash

set -e -u

if [ ! -f _credentials.conf ]; then
cat <<EOF > _credentials.conf
GITHUB_USER=""
GITHUB_PASSWORD=""
MOZILLA_API_KEY=""
MOZILLA_API_SECRET=""
EOF
fi
source _credentials.conf
if [ -z "$MOZILLA_API_KEY" ] || [ -z "$MOZILLA_API_SECRET" ];  then
  echo "Please complete the API access credentials in _credentials.conf"
  exit
fi

# cd to main directory
DEPLOY_DIR=$(pwd)
cd ..

# update source from origin
git submodule update --init --recursive
git pull --recurse-submodules

# tags should be major.minor.build (e.g. 1.12.15)
WEBEXT_VERSION=$(git describe --tags | sed "s/-.*//")

BOOKMARK_HAMSTER_XPI="webext/web-ext-artifacts/bookmark_hamster-${WEBEXT_VERSION}-an+fx.xpi"
if [ ! -f "$BOOKMARK_HAMSTER_XPI" ]; then
  echo "Building browser extension"
  pushd webext

  # build extension
  sed -i "s/^\(.*\"version\":\s*\"\).*\(\",.*\)/\1$WEBEXT_VERSION\2/" manifest.json
  web-ext build --overwrite-dest

  # sign extension
  web-ext sign --channel unlisted --api-key ${MOZILLA_API_KEY} --api-secret ${MOZILLA_API_SECRET}

  # attach .xpi to GitHub release
  export GITHUB_USER GITHUB_PASSWORD && \
  hub release create -m "Version $WEBEXT_VERSION" -a "$BOOKMARK_HAMSTER_XPI" "$WEBEXT_VERSION"
  popd
fi

# only update native builds for build number 0
NATIVE_VERSION=$(echo $WEBEXT_VERSION | sed "s/\.[^.]*//")
if [ "${WEBEXT_VERSION}" == "${NATIVE_VERSION}.0" ]; then

  BUILD_DIR=$(realpath _build_arch)
  PACKAGE_PATH="$BUILD_DIR/bookmark-hamster-${NATIVE_VERSION}-1-x86_64.pkg.tar.xz"
  if [ ! -f "$PACKAGE_PATH" ]; then
    echo "Building Arch Linux version"
    mkdir -p "$BUILD_DIR"
    pushd "$BUILD_DIR"

    cp "$DEPLOY_DIR/"{PKGBUILD,hamster-*.json} "$BUILD_DIR"
    cp "$DEPLOY_DIR/PKGBUILD" "$BUILD_DIR"
    sed -i "s/^\(\s*pkgver=\s*\).*/\1$NATIVE_VERSION/" PKGBUILD
    makepkg

    # attach package to GitHub release
    export GITHUB_USER GITHUB_PASSWORD && \
    hub release edit -m "" -a "$PACKAGE_PATH" "$WEBEXT_VERSION"
    popd
  fi

  BUILD_DIR=$(realpath _build_mingw)
  PACKAGE_PATH="$BUILD_DIR/bookmark_hamster_win64-${NATIVE_VERSION}.msi"
  if [ ! -f "$PACKAGE_PATH" ]; then
    echo "Building Windows version"
    mkdir -p "$BUILD_DIR"
    pushd "$BUILD_DIR"

    # compile windows build
    x86_64-w64-mingw32-cmake .. -DBUILD_WEBRECORDER=true -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/dist"
    cmake --build . --config Release
    cmake --install . --config Release

    # build .msi installer
    peldd -a -t --ignore-errors dist/*.exe | xargs -r cp -t dist &> /dev/null || true
    /usr/x86_64-w64-mingw32/bin/strip --strip-all dist/*.exe
    find dist | wixl-heat -p dist/ --var var.SourceDir --directory-ref=INSTALLFOLDER --component-group Complete > hamster-files.wxs
    wixl -a x64 -D VERSION="${NATIVE_VERSION}" -D SourceDir=dist -o "$PACKAGE_PATH" "$DEPLOY_DIR/hamster.wxs" hamster-files.wxs

    # attach .msi to GitHub release
    export GITHUB_USER GITHUB_PASSWORD && \
    hub release edit -m "" -a "$PACKAGE_PATH" "$WEBEXT_VERSION"
    popd
  fi
fi
