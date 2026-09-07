#include "game_hooks.h"
#include "display.h"
#include "text_style.h"
#include "versions/registry.h"

#include <windows.h>
#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <sstream>
#include <string_view>
#include <utility>

namespace iidxfsms {
namespace {

using Apply = int (*)(void*, int, int, int, int);
using SetDisplay = std::intptr_t (*)(void*, int, int, bool);
using DrawIndicator = void (*)(void*);
using InitializeDisplay = std::intptr_t (*)(void*, int, int);
using DrawSprite = void* (*)(void*, const char*, int, int, float, unsigned int, unsigned int);

// Shared ABI assumptions; recheck these when adding a DLL profile.
namespace native_layout {
constexpr std::size_t CANDIDATE_STRIDE = 16;
constexpr std::size_t CANDIDATE_ERROR = 0;
constexpr std::size_t CANDIDATE_NOTE = 8;

constexpr std::size_t DISPLAY_PLAYER = 8;
constexpr std::size_t DISPLAY_COMBINED_CODE = 32;
constexpr std::size_t DISPLAY_KEY_CODE = 48;
constexpr std::size_t DISPLAY_SCRATCH_CODE = 56;
constexpr std::size_t DISPLAY_SIZE = 64;

constexpr std::size_t STYLE_ALIGNMENT = 16;
constexpr std::size_t STYLE_BUFFER_SIZE = 112;
constexpr std::size_t STYLE_MODE = 4;
constexpr std::size_t STYLE_COLOR = 24;
constexpr std::size_t STYLE_SCALE_X = 92;
constexpr std::size_t STYLE_SCALE_Y = 96;
constexpr std::size_t COLOR_STRIDE = 16;
constexpr std::size_t COLOR_ALPHA = 12;

constexpr std::size_t GEOMETRY_CAPACITY = 16;
constexpr std::size_t GEOMETRY_COUNT = 9112;
constexpr std::size_t SPRITE_VTABLE = 0;
constexpr std::size_t SPRITE_SET_VISIBLE = 5;
constexpr std::size_t SPRITE_GET_DIMENSIONS = 39;

constexpr int FONT = 3;
constexpr int FONT_MODE = 1;
constexpr int MEASUREMENT_LAYER = 201;
}

// Clear state for this call and restore the enclosing call's state on exit.
template<typename Value>
class ScopedRestore {
public:
    explicit ScopedRestore(Value& value) : value_(value), saved_(std::exchange(value, Value{})) {}
    ~ScopedRestore() { value_ = saved_; }

    ScopedRestore(const ScopedRestore&) = delete;
    ScopedRestore& operator=(const ScopedRestore&) = delete;

private:
    Value& value_;
    Value saved_;
};

Apply original_apply{};
SetDisplay original_set{};
DrawIndicator original_keys{};
DrawIndicator original_scratch{};
InitializeDisplay original_initialize{};
DrawSprite original_sprite{};

std::uintptr_t base{};
const versions::Profile* profile{};
std::atomic<bool> enabled{false};
bool show_pgreat = false;

SRWLOCK cache_lock = SRWLOCK_INIT;
DisplayCache cache;

struct PendingJudgment {
    int player = -1;
    bool scratch = false;
    TimingText timing;
};

thread_local PendingJudgment pending;
thread_local TimingText rendering;
thread_local bool rendering_pgreat = false;

template<typename Value>
Value read(const void* object, std::size_t offset) noexcept {
    // Native fields may be unaligned; avoid typed pointer dereferences.
    Value value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}

template<typename Function>
Function function(std::size_t rva) noexcept {
    return reinterpret_cast<Function>(base + rva);
}

bool separate_scratch(int player) {
    const auto options = function<void* (*)()>(profile->helpers.options.rva)();
    const auto style = function<unsigned int (*)()>(profile->helpers.play_style.rva)();
    return function<int (*)(void*, int, unsigned int)>(profile->helpers.separate_scratch.rva)(options, player, style) == 1;
}

int apply_hook(void* context, int player, int grade, int lane, int score_index) {
    const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)) - base;
    const ScopedRestore pending_scope{pending};

