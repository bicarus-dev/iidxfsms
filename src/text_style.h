#pragma once

#include <array>

namespace iidxfsms {

struct TextLayout {
    float scale_x;
    float scale_y;
    int offset_x;
    int offset_y;
};

inline TextLayout text_layout(int width, int height, int text_width, int text_height) noexcept {
    constexpr float scale = 1.0f;
    return {scale, scale, static_cast<int>((width - text_width * scale) * 0.5f),
            static_cast<int>((height - text_height * scale) * 0.5f)};
}

inline std::array<float, 4> readable_color(bool fast, bool scratch, float alpha) noexcept {
    if (scratch) {
        return fast ? std::array<float, 4>{0.6f, 0.9f, 1.0f, alpha} :
                      std::array<float, 4>{1.0f, 0.7f, 0.7f, alpha};
    }
    return fast ? std::array<float, 4>{0.3f, 0.65f, 1.0f, alpha} :
                  std::array<float, 4>{1.0f, 0.35f, 0.35f, alpha};
}

}