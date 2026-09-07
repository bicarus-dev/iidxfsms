#include "config.h"

#include <SimpleIni.h>
#include <cerrno>

namespace iidxfsms {
namespace {

Config settings(const CSimpleIniA& ini) {
    const std::string_view mode = ini.GetValue("iidxfsms", "mode", "ms");
    if (mode == "default") {
        return {Mode::game_default, {}};
    }
    if (mode == "ms") {
        const std::string_view show_pgreat = ini.GetValue("iidxfsms", "show_pgreat", "0");
        if (show_pgreat != "0" && show_pgreat != "1") {
            return {Mode::game_default, "Invalid show_pgreat: expected 0 or 1."};
        }
        return {Mode::milliseconds, {}, show_pgreat == "1"};
    }
    return {Mode::game_default, "Invalid mode: expected ms or default."};
}

}

Config parse_config(std::string_view contents) {
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.SetQuotes();
    if (ini.LoadData(contents.data(), contents.size()) < 0) {
        return {Mode::game_default, "Cannot parse INI."};
    }
    return settings(ini);
}

Config load_config(const std::filesystem::path& path) {
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.SetQuotes();
    errno = 0;
    const auto status = ini.LoadFile(path.c_str());
    if (status < 0) {
        if (status == SI_FILE && errno == ENOENT) {
            return {};
        }
        return {Mode::game_default, "Cannot read INI. Use UTF-8 encoding."};
    }
    return settings(ini);
}

}