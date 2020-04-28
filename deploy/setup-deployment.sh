
if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root" ; exit 1 ; fi

set -e -u

# add martchus.no-ip.biz ownstuff repository
if ! grep -q ownstuff /etc/pacman.conf; then
cat <<EOF >> /etc/pacman.conf
[ownstuff]
SigLevel = Optional TrustAll
Server = http://martchus.no-ip.biz/repo/arch/\$repo/os/\$arch
EOF
fi
pacman -Syy

# install make dependencies for extension
pacman -S --noconfirm --needed npm
npm install --global web-ext
pacman -S --noconfirm --needed hub

# install make dependencies for Linux build
pacman -S --noconfirm --needed asio gumbo-parser gtk3

# install make dependencies for Windows build
pacman -S --noconfirm --needed mingw-w64-gcc mingw-w64-cmake
aur-cache -i mingw-w64-gumbo-parser
aur-cache -i peldd-git
aur-cache -i msitools