    // Only input judgments have a candidate error; overdue misses do not.
    if (enabled.load(std::memory_order_acquire) && player >= 0 && player < 2 && lane >= 0 && lane < 8 &&
        grade >= 0 && grade <= 8 &&
        (caller == profile->press_return || caller == profile->release_return)) {
        const auto candidate = native_layout::CANDIDATE_STRIDE * static_cast<std::size_t>(lane + 8 * player);
        if (read<void*>(context, candidate + native_layout::CANDIDATE_NOTE)) {
            pending = {player, lane == 7, format_timing(read<float>(context, candidate + native_layout::CANDIDATE_ERROR))};
        }
    }

    return original_apply(context, player, grade, lane, score_index);
}

std::intptr_t set_hook(void* display, int code, int combo, bool scratch) {
    const auto result = original_set(display, code, combo, scratch);

    // Associate timing with the display actually updated by the game.
    const auto player = read<int>(display, native_layout::DISPLAY_PLAYER);
    const auto timing = pending.player == player && pending.scratch == scratch ? pending.timing : TimingText{};

    AcquireSRWLockExclusive(&cache_lock);
    cache.update(player, reinterpret_cast<std::uintptr_t>(display), code, scratch, timing);
    ReleaseSRWLockExclusive(&cache_lock);

    return result;
}

std::intptr_t initialize_hook(void* display, int player, int mode) {
    AcquireSRWLockExclusive(&cache_lock);
    cache.reset(player, reinterpret_cast<std::uintptr_t>(display));
    ReleaseSRWLockExclusive(&cache_lock);

    return original_initialize(display, player, mode);
}

void draw_indicator(void* display, bool scratch, DrawIndicator original) {
    const ScopedRestore rendering_scope{rendering};
    const ScopedRestore pgreat_scope{rendering_pgreat};
    std::array<std::uint32_t, native_layout::DISPLAY_SIZE / sizeof(std::uint32_t)> local_display{};

    const auto player = read<int>(display, native_layout::DISPLAY_PLAYER);
    if (enabled.load(std::memory_order_acquire) && player >= 0 && player < 2) {
        const bool separate = separate_scratch(player);
        const std::size_t code_offset = scratch ? native_layout::DISPLAY_SCRATCH_CODE :
                           separate ? native_layout::DISPLAY_KEY_CODE : native_layout::DISPLAY_COMBINED_CODE;
        const auto code = read<int>(display, code_offset);

        AcquireSRWLockShared(&cache_lock);
        rendering = cache.get(player, reinterpret_cast<std::uintptr_t>(display), separate, scratch, code).timing;
        ReleaseSRWLockShared(&cache_lock);

        // Complete misses have a label, not a measured timing offset.
        if (code == 8) {
            rendering = {{'m', 'i', 's', 's', '\0'}, Direction::slow, true};
        }

        // Let native visibility/position logic draw PGREAT using a display-only copy.
        if (show_pgreat && code == 4 && rendering.valid && rendering.text[0] != '\0') {
            std::memcpy(local_display.data(), display, sizeof(local_display));
            rendering_pgreat = prepare_pgreat_display(local_display, code_offset / sizeof(std::uint32_t), show_pgreat, rendering);
        }
    }

    // Valid empty text means rounded zero: suppress both text and native fallback.
    if (!rendering.valid || rendering.text[0] != '\0') {
        original(rendering_pgreat ? local_display.data() : display);
    }
}

void keys_hook(void* display) {
    draw_indicator(display, false, original_keys);
}

void scratch_hook(void* display) {
    draw_indicator(display, true, original_scratch);
}

