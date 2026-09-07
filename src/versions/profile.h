#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace iidxfsms::versions {

struct Site {
    std::size_t rva;
    std::array<std::uint8_t, 15> bytes;
};

struct HookPoints {
    Site apply_judgment;
    Site set_display;
    Site draw_keys;
    Site draw_scratch;
    Site initialize_display;
    Site draw_sprite;
};

struct HelperPoints {
    Site initialize_style;
    Site font_manager;
    Site submit_text;
    Site configure_font;
    Site options;
    Site play_style;
    Site separate_scratch;
};

struct Profile {
    std::string_view pe_identifier;
    std::size_t press_return;
    std::size_t release_return;
    HookPoints hooks;
    HelperPoints helpers;
    std::size_t key_color;
    std::size_t scratch_color;
};

}