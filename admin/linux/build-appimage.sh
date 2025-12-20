#! /bin/bash

# SPDX-FileCopyrightText: 2017 Nextcloud GmbH and Nextcloud contributors
# SPDX-License-Identifier: GPL-2.0-or-later

set -xe

export APPNAME=${APPNAME:-privata}
export EXECUTABLE_NAME=${EXECUTABLE_NAME:-privata}
export BUILD_UPDATER=${BUILD_UPDATER:-ON}
export BUILDNR=${BUILDNR:-0000}
export DESKTOP_CLIENT_ROOT=${DESKTOP_CLIENT_ROOT:-/home/a2kadmin/repos/Privata/}
export QT_BASE_DIR=${QT_BASE_DIR:-/usr}
export OPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR:-/usr/lib/x86_64-linux-gnu}
export VERSION_SUFFIX=${VERSION_SUFFIX:stable}
export APPIMAGE_DEBUG=${APPIMAGE_DEBUG:1} 

echo $DESKTOP_CLIENT_ROOT

# Set defaults
export SUFFIX=${PR_ID:=${DRONE_PULL_REQUEST:=stable}}
if [ $SUFFIX != "stable" ]; then
    SUFFIX="PR-$SUFFIX"
fi
if [ "$BUILD_UPDATER" != "OFF" ]; then
    BUILD_UPDATER=ON
fi

# Ensure we use gcc-11 on RHEL-like systems
if [ -e "/opt/rh/gcc-toolset-11/enable" ]; then
    source /opt/rh/gcc-toolset-11/enable
fi

rm -rf /app/*
mkdir -p /app

echo "current directory"$(pwd)
# Build client
rm -rf ${DESKTOP_CLIENT_ROOT}admin/linux/client-build
mkdir -p ${DESKTOP_CLIENT_ROOT}admin/linux/client-build
cd ${DESKTOP_CLIENT_ROOT}admin/linux/client-build

cmake \
    -G Ninja \
    -DCMAKE_PREFIX_PATH=${QT_BASE_DIR} \
    -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR} \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF \
     ${DESKTOP_CLIENT_ROOT}
   # -DCMAKE_SOURCE_DIR=${DESKTOP_CLIENT_ROOT} \
   # -DCMAKE_CURRENT_SOURCE_DIR=${DESKTOP_CLIENT_ROOT}
   # -DBUILD_UPDATER=$BUILD_UPDATER \
   # -DMIRALL_VERSION_BUILD=$BUILDNR \
   # -DMIRALL_VERSION_SUFFIX="$VERSION_SUFFIX" \
cmake --build . --target all
DESTDIR=/app cmake --install .

# Move stuff around:
cd /app

[ -d usr/lib/x86_64-linux-gnu ] && mv usr/lib/x86_64-linux-gnu/* usr/lib/

mkdir -p AppDir/usr/plugins
mv usr/lib64/*sync_vfs_suffix.so AppDir/usr/plugins || mv usr/lib/*sync_vfs_suffix.so AppDir/usr/plugins
mv usr/lib64/*sync_vfs_xattr.so  AppDir/usr/plugins || mv usr/lib/*sync_vfs_xattr.so  AppDir/usr/plugins

rm -rf usr/lib/cmake
rm -rf usr/include
rm -rf usr/mkspecs
rm -rf usr/lib/x86_64-linux-gnu/

# Don't bundle the explorer extensions as we can't do anything with them in the AppImage
rm -rf usr/share/caja-python/
rm -rf usr/share/nautilus-python/
rm -rf usr/share/nemo-python/
rm -rf AppDir/usr/share/${EXECUTABLE_NAME}

# The client-specific data dir also contains the translations, we want to have those in the AppImage.
mkdir -p AppDir/usr/share
mv usr/share/${EXECUTABLE_NAME} AppDir/usr/share/${EXECUTABLE_NAME}

# Move sync exclude to right location
mv /app/etc/*/sync-exclude.lst usr/bin/
rm -rf etc

