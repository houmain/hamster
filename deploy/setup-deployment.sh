#!/usr/bin/bash
set -e -u

if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root" ; exit 1 ; fi

# add martchus.no-ip.biz ownstuff repository
if ! grep -q ownstuff /etc/pacman.conf; then
cat <<EOF >> /etc/pacman.conf
[ownstuff]
SigLevel = Optional TrustAll
Server = http://martchus.no-ip.biz/repo/arch/\$repo/os/\$arch
EOF
fi
pacman -Syyu

# install make dependencies for extension
pacman -S --noconfirm --needed npm
npm list --global web-ext || npm install --global web-ext
pacman -S --noconfirm --needed hub

# install make dependencies for Linux build
pacman -S --noconfirm --needed cmake asio gtk3
aur-cache -i makeself

# install make dependencies for Windows build
pacman -S --noconfirm --needed mingw-w64-gcc mingw-w64-cmake mingw-w64-openssl
aur-cache -i peldd-git
aur-cache -i msitools
