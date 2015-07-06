#!/usr/bin/env bash

# Setup common vars
PROJECT_DIR=$( cd -P "$( dirname "$0" )/.." && pwd )
BUILD_DIR="$PROJECT_DIR/build/emscripten"

# Setup shell for Emscripten SDK use:
if ! type -p emsdk_env.sh > /dev/null ; then
	echo "Can't find emsdk_env.sh (part of Emscripten SDK)!"
	exit 1
fi
echo "Setting up Emscripten..."
. emsdk_env.sh

# Create the build directory, then cd to it
if [ ! -d "$BUILD_DIR" ]; then
	echo "Creating build dir...: $BUILD_DIR"
	mkdir -p "$BUILD_DIR"
fi
cd "$BUILD_DIR"

# Delete old build file(s)
echo "Cleaning..."
rm -rf *

# Rebuild
echo "Building..."
emcc ../../src/main.cpp -s USE_SDL=2 -O3 -o pongbat.html

# Install
echo "Installing..."
if [ -d "$PONGBAT_INSTALL_DIR" ]; then
	cp -rv * "$PONGBAT_INSTALL_DIR/"
else
	echo "... Nowhere to install to!  Set 'PONGBAT_INSTALL_DIR' in env to an already-mkdir'ed install location."
fi

echo "Script is complete ( $0 )"
