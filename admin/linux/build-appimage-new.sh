#!/bin/bash

# SPDX-FileCopyrightText: 2017 Nextcloud GmbH and Nextcloud contributors
# SPDX-License-Identifier: GPL-2.0-or-later

set -xe

###############################################################################
# CONFIGURATION - Adjust these paths to match your environment
###############################################################################
export APPNAME=${APPNAME:-privata}
export EXECUTABLE_NAME=${EXECUTABLE_NAME:-privata}
export BUILD_UPDATER=${BUILD_UPDATER:-ON}
export BUILDNR=${BUILDNR:-0000}
export DESKTOP_CLIENT_ROOT=${DESKTOP_CLIENT_ROOT:-/home/a2kadmin/repos/Privata/}
export VERSION_SUFFIX=${VERSION_SUFFIX:-stable}
export APPIMAGE_DEBUG=${APPIMAGE_DEBUG:-1}

# Qt path - installed via Qt Online Installer (must be >= 6.5.0)i
export QT_BASE_DIR=${QT_BASE_DIR:-/home/a2kadmin/Qt/6.10.2/gcc_64}
export QT_PATH=${QT_BASE_DIR}
export Qt6_DIR=${QT_PATH}/lib/cmake/Qt6
export Qt6Core_DIR=${QT_PATH}/lib/cmake/Qt6Core

# OpenSSL
export OPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR:-/usr/lib/x86_64-linux-gnu}

# Make sure Qt and locally built KF6/ECM libs are discoverable
export PATH=${QT_PATH}/bin:${PATH}
export LD_LIBRARY_PATH=${QT_PATH}/lib:/usr/local/lib/x86_64-linux-gnu:/usr/local/lib:/usr/local/lib64:${LD_LIBRARY_PATH}
export PKG_CONFIG_PATH=${QT_PATH}/lib/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH}

###############################################################################
# VERIFY PREREQUISITES
###############################################################################
echo "=== Checking prerequisites ==="

# Check Qt version
if [ ! -d "$QT_PATH" ]; then
    echo "ERROR: Qt not found at $QT_PATH"
    echo "Install Qt >= 6.5.0 via the Qt Online Installer: https://www.qt.io/download-qt-installer"
    exit 1
fi

QT_VERSION_FOUND=$(${QT_PATH}/bin/qmake --version 2>/dev/null | grep -oP 'Qt version \K[0-9.]+' || echo "unknown")
echo "Found Qt version: $QT_VERSION_FOUND at $QT_PATH"

# Check ECM
if ! find /usr/local -name "ECMConfig.cmake" 2>/dev/null | grep -q .; then
    echo "ERROR: extra-cmake-modules not found. Run install-prerequisites-ubuntu2404.sh first."
    exit 1
fi

# Check KF6Archive
if ! find /usr/local -name "KF6ArchiveConfig.cmake" 2>/dev/null | grep -q .; then
    echo "ERROR: KF6Archive not found. Run install-prerequisites-ubuntu2404.sh first."
    exit 1
fi

# Check qtkeychain
if ! find /usr/local -name "Qt6KeychainConfig.cmake" 2>/dev/null | grep -q .; then
    echo "ERROR: qtkeychain not found. Run install-prerequisites-ubuntu2404.sh first."
    exit 1
fi

# Check ninja
if ! command -v ninja &> /dev/null; then
    echo "ERROR: ninja not found. Install with: sudo apt install ninja-build"
    exit 1
fi

echo "=== Prerequisites OK ==="

###############################################################################
# SET BUILD VARIABLES
###############################################################################
echo $DESKTOP_CLIENT_ROOT

# Set defaults
export SUFFIX=${PR_ID:=${DRONE_PULL_REQUEST:=stable}}
if [ "$SUFFIX" != "stable" ]; then
    SUFFIX="PR-$SUFFIX"
fi
if [ "$BUILD_UPDATER" != "OFF" ]; then
    BUILD_UPDATER=ON
fi

