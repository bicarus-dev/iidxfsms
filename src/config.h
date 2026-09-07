#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace iidxfsms {

enum class Mode { game_default, milliseconds };

struct Config {
    Mode mode = Mode::milliseconds;
    std::string error;
    bool show_pgreat = false;
};

Config parse_config(std::string_view contents);
Config load_config(const std::filesystem::path& path);

}