#!/usr/bin/env bash
# Copies SDL2's Java glue into android/dist/sdljava so Gradle can compile it as
# part of the app without vendoring SDL sources into this repository.
#
# Usage: prepare-sdl-java.sh <sdl2-source-dir>
set -euo pipefail

SDL_SRC="${1:?SDL2 source dir required}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ROOT}/android/dist/sdljava/org/libsdl/app"

if [ ! -d "${SDL_SRC}/android-project/app/src/main/java/org/libsdl/app" ]; then
  echo "cannot find SDL2 android-project sources in ${SDL_SRC}" >&2
  exit 1
fi

mkdir -p "${OUT}"
cp "${SDL_SRC}"/android-project/app/src/main/java/org/libsdl/app/*.java "${OUT}/"
echo ">> staged SDL2 java glue: $(ls -1 "${OUT}" | wc -l) files"