# com.nextcloud.desktopclient.nextcloud.desktop
DESKTOP_FILE=$(ls /app/usr/share/applications/*.desktop)

# Use linuxdeploy to deploy
export APPIMAGE_NAME=linuxdeploy-x86_64.AppImage
wget -O ${APPIMAGE_NAME} --ca-directory=/etc/ssl/certs -c "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
chmod a+x ${APPIMAGE_NAME}
./${APPIMAGE_NAME} --appimage-extract
rm ./${APPIMAGE_NAME}
cp -r ./squashfs-root ./linuxdeploy-squashfs-root

export LD_LIBRARY_PATH=/app/usr/lib64:/app/usr/lib:${QT_BASE_DIR}/lib:/usr/local/lib/x86_64-linux-gnu:/usr/local/lib:/usr/local/lib64
./linuxdeploy-squashfs-root/AppRun --desktop-file=${DESKTOP_FILE} --icon-file=/app/usr/share/icons/hicolor/512x512/apps/Privata.png --executable=usr/bin/${EXECUTABLE_NAME} --appdir=AppDir

# Use linuxdeploy-plugin-qt to deploy qt dependencies
export APPIMAGE_NAME=linuxdeploy-plugin-qt-x86_64.AppImage
wget -O ${APPIMAGE_NAME} --ca-directory=/etc/ssl/certs -c "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
chmod a+x ${APPIMAGE_NAME}
./${APPIMAGE_NAME} --appimage-extract
rm ./${APPIMAGE_NAME}
cp -r ./squashfs-root ./linuxdeploy-plugin-qt-squashfs-root

export PATH=${QT_BASE_DIR}/bin:${PATH}
export QML_SOURCES_PATHS=${DESKTOP_CLIENT_ROOT}/src/gui
./linuxdeploy-plugin-qt-squashfs-root/AppRun --appdir=AppDir
#read -p "Press Enter to run the first apprun"

#use this code if you use SUSE to build the image ( sometimes it has issues downloading the runtimes )
./linuxdeploy-squashfs-root/AppRun --desktop-file=${DESKTOP_FILE} --icon-file=/app/usr/share/icons/hicolor/512x512/apps/Privata.png --executable=usr/bin/${EXECUTABLE_NAME}  --appdir=AppDir
./linuxdeploy-squashfs-root/plugins/linuxdeploy-plugin-appimage/usr/bin/appimagetool --runtime-file ${DESKTOP_CLIENT_ROOT}admin/linux/runtimes/runtime-x86_64 -n "AppDir"

#use this code if you use ubuntu (it can download the runtime without issues)

#./linuxdeploy-squashfs-root/AppRun --desktop-file=${DESKTOP_FILE} --icon-file=/app/usr/share/icons/hicolor/512x512/apps/Privata.png --executable=usr/bin/${EXECUTABLE_NAME}  --appdir=AppDir --output appimage

#read -p "Press Enter to continue... finished running the first apprun"

# Workaround issue #103 and #7231
export APPIMAGETOOL=appimagetool-x86_64.AppImage
wget -O ${APPIMAGETOOL} --ca-directory=/etc/ssl/certs -c https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
chmod a+x ${APPIMAGETOOL}
rm -rf ./squashfs-root
./${APPIMAGETOOL} --appimage-extract
rm ./${APPIMAGETOOL}
cp -r ./squashfs-root ./appimagetool-squashfs-root
rm -rf ./squashfs-root
APPIMAGE=$(ls *.AppImage)
./"${APPIMAGE}" --appimage-extract
rm ./"${APPIMAGE}"
#rm /squashfs-root/usr/lib/libglib-2.0.so.0
#read -p "Press Enter to continue... running appimagetool"

LD_LIBRARY_PATH="$PWD/appimagetool-squashfs-root/usr/lib":$LD_LIBRARY_PATH PATH="$PWD/appimagetool-squashfs-root/usr/bin":$PATH  appimagetool -n --runtime-file ${DESKTOP_CLIENT_ROOT}admin/linux/runtimes/runtime-x86_64  ./squashfs-root "${APPIMAGE}"

export CMAKE_VERSION=$(head -n 1 ${DESKTOP_CLIENT_ROOT}admin/linux/client-build/version.txt | xargs)
#move AppImage
export COMMIT=${GITHUB_SHA:=${DRONE_COMMIT}}
if [ ! -z "$COMMIT" ]
then
    export APPIMAGE_NAME="${EXECUTABLE_NAME}-${CMAKE_VERSION}-${SUFFIX}-${COMMIT}-x64.AppImage"
else
    export APPIMAGE_NAME="${EXECUTABLE_NAME}-${CMAKE_VERSION}-${SUFFIX}-x64.AppImage"
fi
mv *.AppImage ${DESKTOP_CLIENT_ROOT}$APPIMAGE_NAME

# tell GitHub Actions the name of our appimage
#if [ ! -z "$GITHUB_OUTPUT" ]; then
#  echo "AppImage name: ${APPIMAGE_NAME}"
#  echo "APPIMAGE_NAME=${APPIMAGE_NAME}" >> "$GITHUB_OUTPUT"
#fi
