#pragma once

#include "timing.h"

#include <array>
#include <cstdint>

namespace iidxfsms {

inline bool prepare_pgreat_display(std::array<std::uint32_t, 16>& display, std::size_t code_index,
                                   bool show_pgreat, const TimingText& timing) noexcept {
    if (!show_pgreat || !timing.valid || code_index >= display.size() || display[code_index] != 4) {
        return false;
    }
    display[code_index] = timing.direction == Direction::slow ? 5 : 3;
    return true;
}

struct DisplayTiming {
    TimingText timing;
    int code = -1;
};

struct PlayerDisplay {
    std::uintptr_t owner = 0;
    DisplayTiming combined;
    DisplayTiming keys;
    DisplayTiming scratch;
};

class DisplayCache {
public:
    void reset(int player, std::uintptr_t owner) noexcept {
        if (player >= 0 && player < 2) {
            players_[player] = PlayerDisplay{owner, {}, {}, {}};
        }
    }

    void update(int player, std::uintptr_t owner, int code, bool scratch,
                TimingText timing) noexcept {
        if (player < 0 || player >= 2) {
            return;
        }
        if (players_[player].owner != owner) {
            reset(player, owner);
        }
        if (code == 12) {
            return;
        }
        auto& display = players_[player];
        display.combined = {timing, code};
        (scratch ? display.scratch : display.keys) = display.combined;
    }

    DisplayTiming get(int player, std::uintptr_t owner, bool separate, bool scratch,
                      int code) const noexcept {
        if (player < 0 || player >= 2 || players_[player].owner != owner) {
            return {};
        }
        const auto& display = players_[player];
        const auto result = !separate ? display.combined :
                            scratch ? display.scratch : display.keys;
        return result.code == code ? result : DisplayTiming{};
    }

private:
    std::array<PlayerDisplay, 2> players_{};
};

}