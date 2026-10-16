#!/usr/bin/env bash
# Generates the build-time artifacts that the Android (cross-compiled) build
# cannot produce itself: the host menu font header and the compiled SPIR-V for
# the Vulkan GE.  Both are produced with host tools so no target executable
# ever has to run during the Android build.
#
# Usage: gen-android-assets.sh <generated-dir>
# The directory ends up on the include path of the native build, so it must be
# the directory that contains host_font_5x7.hpp and vulkan/ge_spv.hpp.
set -euo pipefail

GEN="${1:?generated dir required}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="$(mktemp -d)"
trap 'rm -rf "${TOOLS}"' EXIT

mkdir -p "${GEN}/vulkan"

echo ">> packing host menu font"
g++ -std=c++20 -O2 -o "${TOOLS}/pack_font5x7" "${ROOT}/lcs/host/pack_font5x7.cpp"
"${TOOLS}/pack_font5x7" "${ROOT}/lcs/host/font5x7.txt" "${GEN}/host_font_5x7.hpp"

echo ">> compiling Vulkan GE shaders to SPIR-V"
GLSLANG="$(command -v glslangValidator || true)"
if [ -z "${GLSLANG}" ]; then
  echo "glslangValidator not found; install glslang-tools" >&2
  exit 1
fi
# Reuse the project's own embed_spirv.cmake so the generated header matches the
# desktop build exactly.
cmake -DGLSLANG="${GLSLANG}" \
      -DVERT="${ROOT}/lcs/host/vulkan/ge.vert" \
      -DFRAG="${ROOT}/lcs/host/vulkan/ge.frag" \
      -DPRESENT_VERT="${ROOT}/lcs/host/vulkan/present.vert" \
      -DPRESENT_FRAG="${ROOT}/lcs/host/vulkan/present.frag" \
      -DOUT="${GEN}/vulkan/ge_spv.hpp" \
      -P "${ROOT}/lcs/host/vulkan/embed_spirv.cmake"

echo ">> generated assets in ${GEN}"
find "${GEN}" -type f | sed "s|${GEN}/|   |"