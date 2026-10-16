#!/usr/bin/env bash
# Builds the Android-only native dependencies of LCSNative (SDL2 and FFmpeg)
# with the Android NDK, for one ABI at a time, and stages them under
# android/dist/<abi>/.  Gradle's externalNativeBuild then compiles
# LCSNative against the staged headers/libraries.
#
# Usage: build-deps.sh <abi> <ndk-dir> [out-dir]
#   abi: arm64-v8a | armeabi-v7a | x86_64 | x86
set -euo pipefail

ABI="${1:?abi required}"
NDK="${2:?ndk dir required}"
OUT="${3:-$(cd "$(dirname "$0")" && pwd)/dist}"

case "$ABI" in
  arm64-v8a)   ARCH=arm64;   API=21; TRIPLE=aarch64-linux-android ;;
  armeabi-v7a) ARCH=armeabi; API=21; TRIPLE=armv7a-linux-androideabi ;;
  x86_64)      ARCH=x86_64;  API=21; TRIPLE=x86_64-linux-android ;;
  x86)         ARCH=x86;     API=21; TRIPLE=i686-linux-android ;;
  *) echo "unknown abi: $ABI" >&2; exit 1 ;;
esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Build trees live outside the staged prefix so the CI cache only keeps the
# artifacts that Gradle actually consumes.
WORK="${ROOT}/android/.deps-work/${ABI}"
STAGE="${OUT}/${ABI}"
SRC="${OUT}/src"

SDL_VERSION="${SDL_VERSION:-2.30.12}"
FFMPEG_VERSION="${FFMPEG_VERSION:-7.1.1}"

TOOLCHAIN="${NDK}/build/cmake/android.toolchain.cmake"
HOST_TAG="linux-x86_64"

mkdir -p "${SRC}" "${WORK}" "${STAGE}"

# ---------------------------------------------------------------- sources ---
if [ ! -d "${SRC}/SDL2-${SDL_VERSION}" ]; then
  echo ">> fetching SDL2 ${SDL_VERSION}"
  curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL2-${SDL_VERSION}.tar.gz" \
    | tar -xz -C "${SRC}"
fi
if [ ! -d "${SRC}/ffmpeg-${FFMPEG_VERSION}" ]; then
  echo ">> fetching FFmpeg ${FFMPEG_VERSION}"
  curl -fsSL "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" \
    | tar -xJ -C "${SRC}"
fi

# ------------------------------------------------------------------- SDL2 ---
# Build SDL2 as libSDL2.so: this is the library SDLActivity loads first, and it
# keeps the SDL symbol space separate from our libmain.so (avoids clashing on
# main/JNI_OnLoad and lets SDL ship its own TLS/JNI plumbing).
echo ">> building SDL2 for ${ABI}"
cmake -S "${SRC}/SDL2-${SDL_VERSION}" -B "${WORK}/sdl" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
  -DANDROID_ABI="${ABI}" \
  -DANDROID_PLATFORM="android-${API}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSDL_SHARED=ON \
  -DSDL_STATIC=OFF \
  -DSDL_TEST_LIBRARY=OFF \
  -DSDL_VULKAN=ON \
  -DSDL_VIDEO_OPENGLES=ON \
  -DSDL_VIDEO_OPENGLES2=ON \
  -DSDL_AUDIO=ON \
  -DSDL_CAMERA=OFF \
  -DSDL_HIDAPI=OFF \
  -DSDL_RENDER=ON \
  -DANDROID_STL=c++_shared \
  -DCMAKE_INSTALL_PREFIX="${STAGE}"
cmake --build "${WORK}/sdl" --parallel "$(nproc)"
# install(EXPORT) writes include/SDL2, lib/libSDL2.so and the CMake package files.
( cd "${WORK}/sdl" && DESTDIR= cmake --install . >/dev/null )

# ---------------------------------------------------------------- FFmpeg ---
# Only the decode path LCS uses is enabled: the guest renders its own video
# frames through FFmpeg's demuxers/decoders and converts them with swscale.
echo ">> building FFmpeg for ${ABI}"
FF_SRC="${SRC}/ffmpeg-${FFMPEG_VERSION}"
FF_BUILD="${WORK}/ffmpeg"
mkdir -p "${FF_BUILD}"
pushd "${FF_BUILD}" >/dev/null
"${FF_SRC}/configure" \
  --prefix="${STAGE}" \
  --target-os=android \
  --arch="${ARCH}" \
  --enable-cross-compile \
  --cc="${NDK}/toolchains/llvm/prebuilt/${HOST_TAG}/bin/${TRIPLE}${API}-clang" \
  --cxx="${NDK}/toolchains/llvm/prebuilt/${HOST_TAG}/bin/${TRIPLE}${API}-clang++" \
  --ar="${NDK}/toolchains/llvm/prebuilt/${HOST_TAG}/bin/llvm-ar" \
  --ranlib="${NDK}/toolchains/llvm/prebuilt/${HOST_TAG}/bin/llvm-ranlib" \
  --strip="${NDK}/toolchains/llvm/prebuilt/${HOST_TAG}/bin/llvm-strip" \
  --nm="${NDK}/toolchains/llvm/prebuilt/${HOST_TAG}/bin/llvm-nm" \
  --pkg-config=pkg-config \
  --disable-programs \
  --disable-doc \
  --disable-avdevice \
  --disable-postproc \
  --disable-debug \
  --disable-static \
  --enable-shared \
  --enable-swscale \
  --enable-swresample \
  --enable-avformat \
  --enable-avcodec \
  --enable-avutil \
  --disable-everything \
  --enable-decoder=mpeg4 \
  --enable-decoder=h263 \
  --enable-decoder=msmpeg4v3 \
  --enable-decoder=msmpeg4 \
  --enable-decoder=mjpeg \
  --enable-decoder=theora \
  --enable-decoder=vp8 \
  --enable-decoder=vp9 \
  --enable-decoder=atrac3p \
  --enable-decoder=atrac3 \
  --enable-decoder=mp3 \
  --enable-decoder=aac \
  --enable-demuxer=matroska \
  --enable-demuxer=avi \
  --enable-demuxer=mpegts \
  --enable-demuxer=h264 \
  --enable-demuxer=mov \
  --enable-protocol=file \
  --enable-parser=mpeg4video \
  --enable-parser=mpegaudio \
  --enable-parser=aac \
  >configure.log 2>&1 || { tail -40 configure.log; exit 1; }
make -j"$(nproc)"
make install
popd >/dev/null

# ------------------------------------------------------------------ stage ---
echo ">> staged dependencies for ${ABI} in ${STAGE}"
find "${STAGE}/lib" -name '*.so' -o -name '*.a' | sed "s|${STAGE}/||" | sed 's/^/   /'