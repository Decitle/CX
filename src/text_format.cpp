#include "cx/text_format.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace cx {

// Rich-text parsing converts formatting tags into plain text spans with style metadata. The
// renderer can then draw spans separately without carrying markup syntax into the visible text.
namespace {

std::optional<TextStyle> parseHex(std::string_view text, std::size_t at, std::size_t digits) {
    if (at + digits > text.size()) return std::nullopt;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::array<int, 8> n{};
    for (std::size_t i = 0; i < digits; ++i) {
        n[i] = nibble(text[at + i]);
        if (n[i] < 0) return std::nullopt;
    }
    TextStyle style;
    style.red = static_cast<std::uint8_t>(n[0] * 16 + n[1]);
    style.green = static_cast<std::uint8_t>(n[2] * 16 + n[3]);
    style.blue = static_cast<std::uint8_t>(n[4] * 16 + n[5]);
    if (digits == 8) style.alpha = static_cast<std::uint8_t>(n[6] * 16 + n[7]);
    return style;
}

std::size_t utf8Length(unsigned char c) {
    if ((c & 0x80U) == 0U) return 1;
    if ((c & 0xe0U) == 0xc0U) return 2;
    if ((c & 0xf0U) == 0xe0U) return 3;
    if ((c & 0xf8U) == 0xf0U) return 4;
    return 1;
}

std::vector<std::string> utf8Characters(std::string_view value) {
    std::vector<std::string> chars;
    for (std::size_t i = 0; i < value.size();) {
        const std::size_t length = std::min(utf8Length(static_cast<unsigned char>(value[i])), value.size() - i);
        chars.emplace_back(value.substr(i, length));
        i += length;
    }
    return chars;
}

std::uint8_t mix(std::uint8_t a, std::uint8_t b, double t) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(a + (static_cast<double>(b) - a) * t + 0.5), 0, 255));
}

TextStyle blend(TextStyle a, TextStyle b, double t) {
    TextStyle out;
    out.red = mix(a.red, b.red, t); out.green = mix(a.green, b.green, t); out.blue = mix(a.blue, b.blue, t); out.alpha = mix(a.alpha, b.alpha, t);
    return out;
}

void append(std::vector<RichTextSpan>& out, std::string text, const TextStyle& style) {
    if (text.empty()) return;
    if (!out.empty() && out.back().style == style) out.back().text += text;
    else out.push_back({std::move(text), style});
}

std::optional<TextStyle> legacyColor(char code) {
    const auto c = static_cast<char>(std::tolower(static_cast<unsigned char>(code)));
    static const std::array<TextStyle, 16> colors = {{
        {0,0,0,255}, {0,0,170,255}, {0,170,0,255}, {0,170,170,255},
        {170,0,0,255}, {170,0,170,255}, {255,170,0,255}, {170,170,170,255},
        {85,85,85,255}, {85,85,255,255}, {85,255,85,255}, {85,255,255,255},
        {255,85,85,255}, {255,85,255,255}, {255,255,85,255}, {255,255,255,255}
    }};
    if (c >= '0' && c <= '9') return colors[static_cast<std::size_t>(c - '0')];
    if (c >= 'a' && c <= 'f') return colors[10U + static_cast<std::size_t>(c - 'a')];
    return std::nullopt;
}

} // namespace