###############################################################################
# BUILD CLIENT
###############################################################################
rm -rf /app/*
mkdir -p /app

echo "current directory: $(pwd)"

rm -rf ${DESKTOP_CLIENT_ROOT}admin/linux/client-build
mkdir -p ${DESKTOP_CLIENT_ROOT}admin/linux/client-build
cd ${DESKTOP_CLIENT_ROOT}admin/linux/client-build

# CMAKE_PREFIX_PATH: Qt first (so it takes priority over system Qt), then /usr/local for KF6/ECM/qtkeychain
cmake \
    -G Ninja \
    -DCMAKE_PREFIX_PATH="${QT_BASE_DIR};${QT_BASE_DIR}/lib/cmake;/usr/local" \
    -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR} \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF \
    -DQt6_DIR=${QT_PATH}/lib/cmake/Qt6 \
    -DQt6Core_DIR=${QT_PATH}/lib/cmake/Qt6Core \
    ${DESKTOP_CLIENT_ROOT}
    # -DBUILD_UPDATER=$BUILD_UPDATER \
    # -DMIRALL_VERSION_BUILD=$BUILDNR \
    # -DMIRALL_VERSION_SUFFIX="$VERSION_SUFFIX" \

cmake --build . --target all
DESTDIR=/app cmake --install .

###############################################################################
# PACKAGE INTO APPIMAGE
###############################################################################
cd /app

[ -d usr/lib/x86_64-linux-gnu ] && mv usr/lib/x86_64-linux-gnu/* usr/lib/

mkdir -p AppDir/usr/plugins
mv usr/lib64/*sync_vfs_suffix.so AppDir/usr/plugins 2>/dev/null || mv usr/lib/*sync_vfs_suffix.so AppDir/usr/plugins 2>/dev/null || true
mv usr/lib64/*sync_vfs_xattr.so  AppDir/usr/plugins 2>/dev/null || mv usr/lib/*sync_vfs_xattr.so  AppDir/usr/plugins 2>/dev/null || true

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
mv /app/etc/*/sync-exclude.lst usr/bin/ 2>/dev/null || true
rm -rf etc

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

# Temporarily hide unused Qt SQL plugins so linuxdeploy-plugin-qt skips them
mkdir -p /tmp/qt-sqldrivers-disabled
find ${QT_BASE_DIR}/plugins/sqldrivers/ -maxdepth 1 -name '*.so' ! -name 'libqsqlite.so' \
    -exec mv {} /tmp/qt-sqldrivers-disabled/ \;

# Ensure plugins are always restored, even if the script fails
restore_sql_plugins() {
    if [ -d /tmp/qt-sqldrivers-disabled ]; then
        mv /tmp/qt-sqldrivers-disabled/*.so ${QT_BASE_DIR}/plugins/sqldrivers/ 2>/dev/null || true
        rm -rf /tmp/qt-sqldrivers-disabled
    fi
}
trap restore_sql_plugins EXIT

./linuxdeploy-plugin-qt-squashfs-root/AppRun --appdir=AppDir

# Restore now (trap will also catch failures)
restore_sql_plugins

#use this code if you use SUSE to build the image ( sometimes it has issues downloading the runtimes )
#./linuxdeploy-squashfs-root/AppRun --desktop-file=${DESKTOP_FILE} --icon-file=/app/usr/share/icons/hicolor/512x512/apps/Privata.png --executable=usr/bin/${EXECUTABLE_NAME}  --appdir=AppDir
#./linuxdeploy-squashfs-root/plugins/linuxdeploy-plugin-appimage/usr/bin/appimagetool --runtime-file ${DESKTOP_CLIENT_ROOT}admin/linux/runtimes/runtime-x86_64 -n "AppDir"

#use this code if you use ubuntu (it can download the runtime without issues)
./linuxdeploy-squashfs-root/AppRun --desktop-file=${DESKTOP_FILE} --icon-file=/app/usr/share/icons/hicolor/512x512/apps/Privata.png --executable=usr/bin/${EXECUTABLE_NAME}  --appdir=AppDir --output appimage

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
