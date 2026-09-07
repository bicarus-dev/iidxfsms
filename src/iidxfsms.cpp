#include "config.h"
#include "game_hooks.h"

#include <windows.h>
#include <spicesdk.h>

#include <array>
#include <exception>

namespace {

SPICE_SDK_V0 sdk{};

void __cdecl shutdown() {
    iidxfsms::stop_game_hooks();
}

void log(SPICE_SDK_LOG_LEVEL level, const char* message) {
    if (sdk.log) {
        sdk.log(level, "iidxfsms", message);
    }
}

int initialize(spice_sdk_init_func* init) {
    if (!init) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }

    // Negotiate the SDK before accessing logging or game metadata.
    sdk.size = sizeof(sdk);
    const auto status = init(0, &shutdown, &sdk);
    if (status != SPICE_SDK_STATUS_SUCCESS) {
        return status;
    }
    if (!sdk.log || !sdk.get_avs_info) {
        return SPICE_SDK_STATUS_NOT_SUPPORTED;
    }

    try {
        // Resolve the INI beside this DLL, independent of the working directory.
        HMODULE self{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&initialize), &self)) {
            log(SPICE_SDK_LOG_LEVEL_WARNING, "Cannot resolve DLL path; native FAST/SLOW retained.");
            return 1;
        }

        std::array<wchar_t, 32768> buffer{};
        const auto length = GetModuleFileNameW(self, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            log(SPICE_SDK_LOG_LEVEL_WARNING, "Cannot resolve INI path; native FAST/SLOW retained.");
            return 1;
        }
        auto path = std::filesystem::path(buffer.data());
        path.replace_extension(L".ini");

        // Invalid settings and default mode leave the game's display untouched.
        const auto config = iidxfsms::load_config(path);
        if (!config.error.empty()) {
            log(SPICE_SDK_LOG_LEVEL_WARNING, config.error.c_str());
            return 1;
        }
        if (config.mode == iidxfsms::Mode::game_default) {
            log(SPICE_SDK_LOG_LEVEL_INFO, "Game-default mode; no hooks installed.");
            return 0;
        }

        // Restrict hook installation to IIDX; the installer verifies the DLL build.
        SPICE_SDK_AVS_INFO info{};
        if (sdk.get_avs_info(&info) != SPICE_SDK_STATUS_SUCCESS || std::string_view(info.model, 3) != "LDJ") {
            log(SPICE_SDK_LOG_LEVEL_WARNING, "This DLL supports IIDX only; no hooks installed.");
            return 1;
        }

        const auto error = iidxfsms::install_game_hooks(config.show_pgreat);
        if (!error.empty()) {
            log(SPICE_SDK_LOG_LEVEL_WARNING, error.c_str());
            return 1;
        }

        log(SPICE_SDK_LOG_LEVEL_INFO, "Millisecond FAST/SLOW active for the verified game build.");
        return 0;
    } catch (const std::exception& error) {
        // Keep C++ exceptions from escaping through the SDK entry point.
        iidxfsms::stop_game_hooks();
        log(SPICE_SDK_LOG_LEVEL_WARNING, error.what());
        return 1;
    }
}

}

SPICE_SDK_ENTRY_POINT spice_sdk_entry_point(spice_sdk_init_func* init) {
    // Repeated entry calls return the first initialization result.
    static const auto result = initialize(init);
    return result;
}