#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>

namespace iidxfsms {

enum class Direction { neutral, fast, slow };

struct TimingText {
    std::array<char, 24> text{};
    Direction direction = Direction::neutral;
    bool valid = false;
};

inline TimingText format_timing(float milliseconds) noexcept {
    TimingText result;
    if (!std::isfinite(milliseconds) || std::abs(milliseconds) > 10000.0f) {
        return result;
    }
    result.direction = milliseconds < 0 ? Direction::fast :
                       milliseconds > 0 ? Direction::slow : Direction::neutral;
    const auto tenths = static_cast<std::uint32_t>(std::round(std::abs(milliseconds) * 10.0f));
    result.valid = true;
    if (tenths == 0) {
        return result;
    }
    auto cursor = result.text.data();
    *cursor++ = milliseconds < 0 ? '-' : '+';
    cursor = std::to_chars(cursor, result.text.data() + result.text.size() - 6, tenths / 10).ptr;
    *cursor++ = '.';
    *cursor++ = static_cast<char>('0' + tenths % 10);
    *cursor++ = ' ';
    *cursor++ = 'm';
    *cursor++ = 's';
    *cursor = '\0';
    return result;
}

}