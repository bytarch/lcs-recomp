#include "psprecomp/common.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"
#include "lcs_profile.hpp"
#include "display_window.hpp"
#include "lcs_audio_output.hpp"
#include "ge_gpu_backend.hpp"
#include "ge_renderer.hpp"
#include "lcs_render_config.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>

#ifdef __ANDROID__
#include <SDL_system.h>
// On Android SDL exports the app entry point as SDL_main and Java calls it
// through dlsym, so main() is renamed here the way SDL expects.
#include <SDL_main.h>
#endif

#ifndef LCS_NO_GENERATED
namespace psprecomp {
void register_generated_functions(Runtime &runtime);
} // namespace psprecomp
#endif

#ifdef __ANDROID__
// The working directory on Android is not the app's, so game data and settings
// are resolved against the internal storage directory SDL reports.
std::filesystem::path android_storage_root() {
    const char *path = SDL_AndroidGetInternalStoragePath();
    return std::filesystem::path(path != nullptr && *path != '\0' ? path : ".");
}
#endif

int main(int argc, char **argv) {
#ifdef __ANDROID__
    std::filesystem::path game_root = android_storage_root() / "game";
    std::filesystem::path elf_path = game_root / "EBOOT.ELF";
#else
    std::filesystem::path elf_path = "game/EBOOT.ELF";
    std::filesystem::path game_root = "game";
#endif
    std::uint64_t max_dispatches = std::numeric_limits<std::uint64_t>::max();
    double max_seconds = 0.0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--game" && i + 1 < argc) {
            game_root = argv[++i];
            elf_path = game_root / "EBOOT.ELF";
        } else if (arg == "--elf" && i + 1 < argc) {
            elf_path = argv[++i];
        } else if ((arg == "--max-dispatches" || arg == "--dispatch-cap") && i + 1 < argc) {
            max_dispatches = std::strtoull(argv[++i], nullptr, 0);
        } else if (arg == "--max-seconds" && i + 1 < argc) {
            max_seconds = std::strtod(argv[++i], nullptr);
        } else {
            std::cerr << "Ignoring unrecognised argument \"" << arg << "\"\n";
        }
    }

    try {
        std::error_code executable_error;
#ifdef __ANDROID__
        const std::filesystem::path executable_directory = android_storage_root();
#else
        const std::filesystem::path executable_directory =
            argc > 0 ? std::filesystem::absolute(argv[0], executable_error).parent_path()
                     : std::filesystem::current_path();
#endif
        lcs::initialize_lcs_render_configuration(executable_directory);

        auto elf = psprecomp::Elf32Image::from_file(elf_path);

        psprecomp::Runtime runtime;
        runtime.set_game_root(game_root);

        const auto stats = elf.load_and_relocate(runtime.memory(), psprecomp::kDefaultPspUserLoadBase);
        std::cout << "Loaded " << elf_path.string() << " (" << stats.total << " relocations applied)\n" << std::flush;

#ifndef LCS_NO_GENERATED
        psprecomp::register_generated_functions(runtime);
#endif
        std::cout << "Registered " << runtime.function_count() << " generated functions\n" << std::flush;


        // User memory arena starts right after the ELF image.
        std::uint64_t image_end = 0u;
        for (std::size_t index = 0; index < elf.segments().size(); ++index) {
            const auto &segment = elf.segments()[index];
            if (segment.type != 1u) continue;  // PT_LOAD
            const std::uint64_t start = elf.segment_runtime_address(index, psprecomp::kDefaultPspUserLoadBase);
            image_end = std::max(image_end, start + segment.memory_size);
        }
        const std::uint32_t user_arena_start =
            static_cast<std::uint32_t>((image_end + 0xFFu) & ~0xFFull);
        lcs::display_window_init();
        lcs::install_profile(runtime, user_arena_start);

        std::string gpu_backend_error;
        if (!lcs::initialize_ge_gpu_backend(gpu_backend_error))
            std::cerr << "[ge] GPU backend unavailable: " << gpu_backend_error << "\n";
        lcs::display_window_attach_gpu_backend();
        lcs::set_wall_clock_limit(max_seconds);

        if (const auto module = elf.find_module_info(runtime.memory(), psprecomp::kDefaultPspUserLoadBase)) {
            runtime.cpu().set_gpr(28, module->gp);
        } else {
            throw psprecomp::Error("PSP module info not found after relocation");
        }
        runtime.cpu().set_gpr(31, 0u);
        runtime.cpu().set_gpr(4, 0u);
        runtime.cpu().set_gpr(5, 0u);

        const std::uint32_t entry = elf.runtime_entry(psprecomp::kDefaultPspUserLoadBase);
        std::cout << "Running from entry " << psprecomp::hex32(entry) << "\n" << std::flush;
        runtime.run(entry, max_dispatches);
        lcs::ge_worker_shutdown();

        if (runtime.stopped()) {
            std::cout << "Runtime stopped: " << runtime.stop_reason() << "\n";
        }
        lcs::audio_output_shutdown();
    } catch (const std::exception &ex) {
        std::cerr << "LCSNative error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
