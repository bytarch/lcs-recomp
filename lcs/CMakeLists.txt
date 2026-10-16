set(FFMPEG_LIBS avcodec avformat avutil swresample swscale)
set(FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg)

file(GLOB HOST_SOURCES CONFIGURE_DEPENDS host/*.cpp)
list(REMOVE_ITEM HOST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/host/pack_font5x7.cpp)
if(ANDROID)
    # SDLActivity dlopen()s libmain.so and calls SDL_main() in it, so the
    # executable becomes a shared library that keeps the LCSNative target name.
    add_library(LCSNative SHARED ${HOST_SOURCES})
    set_target_properties(LCSNative PROPERTIES OUTPUT_NAME main)
else()
    add_executable(LCSNative ${HOST_SOURCES})
endif()
target_include_directories(LCSNative PRIVATE host)
if(WIN32)
    target_include_directories(LCSNative PRIVATE ${FFMPEG_DIR}/include)
endif()
target_link_libraries(LCSNative PRIVATE psprecomp_core)

if(MSVC)
    target_compile_options(LCSNative PRIVATE /W4 /permissive- /EHsc)
    target_link_options(LCSNative PRIVATE /STACK:67108864)
endif()

if(ANDROID)
    # Android builds libmain.so, which SDLActivity loads and calls SDL_main in.
    # Every host source compiles unchanged; only the packaging differs.  The
    # NDK cross build cannot run target executables, so the two build-time
    # artifacts (host font header, SPIR-V header) are pre-generated with host
    # tools by android/gen-android-assets.sh and passed in here.
    if(NOT DEFINED LCS_ANDROID_DEPS_DIR)
        # Gradle drives the native build for all ABIs with one set of arguments,
        # so the per-ABI staging directory comes from the NDK toolchain instead.
        set(LCS_ANDROID_DEPS_DIR
            ${CMAKE_SOURCE_DIR}/android/dist/${CMAKE_ANDROID_ARCH_ABI})
    endif()
    if(NOT DEFINED LCS_ANDROID_PREBUILT_DIR)
        set(LCS_ANDROID_PREBUILT_DIR ${CMAKE_SOURCE_DIR}/android/prebuilt)
    endif()
    target_include_directories(LCSNative BEFORE PRIVATE
        "${LCS_ANDROID_PREBUILT_DIR}"
        "${LCS_ANDROID_DEPS_DIR}/include")
    target_include_directories(LCSNative PRIVATE "${LCS_ANDROID_DEPS_DIR}/include/SDL2")
    target_compile_definitions(LCSNative PRIVATE LCS_VULKAN_GE_BACKEND=1)
    # SDL2 and FFmpeg are shipped as shared libraries in jniLibs.
    target_link_libraries(LCSNative PRIVATE
        "${LCS_ANDROID_DEPS_DIR}/lib/libSDL2.so"
        "${LCS_ANDROID_DEPS_DIR}/lib/libavcodec.so"
        "${LCS_ANDROID_DEPS_DIR}/lib/libavformat.so"
        "${LCS_ANDROID_DEPS_DIR}/lib/libavutil.so"
        "${LCS_ANDROID_DEPS_DIR}/lib/libswresample.so"
        "${LCS_ANDROID_DEPS_DIR}/lib/libswscale.so"
        log android dl z)
    target_link_options(LCSNative PRIVATE
        "-Wl,-rpath,\$ORIGIN"
        "-Wl,--no-undefined")
endif()

if(WIN32)
    target_sources(LCSNative PRIVATE resources/LCSNative.rc)
    target_link_libraries(LCSNative PRIVATE gdi32 user32 winmm d3d12 dxgi d3dcompiler dxguid)
    foreach(lib ${FFMPEG_LIBS})
        target_link_libraries(LCSNative PRIVATE ${FFMPEG_DIR}/lib/${lib}.lib)
    endforeach()
    add_custom_command(TARGET LCSNative POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${FFMPEG_DIR}/bin $<TARGET_FILE_DIR:LCSNative>)

    find_program(POWERSHELL NAMES pwsh powershell)
    if(POWERSHELL)
        add_custom_command(TARGET LCSNative POST_BUILD
            COMMAND ${POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/stamp_outputs.ps1
                -Executable $<TARGET_FILE:LCSNative>)
    endif()
elseif(NOT ANDROID)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec libavformat libavutil libswresample libswscale)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
    find_package(Threads REQUIRED)
    target_compile_definitions(LCSNative PRIVATE LCS_VULKAN_GE_BACKEND=1)
    target_link_libraries(LCSNative PRIVATE
        PkgConfig::FFMPEG PkgConfig::SDL2 PkgConfig::VULKAN Threads::Threads)
    # ge_spv.hpp is produced here so an edit to a shader is what the binary runs.
    find_program(LCS_GLSLANG NAMES glslangValidator glslang REQUIRED)
    set(LCS_GE_SPV_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/vulkan/ge_spv.hpp)
    add_custom_command(
        OUTPUT ${LCS_GE_SPV_HEADER}
        COMMAND ${CMAKE_COMMAND}
            -DGLSLANG=${LCS_GLSLANG}
            -DVERT=${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/ge.vert
            -DFRAG=${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/ge.frag
            -DPRESENT_VERT=${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/present.vert
            -DPRESENT_FRAG=${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/present.frag
            -DOUT=${LCS_GE_SPV_HEADER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/embed_spirv.cmake
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/ge.vert
            ${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/ge.frag
            ${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/present.vert
            ${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/present.frag
            ${CMAKE_CURRENT_SOURCE_DIR}/host/vulkan/embed_spirv.cmake
        COMMENT "Compiling Vulkan GE shaders"
        VERBATIM)
    add_custom_target(lcs_ge_spirv DEPENDS ${LCS_GE_SPV_HEADER})
    add_dependencies(LCSNative lcs_ge_spirv)
    # pack_font5x7 turns host/font5x7.txt into the F10 menu font. Windows skips this.
    add_executable(lcs_pack_font5x7 host/pack_font5x7.cpp)
    set_target_properties(lcs_pack_font5x7 PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set(LCS_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/host_font_5x7.hpp)
    add_custom_command(
        OUTPUT ${LCS_FONT_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
        COMMAND lcs_pack_font5x7 ${CMAKE_CURRENT_SOURCE_DIR}/host/font5x7.txt ${LCS_FONT_HEADER}
        DEPENDS lcs_pack_font5x7 ${CMAKE_CURRENT_SOURCE_DIR}/host/font5x7.txt
        COMMENT "Packing host menu font"
        VERBATIM)
    add_custom_target(lcs_host_font DEPENDS ${LCS_FONT_HEADER})
    add_dependencies(LCSNative lcs_host_font)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/host/display_window_sdl.cpp
        PROPERTIES OBJECT_DEPENDS ${LCS_FONT_HEADER})
    target_include_directories(LCSNative BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/host/ge_gpu_backend_vulkan.cpp
        PROPERTIES OBJECT_DEPENDS ${LCS_GE_SPV_HEADER})
    # Some linkers do not resolve the shared libraries FFmpeg itself depends on.
    # Probe a small link, and only then pass FFmpeg's own libdir as -rpath-link.
    include(CheckCXXSourceCompiles)
    set(_lcs_ffmpeg_probe [=[
extern "C" int avformat_version();
int main() { return avformat_version() == 0; }
]=])
    set(CMAKE_REQUIRED_LIBRARIES ${FFMPEG_LINK_LIBRARIES})
    check_cxx_source_compiles("${_lcs_ffmpeg_probe}" LCS_FFMPEG_LINKS)
    if(NOT LCS_FFMPEG_LINKS)
        pkg_get_variable(LCS_FFMPEG_LIBDIR libavcodec libdir)
        if(LCS_FFMPEG_LIBDIR)
            set(CMAKE_REQUIRED_LINK_OPTIONS "-Wl,-rpath-link,${LCS_FFMPEG_LIBDIR}")
            check_cxx_source_compiles("${_lcs_ffmpeg_probe}" LCS_FFMPEG_LINKS_WITH_LIBDIR)
            unset(CMAKE_REQUIRED_LINK_OPTIONS)
            if(LCS_FFMPEG_LINKS_WITH_LIBDIR)
                target_link_options(LCSNative PRIVATE "-Wl,-rpath-link,${LCS_FFMPEG_LIBDIR}")
            endif()
        endif()
    endif()
    unset(CMAKE_REQUIRED_LIBRARIES)
endif()

file(GLOB GENERATED_SOURCES CONFIGURE_DEPENDS generated/*.cpp)
if(GENERATED_SOURCES)
    add_library(lcs_generated STATIC ${GENERATED_SOURCES})
    target_include_directories(lcs_generated PRIVATE host)
    target_link_libraries(lcs_generated PUBLIC psprecomp_core)
    if(MSVC)
        target_compile_options(lcs_generated PRIVATE /bigobj ${PSPRECOMP_MSVC_MP_FLAG}
            $<$<CONFIG:Release,RelWithDebInfo>:/O2>)
    else()
        target_compile_options(lcs_generated PRIVATE -O2)
    endif()
    target_link_libraries(LCSNative PRIVATE lcs_generated)
else()
    target_compile_definitions(LCSNative PRIVATE LCS_NO_GENERATED)
endif()

if(NOT ANDROID)
    add_executable(lcs_host_menu_test
        ${CMAKE_SOURCE_DIR}/tests/lcs_host_menu_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/host/lcs_display_menu.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/host/lcs_render_config.cpp)
    target_include_directories(lcs_host_menu_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/host
        ${CMAKE_SOURCE_DIR}/include)
    if(WIN32)
        target_link_libraries(lcs_host_menu_test PRIVATE user32)
    else()
        target_link_libraries(lcs_host_menu_test PRIVATE PkgConfig::SDL2 Threads::Threads)
    endif()
endif()