bool draw_text(int horizontal, int vertical, unsigned int layer, const TimingText& timing,
               int width, int height, bool scratch, bool native_fast) {
    // Match the native PASELI/COIN font's proportional glyph metrics.
    alignas(native_layout::STYLE_ALIGNMENT) std::array<std::byte, native_layout::STYLE_BUFFER_SIZE> style{};
    function<void* (*)(void*)>(profile->helpers.initialize_style.rva)(style.data());
    constexpr int FONT = native_layout::FONT;
    constexpr int FONT_MODE = native_layout::FONT_MODE;
    function<void (*)(int, void*)>(profile->helpers.configure_font.rva)(FONT, style.data());
    std::memcpy(style.data() + native_layout::STYLE_MODE, &FONT_MODE, sizeof(FONT_MODE));

    const bool fast = timing.direction == Direction::neutral ? native_fast : timing.direction == Direction::fast;
    const auto color = base + (scratch ? profile->scratch_color : profile->key_color) +
                       (fast ? 0 : native_layout::COLOR_STRIDE);
    const auto foreground = readable_color(fast, scratch,
        read<float>(reinterpret_cast<const void*>(color), native_layout::COLOR_ALPHA));

    const auto length = std::strlen(timing.text.data());
    auto* manager = function<void* (*)()>(profile->helpers.font_manager.rva)();
    const auto submit = function<std::uintptr_t (*)(void*, int, int, int, const char*, int, void*, void*)>(profile->helpers.submit_text.rva);

    // This layer measures bounds without emitting geometry; size stays fixed.
    std::array<int, 4> bounds{};
    submit(manager, FONT, 0, 0, timing.text.data(), native_layout::MEASUREMENT_LAYER, style.data(), bounds.data());
    if (bounds[2] <= 0 || bounds[3] <= 0) {
        return false;
    }
    const auto layout = text_layout(width, height, bounds[2], bounds[3]);
    std::memcpy(style.data() + native_layout::STYLE_SCALE_X, &layout.scale_x, sizeof(layout.scale_x));
    std::memcpy(style.data() + native_layout::STYLE_SCALE_Y, &layout.scale_y, sizeof(layout.scale_y));
    horizontal += layout.offset_x;
    vertical += layout.offset_y;

    // Budget ten outline passes and two foreground passes per character.
    constexpr std::size_t PASSES_PER_CHARACTER = 12;
    if (read<int>(manager, native_layout::GEOMETRY_CAPACITY) - read<int>(manager, native_layout::GEOMETRY_COUNT) <
        static_cast<int>(length * PASSES_PER_CHARACTER)) {
        return false;
    }

    const std::array<float, 4> outline{0.0f, 0.0f, 0.0f, foreground[3]};
    std::memcpy(style.data() + native_layout::STYLE_COLOR, outline.data(), sizeof(outline));
    for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        for (int offset_x = -1; offset_x <= 2; ++offset_x) {
            if (offset_y != 0 || offset_x == -1 || offset_x == 2) {
                submit(manager, FONT, horizontal + offset_x, vertical + offset_y, timing.text.data(),
                       static_cast<int>(layer), style.data(), nullptr);
            }
        }
    }

    std::memcpy(style.data() + native_layout::STYLE_COLOR, foreground.data(), sizeof(foreground));
    const auto before = read<int>(manager, native_layout::GEOMETRY_COUNT);
    submit(manager, FONT, horizontal, vertical, timing.text.data(), static_cast<int>(layer), style.data(), nullptr);
    submit(manager, FONT, horizontal + 1, vertical, timing.text.data(), static_cast<int>(layer), style.data(), nullptr);
    return read<int>(manager, native_layout::GEOMETRY_COUNT) > before;
}

void* sprite_hook(void* manager, const char* name, int horizontal, int vertical, float scale,
                  unsigned int layer, unsigned int flags) {
    auto* sprite = original_sprite(manager, name, horizontal, vertical, scale, layer, flags);

    // A synthetic PGREAT sprite must stay hidden even if text submission fails.
    if (sprite && rendering_pgreat) {
        const auto vtable = read<std::uintptr_t*>(sprite, native_layout::SPRITE_VTABLE);
        reinterpret_cast<void (*)(void*, bool)>(vtable[native_layout::SPRITE_SET_VISIBLE])(sprite, false);
    }

    if (!sprite || !rendering.valid || !enabled.load(std::memory_order_acquire) || !name) {
        return sprite;
    }
    const std::string_view asset_name{name};
    if (asset_name != "fast" && asset_name != "slow" && asset_name != "s_fast" && asset_name != "s_slow") {
        return sprite;
    }

    // Keep the native sprite as fallback until replacement text produces geometry.
    const auto vtable = read<std::uintptr_t*>(sprite, native_layout::SPRITE_VTABLE);
    int width = 0;
    int height = 0;
    reinterpret_cast<std::intptr_t (*)(void*, int*, int*)>(vtable[native_layout::SPRITE_GET_DIMENSIONS])(sprite, &width, &height);
    if (width > 0 && height > 0 && draw_text(horizontal, vertical, layer, rendering, width, height,
                                           asset_name.starts_with("s_"), asset_name.ends_with("fast"))) {
        reinterpret_cast<void (*)(void*, bool)>(vtable[native_layout::SPRITE_SET_VISIBLE])(sprite, false);
    }
    return sprite;
}

