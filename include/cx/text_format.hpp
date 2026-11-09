#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cx {

struct TextStyle {
    std::uint8_t red{255};
    std::uint8_t green{255};
    std::uint8_t blue{255};
    std::uint8_t alpha{255};
    bool bold{false};
    bool italic{false};
    bool underline{false};
    bool strikethrough{false};
    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

struct RichTextSpan {
    std::string text;
    TextStyle style;
};

// Accepts MiniMessage-like <#RRGGBB>, <#RRGGBB>text</#RRGGBB> gradients,
// <gradient:#RRGGBB:#RRGGBB>text</gradient>, and common Minecraft & color/style codes.
[[nodiscard]] std::vector<RichTextSpan> parseRichText(std::string_view input, TextStyle base = {});
[[nodiscard]] std::string toAnsi(std::string_view input);
[[nodiscard]] std::string stripRichText(std::string_view input);

} // namespace cx
