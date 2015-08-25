#!/usr/bin/env bash

# Utility function(s)
die() {
	echo "ERROR: $* (status $?)" 1>&2
	exit 1
}

# Setup common vars
PROJECT_DIR=$( cd -P "$( dirname "$0" )" && pwd )
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
	die "Can't find emsdk_env.sh (part of Emscripten SDK)!"
fi
echo "Setting up Emscripten..."
. emsdk_env.sh

# Create the build directory
if [ ! -d "$BUILD_DIR" ]; then
	echo "Creating build dir...: $BUILD_DIR"
	mkdir -p "$BUILD_DIR" || die "Can't create 'BUILD_DIR'"
fi
cd "$PROJECT_DIR" || die "Can't 'cd' to 'PROJECT_DIR'"
	
# Delete old build file(s)
echo "Cleaning..."
rm -rf pongbat* || die "Couldn't clean old files"

# Rebuild
echo "Building..."
emcc src/main.cpp -s USE_SDL=2 -O3 -std=c++11 \
	-o "$BUILD_DIR/pongbat.html" \
	--preload-file "Data/Fonts/FogSans.ttf" \
	--preload-file "Data/Images/BallBlue.png" \
	--preload-file "Data/Images/BallNoPlayer.png" \
	--preload-file "Data/Images/BallRed.png" \
	--preload-file "Data/Images/PaddleBlue.png" \
	--preload-file "Data/Images/PaddleRed.png" \
	--preload-file "Data/Images/BackgroundTile.png" \
	--preload-file "Data/Images/BackgroundPaddleBar.png" \
	--preload-file "Data/Images/PowerupPlain.png" \
	--preload-file "Data/Images/PowerupHealth.png" \
	|| die "Compile failed"

# Install
cd "$BUILD_DIR" || die "Can't 'cd' to 'BUILD_DIR'"
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