bool executable_site(const versions::Site& site, std::size_t image_size) {
    if (site.rva >= image_size || site.bytes.size() > image_size - site.rva) {
        return false;
    }

    // Validate memory access before comparing the profile's entry guard.
    MEMORY_BASIC_INFORMATION region{};
    const auto* address = reinterpret_cast<void*>(base + site.rva);
    if (!VirtualQuery(address, &region, sizeof(region)) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        (region.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0 ||
        base + site.rva + site.bytes.size() > reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize) {
        return false;
    }
    return std::memcmp(address, site.bytes.data(), site.bytes.size()) == 0;
}

}

std::string install_game_hooks(bool pgreat_option) {
    show_pgreat = pgreat_option;
    const auto game = GetModuleHandleW(L"bm2dx.dll");
    if (!game) {
        return "bm2dx.dll is not loaded.";
    }

    // Reject invalid headers before selecting a signature-specific profile.
    base = reinterpret_cast<std::uintptr_t>(game);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) || dos->e_lfanew > 0x100000) {
        return "Invalid game PE header.";
    }
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return "Invalid x64 game PE header.";
    }

    std::ostringstream identifier;
    identifier << "LDJ-" << std::hex << pe->FileHeader.TimeDateStamp << '_'
               << pe->OptionalHeader.AddressOfEntryPoint;
    profile = versions::find(identifier.str());
    if (!profile) {
        return "Unsupported build; native FAST/SLOW retained.";
    }

    // Keep each native site paired with its replacement and trampoline storage.
    struct HookBinding {
        const versions::Site& site;
        void* replacement;
        void* original;
    };
    const std::array bindings{
        HookBinding{profile->hooks.apply_judgment, reinterpret_cast<void*>(&apply_hook), &original_apply},
        HookBinding{profile->hooks.set_display, reinterpret_cast<void*>(&set_hook), &original_set},
        HookBinding{profile->hooks.draw_keys, reinterpret_cast<void*>(&keys_hook), &original_keys},
        HookBinding{profile->hooks.draw_scratch, reinterpret_cast<void*>(&scratch_hook), &original_scratch},
        HookBinding{profile->hooks.initialize_display, reinterpret_cast<void*>(&initialize_hook), &original_initialize},
        HookBinding{profile->hooks.draw_sprite, reinterpret_cast<void*>(&sprite_hook), &original_sprite}
    };

    // Validate every hook and helper before installing anything.
    const auto valid_site = [image_size = pe->OptionalHeader.SizeOfImage](const versions::Site& site) {
        return executable_site(site, image_size);
    };
    if (!std::ranges::all_of(bindings, [&](const HookBinding& binding) { return valid_site(binding.site); })) {
        return "Hook entry bytes differ; native FAST/SLOW retained.";
    }
    const std::array helper_sites{
        &profile->helpers.initialize_style, &profile->helpers.font_manager, &profile->helpers.submit_text,
        &profile->helpers.configure_font, &profile->helpers.options, &profile->helpers.play_style,
        &profile->helpers.separate_scratch
    };
    if (!std::ranges::all_of(helper_sites, [&](const versions::Site* site) { return valid_site(*site); })) {
        return "Renderer helper bytes differ; native FAST/SLOW retained.";
    }

    // Detours can outlive shutdown; pin the DLL before creating trampolines.
    HMODULE self{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&install_game_hooks), &self)) {
        return "Cannot pin the DLL for hook lifetime.";
    }
    if (MH_Initialize() != MH_OK) {
        return "Cannot initialize MinHook.";
    }

    for (const auto& binding : bindings) {
        auto* target = reinterpret_cast<void*>(base + binding.site.rva);
        if (MH_CreateHook(target, binding.replacement, static_cast<void**>(binding.original)) != MH_OK) {
            MH_Uninitialize();
            return "Cannot create all hooks; no hooks enabled.";
        }
        if (MH_QueueEnableHook(target) != MH_OK) {
            MH_Uninitialize();
            return "Cannot queue all hooks; no hooks enabled.";
        }
    }

    if (MH_ApplyQueued() != MH_OK) {
        // Disable owned hooks without freeing trampolines that may be in flight.
        for (const auto& binding : bindings) {
            MH_DisableHook(reinterpret_cast<void*>(base + binding.site.rva));
        }
        return "Cannot enable all hooks; replacement disabled and DLL retained for safety.";
    }

    enabled.store(true, std::memory_order_release);
    return {};
}

void stop_game_hooks() noexcept {
    // Leave hooks resident; only stop replacing the native display.
    enabled.store(false, std::memory_order_release);
}

}