std::vector<RichTextSpan> parseRichText(std::string_view input, TextStyle base) {
    std::vector<RichTextSpan> out;
    TextStyle current = base;
    std::string plain;
    auto flush = [&] { append(out, std::move(plain), current); plain.clear(); };
    for (std::size_t i = 0; i < input.size();) {
        // Explicit gradient pair: <#start>text</#end>.
        if (input.substr(i).starts_with("<#") && i + 9 <= input.size()) {
            auto start = parseHex(input, i + 2, 6);
            if (start && input[i + 8] == '>') {
                const std::size_t contentStart = i + 9;
                const auto close = input.find("</#", contentStart);
                if (close != std::string_view::npos && close + 10 <= input.size() && input[close + 9] == '>') {
                    auto end = parseHex(input, close + 3, 6);
                    const auto content = input.substr(contentStart, close - contentStart);
                    if (end && content.find('<') == std::string_view::npos) {
                        flush();
                        const auto chars = utf8Characters(content);
                        for (std::size_t j = 0; j < chars.size(); ++j) {
                            const double t = chars.size() <= 1 ? 0.0 : static_cast<double>(j) / static_cast<double>(chars.size() - 1);
                            TextStyle color = blend(*start, *end, t);
                            color.bold = current.bold; color.italic = current.italic; color.underline = current.underline; color.strikethrough = current.strikethrough;
                            append(out, chars[j], color);
                        }
                        i = close + 10;
                        continue;
                    }
                }
                flush(); current.red = start->red; current.green = start->green; current.blue = start->blue; current.alpha = start->alpha; i += 9; continue;
            }
        }
        // MiniMessage gradient: <gradient:#rrggbb:#rrggbb>text</gradient>
        if (input.substr(i).starts_with("<gradient:#")) {
            const auto tagEnd = input.find('>', i);
            if (tagEnd != std::string_view::npos) {
                const auto tag = input.substr(i, tagEnd - i + 1);
                if (tag.size() == 26 && tag.starts_with("<gradient:#") && tag[17] == ':') {
                    auto c1 = parseHex(tag, 11, 6); auto c2 = parseHex(tag, 19, 6);
                    const auto close = input.find("</gradient>", tagEnd + 1);
                    if (c1 && c2 && close != std::string_view::npos) {
                        flush();
                        const auto chars = utf8Characters(input.substr(tagEnd + 1, close - tagEnd - 1));
                        for (std::size_t j = 0; j < chars.size(); ++j) {
                            const double t = chars.size() <= 1 ? 0.0 : static_cast<double>(j) / static_cast<double>(chars.size() - 1);
                            TextStyle color = blend(*c1, *c2, t);
                            color.bold = current.bold; color.italic = current.italic; color.underline = current.underline; color.strikethrough = current.strikethrough;
                            append(out, chars[j], color);
                        }
                        i = close + 11; continue;
                    }
                }
            }
        }
        // Standard persistent hex color tag.
        if (input.substr(i).starts_with("<#") && i + 9 <= input.size()) {
            auto color = parseHex(input, i + 2, 6);
            if (color && input[i + 8] == '>') {
                flush(); current.red = color->red; current.green = color->green; current.blue = color->blue; current.alpha = color->alpha; i += 9; continue;
            }
        }
        // TAB/MiniMessage legacy hex sequence: &#RRGGBB
        if (input.substr(i).starts_with("&#") && i + 8 <= input.size()) {
            auto color = parseHex(input, i + 2, 6);
            if (color) { flush(); current.red=color->red; current.green=color->green; current.blue=color->blue; current.alpha=color->alpha; i += 8; continue; }
        }
        // Legacy Minecraft formatting codes (&0–&f, &l, &o, &n, &m, &r).
        if (input[i] == '&' && i + 1 < input.size()) {
            const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(input[i + 1])));
            if (const auto color = legacyColor(code)) {
                flush(); current.red=color->red; current.green=color->green; current.blue=color->blue; current.alpha=color->alpha;
                current.bold = current.italic = current.underline = current.strikethrough = false; i += 2; continue;
            }
            if (code == 'r') { flush(); current = base; i += 2; continue; }
            if (code == 'l' || code == 'o' || code == 'n' || code == 'm') {
                flush(); if (code == 'l') current.bold = true; if (code == 'o') current.italic = true;
                if (code == 'n') current.underline = true;
                if (code == 'm') current.strikethrough = true;
                i += 2;
                continue;
            }
        }
        // Closing hex tags without a paired opening are harmless formatting resets, not visible text.
        if (input.substr(i).starts_with("</#") && i + 10 <= input.size() && input[i + 9] == '>') {
            if (auto color = parseHex(input, i + 3, 6)) {
                flush(); current.red=color->red; current.green=color->green; current.blue=color->blue; current.alpha=color->alpha; i += 10; continue;
            }
        }
        plain.push_back(input[i++]);
    }
    flush();
    return out;
}

std::string stripRichText(std::string_view input) {
    const auto spans = parseRichText(input);
    std::string out;
    for (const auto& span : spans) out += span.text;
    return out;
}

std::string toAnsi(std::string_view input) {
    const auto spans = parseRichText(input);
    std::string out;
    for (const auto& span : spans) {
        const auto& s = span.style;
        out += "\033[0;38;2;" + std::to_string(s.red) + ";" + std::to_string(s.green) + ";" + std::to_string(s.blue) + "m";
        if (s.bold) out += "\033[1m";
        if (s.italic) out += "\033[3m";
        if (s.underline) out += "\033[4m";
        if (s.strikethrough) out += "\033[9m";
        out += span.text;
    }
    if (!out.empty()) out += "\033[0m";
    return out;
}

} // namespace cx
