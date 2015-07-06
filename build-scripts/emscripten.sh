#!/usr/bin/env bash

# Setup common vars
PROJECT_DIR=$( cd -P "$( dirname "$0" )/.." && pwd )
BUILD_DIR="$PROJECT_DIR/build/emscripten"

echo "BUILD_DIR: $BUILD_DIR"

# Setup + log install-dir details
if [ "$1" != "" ]; then
	PONGBAT_INSTALL_DIR="$1"
fi
if [ "$PONGBAT_INSTALL_DIR" != "" ]; then
	echo "Install path: $PONGBAT_INSTALL_DIR"
fi

if ! type -p scp > /dev/null; then
	echo "Warning: Can't find 'scp'.  Installation to remote directories will be disabled"
	CP=cp
else
	CP=scp
fi

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
if ! emcc ../../src/main.cpp -s USE_SDL=2 -O3 -o pongbat.html; then
	echo "Script exiting, as compile failed"
	exit 1
fi

# Install
if [ "$PONGBAT_INSTALL_DIR" != "" ]; then
	echo "Installing..."
	if echo "$PONGBAT_INSTALL_DIR" | grep -q ":" > /dev/null; then
		"$CP" -r * "$PONGBAT_INSTALL_DIR/"
	else
		# Use verbose logging when a local directory is specified
		"$CP" -rv * "$PONGBAT_INSTALL_DIR/"
	fi
else
	echo "Ignoring install, as neither the env-var PONGBAT_INSTALL_DIR, nor the build-script's 1st arg, were set."
fi

echo "Script is complete ( $0 )"
