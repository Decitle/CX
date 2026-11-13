#include "cx/script.hpp"
#include "cx/diagnostics.hpp"
#include "cx/value.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace cx {
namespace {

// Parser overview: source text is normalized into numbered logical lines, each line
// becomes a Statement (or a declaration such as an event/function/canvas), and the
// finished ScriptProgram is handed to Runtime. Parsing never executes game commands.

struct SourceLine {
    int indent{};
    int number{};
    std::string text;
    bool usedTab{false};
};

// A # starts a comment only outside quoted text. This small state machine matters
// because a player can legitimately write `broadcast "#welcome"` in a script.
std::string stripComment(std::string_view line) {
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && inString) { escaped = true; continue; }
        if (c == '"') { inString = !inString; continue; }
        if (c == '#' && !inString) return trim(line.substr(0, i));
    }
    return trim(line);
}

// Normalize indentation before parsing blocks. We preserve the original source line
// number for diagnostics, but turn both 2-space and 4-space authoring styles (and tabs)
// into the same internal indentation levels so the rest of the parser has one rule.
std::vector<SourceLine> prepareLines(const std::string& text) {
    struct RawSourceLine {
        int number{};
        int spaces{};
        bool usedTab{false};
        std::string raw;
        std::string cleaned;
    };
    std::vector<RawSourceLine> rawLines;
    std::istringstream input(text);
    std::string raw;
    int lineNumber = 0;
    bool useTwoSpaceStyle = false;
    while (std::getline(input, raw)) {
        ++lineNumber;
        int pureSpaces = 0;
        bool sawTab = false;
        for (char c : raw) {
            if (c == ' ') {
                if (!sawTab) ++pureSpaces;
            } else if (c == '\t') {
                sawTab = true;
            } else break;
        }
        const auto cleaned = stripComment(raw);
        if (cleaned.empty()) continue;
        // If this file uses two-space indentation, also treat each tab as two spaces.
        // Detect it from a pure-space prefix that cannot belong to a 4-space grid.
        if (pureSpaces > 0 && (pureSpaces % 4) != 0 && (pureSpaces % 2) == 0)
            useTwoSpaceStyle = true;
        rawLines.push_back({lineNumber, pureSpaces, sawTab, raw, cleaned});
    }

    const int indentUnit = useTwoSpaceStyle ? 2 : 4;
    std::vector<SourceLine> result;
    result.reserve(rawLines.size());
    for (auto& line : rawLines) {
        int rawWidth = 0;
        for (char c : line.raw) {
            if (c == ' ') ++rawWidth;
            else if (c == '\t') rawWidth += indentUnit;
            else break;
        }
        // Round mildly inconsistent whitespace to the nearest detected logical level.
        // A conventional 2-space level and a conventional 4-space/tab level both become
        // four parser columns, so block parsing is independent from the author's style.
        int levels = 0;
        if (rawWidth > 0) levels = std::max(1, (rawWidth + indentUnit / 2) / indentUnit);
        result.push_back({levels * 4, line.number, std::move(line.cleaned), line.usedTab});
    }
    return result;
}

bool startsWithInsensitive(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && lower(text.substr(0, prefix.size())) == lower(prefix);
}

bool endsWithColon(std::string_view text) {
    return !text.empty() && text.back() == ':';
}

std::size_t findPhraseOutsideQuotes(std::string_view text, std::string_view phrase) {
    bool inString = false;
    bool escaped = false;
    int braceDepth = 0;
    int parenDepth = 0;
    const auto lowered = lower(text);
    const auto wanted = lower(phrase);
    for (std::size_t i = 0; i + wanted.size() <= text.size(); ++i) {
        const char c = text[i];
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && inString) { escaped = true; continue; }
        if (c == '"') { inString = !inString; continue; }
        if (inString) continue;
        if (c == '{') ++braceDepth;
        else if (c == '}' && braceDepth > 0) --braceDepth;
        else if (c == '(') ++parenDepth;
        else if (c == ')' && parenDepth > 0) --parenDepth;
        if (braceDepth != 0 || parenDepth != 0) continue;
        if (lowered.compare(i, wanted.size(), wanted) == 0) {
            const bool leftOkay = i == 0 || std::isspace(static_cast<unsigned char>(text[i - 1]));
            const auto after = i + wanted.size();
            const bool rightOkay = after == text.size() || std::isspace(static_cast<unsigned char>(text[after]));
            if (leftOkay && rightOkay) return i;
        }
    }
    return std::string_view::npos;
}

std::string unquote(std::string text) {
    text = trim(text);
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return text;
    std::string out;
    out.reserve(text.size() - 2);
    bool escaped = false;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                default: out.push_back(c); break;
            }
            escaped = false;
        } else if (c == '\\') escaped = true;
        else out.push_back(c);
    }
    if (escaped) out.push_back('\\');
    return out;
}

std::vector<std::string> splitComma(std::string_view text) {
    std::vector<std::string> result;
    std::size_t start = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size()) {
            const char c = text[i];
            if (escaped) { escaped = false; continue; }
            if (c == '\\' && inString) { escaped = true; continue; }
            if (c == '"') inString = !inString;
            if (c != ',' || inString) continue;
        }
        result.push_back(trim(text.substr(start, i - start)));
        start = i + 1;
    }
    return result;
}

bool parseNumber(std::string_view text, float& value) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) return false;
    try {
        std::size_t consumed = 0;
        value = std::stof(cleaned, &consumed);
        return consumed == cleaned.size() && std::isfinite(value);
    } catch (...) { return false; }
}

bool parseUnsigned(std::string_view text, unsigned int& value) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) return false;
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoul(cleaned, &consumed);
        if (consumed != cleaned.size() || parsed > 10000UL) return false;
        value = static_cast<unsigned int>(parsed);
        return true;
    } catch (...) { return false; }
}

bool parseTuple(std::string_view tuple, std::vector<std::string>& parts, std::size_t expected) {
    parts = splitComma(tuple);
    return parts.size() == expected;
}

int editDistance(std::string_view a, std::string_view b) {
    std::vector<int> previous(b.size() + 1), current(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) previous[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const int cost = std::tolower(static_cast<unsigned char>(a[i - 1])) ==
                             std::tolower(static_cast<unsigned char>(b[j - 1])) ? 0 : 1;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        std::swap(previous, current);
    }
    return previous.back();
}

bool containsEventLocalReference(std::string_view source) {
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] != '{') continue;
        std::size_t j = i + 1;
        while (j < source.size() && std::isspace(static_cast<unsigned char>(source[j]))) ++j;
        if (j < source.size() && source[j] == '_') return true;
    }
    return false;
}

bool referencesEventLocal(const Statement& statement) {
    return statement.target.starts_with("_") ||
           containsEventLocalReference(statement.target) ||
           containsEventLocalReference(statement.expression) ||
           containsEventLocalReference(statement.secondaryExpression) ||
           containsEventLocalReference(statement.detailExpression);
}

std::string commandHint(std::string_view line) {
    static const std::vector<std::string> commands = {
        "set {variable} to value", "add amount to {variable}",
        "subtract amount from {variable}", "toggle {variable}",
        "broadcast \"message\"", "if condition:", "open canvas \"Name\"",
        "close canvas \"Name\"", "set {entrance1::room1} to an entrance point at (0, 0)",
        "load room \"room_name\"", "unload room \"room_name\"", "reload room \"room_name\"",
        "enter room \"room_name\" at {entrance1::room1}", "create light \"torch\" at (8, 5)",
        "set light \"torch\" radius to 180", "remove light \"torch\"",
        "create sprite \"player\" with texture {$crate}", "create entity \"player\" with texture {$hero}", "set sprite \"player\" position to (640, 360)",
        "set sprite \"player\" size to (48, 48)", "move sprite \"player\" by (0, -3)",
        "tween entity \"boulder\" to (640, 240) over 0.8 seconds easing ease_in_out with arc height 96",
        "cancel tween for entity \"boulder\"", "delete sprite \"player\"", "stop", "on load:", "on tick:",
        "on press key \"SPACE\":" , "on click button \"button_id\":",
        "on file attach:", "create wave \"tone\" duration 0.25 sample_rate 44100",
        "add sine wave to \"tone\" frequency 440 amplitude 0.5", "play sound \"tone\""
    };
    std::string input = trim(line);
    const auto firstSpace = input.find(' ');
    const auto firstWord = lower(input.substr(0, firstSpace));
    const std::vector<std::string> topWords = {"set", "add", "subtract", "remove", "toggle", "broadcast", "send", "if", "loop", "while", "wait", "open", "close", "stop", "on", "canvas", "function", "return", "delete", "clear", "call"};
    int bestDistance = 99;
    std::string closest;
    for (const auto& word : topWords) {
        const int distance = editDistance(firstWord, word);
        if (distance < bestDistance) { bestDistance = distance; closest = word; }
    }
    if (bestDistance <= 2 && !closest.empty() && closest != firstWord) {
        for (const auto& pattern : commands) if (lower(pattern).starts_with(closest + " ") || lower(pattern) == closest) return "Did you mean `" + pattern + "`?";
    }
    return "Use one of these command forms:\n      set {variable} to value\n      add amount to {variable}\n      subtract amount from {variable}\n      toggle {variable}\n      broadcast \"message\"\n      if condition:\n      open canvas \"Name\"\n      close canvas \"Name\"\n      stop";
}

std::optional<CanvasElement> parseCanvasElement(const SourceLine& line, const std::filesystem::path& source,
                                                  std::vector<Diagnostic>& diagnostics) {
    static const std::regex panelPattern(R"REGEX(^create\s+panel\s+at\s+\(([^)]*)\)\s+size\s+\(([^)]*)\)\s+color\s+\(([^)]*)\)\s*$)REGEX", std::regex::icase);
    static const std::regex namedPanelPattern(R"REGEX(^create\s+panel\s+"([^"\n]+)"\s+at\s+\(([^)]*)\)\s+size\s+\(([^)]*)\)\s+color\s+\(([^)]*)\)\s*$)REGEX", std::regex::icase);
    static const std::regex gridPanelPattern(R"REGEX(^create\s+panel\s+"([^"\n]+)"\s+at\s+grid\s+\(([^)]*)\)(?:\s+anchor\s+([a-z_]+))?\s+size\s+\(([^)]*)\)\s+color\s+\(([^)]*)\)\s*$)REGEX", std::regex::icase);
    static const std::regex relativePanelPattern(R"REGEX(^create\s+panel\s+"([^"\n]+)"\s+(?:to\s+the\s+)?(left|right|above|below)(?:\s+of)?\s+(button|panel)\s+"([^"\n]+)"\s+gap\s+([0-9]+(?:\.[0-9]+)?)\s+size\s+\(([^)]*)\)\s+color\s+\(([^)]*)\)\s*$)REGEX", std::regex::icase);
    static const std::regex gridButtonPattern(R"REGEX(^create\s+button\s+"([^"\n]+)"\s+at\s+grid\s+\(([^)]*)\)(?:\s+anchor\s+([a-z_]+))?\s+size\s+\(([^)]*)\)\s+label\s+("(?:\\.|[^"])*")\s*$)REGEX", std::regex::icase);
    static const std::regex relativeButtonPattern(R"REGEX(^create\s+button\s+"([^"\n]+)"\s+(?:to\s+the\s+)?(left|right|above|below)(?:\s+of)?\s+(button|panel)\s+"([^"\n]+)"\s+gap\s+([0-9]+(?:\.[0-9]+)?)\s+size\s+\(([^)]*)\)\s+label\s+("(?:\\.|[^"])*")\s*$)REGEX", std::regex::icase);
    static const std::regex textPattern(R"REGEX(^create\s+text\s+("(?:\\.|[^"])*")\s+at\s+\(([^)]*)\)\s+font_size\s+(\d+)\s*$)REGEX", std::regex::icase);
    static const std::regex buttonPattern(R"REGEX(^create\s+button\s+"([^"\n]+)"\s+at\s+\(([^)]*)\)\s+size\s+\(([^)]*)\)\s+label\s+("(?:\\.|[^"])*")\s*$)REGEX", std::regex::icase);
    std::smatch match;
    CanvasElement element;
    element.line = line.number;
    auto bad = [&](std::string message, std::string suggestion, int column = 1, int length = 1) -> std::optional<CanvasElement> {
        appendDiagnostic(diagnostics, {false, source, line.number, std::move(message), std::move(suggestion), column, length});
        return std::nullopt;
    };
    auto parseColor = [&](const std::string& colorText, CanvasElement& target) -> bool {
        std::vector<std::string> color;
        if (!parseTuple(colorText, color, 4)) return false;
        float r{}, g{}, b{}, a{};
        if (!parseNumber(color[0], r) || !parseNumber(color[1], g) || !parseNumber(color[2], b) || !parseNumber(color[3], a)) return false;
        target.red=static_cast<unsigned char>(std::clamp(r,0.0F,255.0F));
        target.green=static_cast<unsigned char>(std::clamp(g,0.0F,255.0F));
        target.blue=static_cast<unsigned char>(std::clamp(b,0.0F,255.0F));
        target.alpha=static_cast<unsigned char>(std::clamp(a,0.0F,255.0F));
        return true;
    };
    auto parseSize = [&](const std::string& sizeText, CanvasElement& target) -> bool {
        std::vector<std::string> size;
        return parseTuple(sizeText, size, 2) && parseNumber(size[0], target.width) && parseNumber(size[1], target.height) && target.width > 0.0F && target.height > 0.0F;
    };
    const std::unordered_set<std::string> validAnchors = {"auto", "top_left", "top_center", "top_right", "center_left", "center", "center_right", "bottom_left", "bottom_center", "bottom_right"};
    if (std::regex_match(line.text, match, gridPanelPattern)) {
        std::vector<std::string> position;
        float gx{}, gy{};
        if (!parseTuple(match[2].str(), position, 2) || !parseNumber(position[0], gx) || !parseNumber(position[1], gy) || gx < 0 || gx > 128 || gy < 0 || gy > 128)
            return bad("Grid positions must be coordinates from 0 to 128.", "Use `at grid (1, 1)` near the top-left or `at grid (127, 127) anchor bottom_right` near the bottom-right.");
        element.type = CanvasElementType::Panel; element.id = match[1].str(); element.layoutMode = CanvasLayoutMode::Grid;
        element.gridX = gx; element.gridY = gy; element.anchor = match[3].matched ? lower(match[3].str()) : "auto";
        if (!validAnchors.contains(element.anchor)) return bad("Unknown UI anchor `" + element.anchor + "`.", "Use auto, top_left, top_center, top_right, center_left, center, center_right, bottom_left, bottom_center, or bottom_right.");
        if (!parseSize(match[4].str(), element) || !parseColor(match[5].str(), element)) return bad("Grid panel size or color is invalid.", "Example: create panel \"panel1\" at grid (1, 1) size (400, 240) color (40, 40, 40, 220)");
        return element;
    }
    if (std::regex_match(line.text, match, relativePanelPattern)) {
        element.type = CanvasElementType::Panel; element.id = match[1].str(); element.layoutMode = CanvasLayoutMode::Relative;
        element.relativeSide = lower(match[2].str()); element.relativeToId = match[4].str();
        try { element.gap = std::stof(match[5].str()); } catch (...) { return bad("Relative panel gap must be numeric.", "Use `gap 8` between the relative target and the panel."); }
        if (element.gap < 0 || element.gap > 1000 || !parseSize(match[6].str(), element) || !parseColor(match[7].str(), element))
            return bad("Relative panel size, gap, or color is invalid.", "Example: create panel \"panel4\" to the left of button \"button1\" gap 8 size (180, 40) color (40, 40, 40, 220)");
        return element;
    }
    if (std::regex_match(line.text, match, gridButtonPattern)) {
        std::vector<std::string> position;
        float gx{}, gy{};
        if (!parseTuple(match[2].str(), position, 2) || !parseNumber(position[0], gx) || !parseNumber(position[1], gy) || gx < 0 || gx > 128 || gy < 0 || gy > 128)
            return bad("Grid positions must be coordinates from 0 to 128.", "Use `at grid (127, 127) anchor bottom_right` to pin a button to the lower-right corner.");
        element.type = CanvasElementType::Button; element.id = match[1].str(); element.text = unquote(match[5].str());
        element.layoutMode = CanvasLayoutMode::Grid; element.gridX = gx; element.gridY = gy;
        element.anchor = match[3].matched ? lower(match[3].str()) : "auto";
        if (!validAnchors.contains(element.anchor)) return bad("Unknown UI anchor `" + element.anchor + "`.", "Use top_left, top_center, top_right, center_left, center, center_right, bottom_left, bottom_center, or bottom_right.");
        if (!parseSize(match[4].str(), element)) return bad("Grid button size is invalid.", "Use a positive width and height, e.g. size (180, 44).");
        element.red=55; element.green=66; element.blue=86; element.alpha=245;
        return element;
    }
    if (std::regex_match(line.text, match, relativeButtonPattern)) {
        element.type = CanvasElementType::Button; element.id = match[1].str(); element.text = unquote(match[7].str());
        element.layoutMode = CanvasLayoutMode::Relative; element.relativeSide = lower(match[2].str()); element.relativeToId = match[4].str();
        try { element.gap = std::stof(match[5].str()); } catch (...) { return bad("Relative button gap must be numeric.", "Use gap 8 or gap 12."); }
        if (element.gap < 0 || element.gap > 1000 || !parseSize(match[6].str(), element)) return bad("Relative button size or gap is invalid.", "Example: create button \"inventory_btn\" to the left of button \"map_btn\" gap 8 size (120, 44) label \"Inventory\"");
        element.red=55; element.green=66; element.blue=86; element.alpha=245;
        return element;
    }
    if (std::regex_match(line.text, match, namedPanelPattern)) {
        std::vector<std::string> position;
        if (!parseTuple(match[2].str(), position, 2) || !parseNumber(position[0],element.x) || !parseNumber(position[1],element.y) ||
            !parseSize(match[3].str(), element) || !parseColor(match[4].str(), element))
            return bad("Named panel position, size, or color is invalid.", "Example: create panel \"dialog_panel\" at (200, 150) size (400, 300) color (40, 40, 40, 220)");
        element.type = CanvasElementType::Panel; element.id=match[1].str();
        return element;
    }
    if (std::regex_match(line.text, match, panelPattern)) {
        std::vector<std::string> position, size, color;
        if (!parseTuple(match[1].str(), position, 2) || !parseTuple(match[2].str(), size, 2) || !parseTuple(match[3].str(), color, 4))
            return bad("Panel coordinates, size, or color have the wrong number of values.",
                       "Use: create panel at (x, y) size (width, height) color (red, green, blue, alpha)");
        float x{}, y{}, w{}, h{}, r{}, g{}, b{}, a{};
        if (!parseNumber(position[0], x) || !parseNumber(position[1], y) || !parseNumber(size[0], w) ||
            !parseNumber(size[1], h) || !parseNumber(color[0], r) || !parseNumber(color[1], g) ||
            !parseNumber(color[2], b) || !parseNumber(color[3], a) || w <= 0.0F || h <= 0.0F)
            return bad("Panel values must be valid numbers and width/height must be positive.",
                       "Example: create panel at (200, 150) size (400, 300) color (40, 40, 40, 200)");
        element.type = CanvasElementType::Panel;
        element.x=x; element.y=y; element.width=w; element.height=h;
        element.red=static_cast<unsigned char>(std::clamp(r,0.0F,255.0F));
        element.green=static_cast<unsigned char>(std::clamp(g,0.0F,255.0F));
        element.blue=static_cast<unsigned char>(std::clamp(b,0.0F,255.0F));
        element.alpha=static_cast<unsigned char>(std::clamp(a,0.0F,255.0F));
        return element;
    }
    if (std::regex_match(line.text, match, textPattern)) {
        std::vector<std::string> position;
        unsigned int size{};
        float x{}, y{};
        if (!parseTuple(match[2].str(), position, 2) || !parseNumber(position[0],x) || !parseNumber(position[1],y) || !parseUnsigned(match[3].str(),size) || size == 0)
            return bad("Text position or font size is invalid.", "Example: create text \"Game Paused\" at (300, 180) font_size 24");
        element.type=CanvasElementType::Text; element.text=unquote(match[1].str());
        element.x=x; element.y=y; element.fontSize=size;
        return element;
    }
    if (std::regex_match(line.text, match, buttonPattern)) {
        std::vector<std::string> position, size;
        float x{}, y{}, w{}, h{};
        if (!parseTuple(match[2].str(), position, 2) || !parseTuple(match[3].str(), size, 2) ||
            !parseNumber(position[0],x) || !parseNumber(position[1],y) || !parseNumber(size[0],w) || !parseNumber(size[1],h) || w <= 0.0F || h <= 0.0F)
            return bad("Button coordinates or dimensions are invalid.", "Example: create button \"resume_btn\" at (300, 240) size (200, 40) label \"Resume Game\"");
        element.type=CanvasElementType::Button; element.id=match[1].str(); element.text=unquote(match[4].str());
        element.x=x; element.y=y; element.width=w; element.height=h;
        element.red=55; element.green=66; element.blue=86; element.alpha=245;
        return element;
    }
    static const std::regex progressBarPattern(R"BAR(^create\s+progress\s+bar\s+"([^"\n]+)"\s+at\s+grid\s+\(([^)]+)\)\s+size\s+\(([^)]+)\)\s+value\s+\{([^{}]+)\}\s+max\s+\{([^{}]+)\}\s+color\s+\(([^)]+)\)\s+background\s+\(([^)]+)\)\s*$)BAR", std::regex::icase);
    if (std::regex_match(line.text, match, progressBarPattern)) {
        std::vector<std::string> pos,size,color,bg;
        if(!parseTuple(match[2].str(),pos,2)||!parseTuple(match[3].str(),size,2)||!parseTuple(match[6].str(),color,4)||!parseTuple(match[7].str(),bg,4))
            return bad("Progress-bar grid, size, or colors are invalid.","Use: create progress bar \"boss_hp\" at grid (64, 6) size (500, 18) value {boss_hp} max {boss_max} color (230, 60, 75, 255) background (24, 24, 32, 230)");
        float gx=0,gy=0,w=0,h=0,r=0,g=0,b=0,a=0,br=0,bgcol=0,bb=0,ba=0;
        if(!parseNumber(pos[0],gx)||!parseNumber(pos[1],gy)||gx<0||gx>128||gy<0||gy>128||!parseNumber(size[0],w)||!parseNumber(size[1],h)||w<=0||h<=0||
           !parseNumber(color[0],r)||!parseNumber(color[1],g)||!parseNumber(color[2],b)||!parseNumber(color[3],a)||
           !parseNumber(bg[0],br)||!parseNumber(bg[1],bgcol)||!parseNumber(bg[2],bb)||!parseNumber(bg[3],ba))
            return bad("Progress-bar values must be valid; size must be positive and the grid must range from 0 to 128.","Check the example for the full syntax.");
        element.type=CanvasElementType::ProgressBar; element.id=match[1].str(); element.layoutMode=CanvasLayoutMode::Grid; element.gridX=gx; element.gridY=gy; element.anchor="auto"; element.width=w; element.height=h;
        element.valueVariable=canonicalVariableName(match[4].str()); element.maxVariable=canonicalVariableName(match[5].str());
        element.red=static_cast<unsigned char>(std::clamp(r,0.0F,255.0F)); element.green=static_cast<unsigned char>(std::clamp(g,0.0F,255.0F)); element.blue=static_cast<unsigned char>(std::clamp(b,0.0F,255.0F)); element.alpha=static_cast<unsigned char>(std::clamp(a,0.0F,255.0F));
        element.backgroundRed=static_cast<unsigned char>(std::clamp(br,0.0F,255.0F)); element.backgroundGreen=static_cast<unsigned char>(std::clamp(bgcol,0.0F,255.0F)); element.backgroundBlue=static_cast<unsigned char>(std::clamp(bb,0.0F,255.0F)); element.backgroundAlpha=static_cast<unsigned char>(std::clamp(ba,0.0F,255.0F));
        return element;
    }
    return bad("I couldn't understand this canvas element.",
               "Supported forms include absolute coordinates, grid anchors, and relative placement.\n      create button \"id\" at grid (127, 127) anchor bottom_right size (180, 44) label \"Play\"\n      create panel \"details\" to the left of button \"Play\" gap 8 size (180, 44) color (40, 40, 40, 220)");
}


// Parse one non-block statement by recognizing the language's supported command
// forms. `parseStatements` handles nested blocks around this function; keeping the two
// jobs separate makes it easier to add syntax without rewriting indentation logic.
std::optional<Statement> parseStatement(const SourceLine& line, const std::filesystem::path& source,
                                        std::vector<Diagnostic>& diagnostics) {
    const auto text = trim(line.text);
    const auto lowered = lower(text);
    Statement stmt;
    stmt.line = line.number;
    stmt.source = source.string();
    stmt.sourceLineText = text;
    auto error = [&](std::string message, std::string suggestion, int column = 1, int length = 1) {
        appendDiagnostic(diagnostics, {false, source, line.number, std::move(message), std::move(suggestion), column, length});
    };

    static const std::regex createWavePattern(R"REGEX(^create\s+wave\s+"([^"\n]+)"\s+duration\s+([0-9]+(?:\.[0-9]+)?)\s+sample_rate\s+(\d+)\s*$)REGEX", std::regex::icase);
    static const std::regex addWaveLayerPattern(R"REGEX(^add\s+(sine|square|triangle|saw|noise)\s+wave\s+to\s+"([^"\n]+)"\s+(.+?)\s*$)REGEX", std::regex::icase);
    static const std::regex setWaveEnvelopePattern(R"REGEX(^set\s+wave\s+"([^"\n]+)"\s+envelope\s+attack\s+([0-9]+(?:\.[0-9]+)?)\s+decay\s+([0-9]+(?:\.[0-9]+)?)\s+sustain\s+([0-9]+(?:\.[0-9]+)?)\s+release\s+([0-9]+(?:\.[0-9]+)?)\s*$)REGEX", std::regex::icase);
    static const std::regex namedWavePattern(R"REGEX(^(normalize\s+wave|save\s+wave|visualize\s+wave|play\s+sound)\s+(.+?)\s*$)REGEX", std::regex::icase);
    static const std::regex waveOutputPattern(R"REGEX(^(save|visualize)\s+wave\s+"([^"\n]+)"\s+to\s+"([^"\n]+)"\s*$)REGEX", std::regex::icase);
    std::smatch audioMatch;
    if (std::regex_match(text, audioMatch, createWavePattern)) {
        stmt.type = StatementType::CreateWave; stmt.target = lower(trim(audioMatch[1].str()));
        stmt.expression = audioMatch[2].str(); stmt.property = audioMatch[3].str(); return stmt;
    }
    if (std::regex_match(text, audioMatch, addWaveLayerPattern)) {
        stmt.type = StatementType::AddWaveLayer; stmt.property = lower(audioMatch[1].str());
        stmt.target = lower(trim(audioMatch[2].str())); stmt.expression = trim(audioMatch[3].str()); return stmt;
    }
    if (std::regex_match(text, audioMatch, setWaveEnvelopePattern)) {
        stmt.type = StatementType::SetWaveEnvelope; stmt.target = lower(trim(audioMatch[1].str()));
        stmt.expression = audioMatch[2].str() + "," + audioMatch[3].str() + "," + audioMatch[4].str() + "," + audioMatch[5].str(); return stmt;
    }
    if (std::regex_match(text, audioMatch, waveOutputPattern)) {
        stmt.type = lower(audioMatch[1].str()) == "save" ? StatementType::SaveWave : StatementType::VisualizeWave;
        stmt.target = lower(trim(audioMatch[2].str())); stmt.expression = audioMatch[3].str(); return stmt;
    }
    if (std::regex_match(text, audioMatch, namedWavePattern)) {
        const auto action = lower(trim(audioMatch[1].str()));
        if (action == "normalize wave") {
            stmt.type = StatementType::NormalizeWave; stmt.target = lower(unquote(trim(audioMatch[2].str()))); return stmt;
        }
        if (action == "play sound") {
            stmt.type = StatementType::PlaySound; stmt.expression = trim(audioMatch[2].str()); return stmt;
        }
    }

    static const std::regex entranceSetPattern(R"REGEX(^set\s+(\{[^{}]+\})\s+to\s+an\s+entrance\s+point\s+at\s+\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*$)REGEX", std::regex::icase);
    static const std::regex roomActionPattern(R"REGEX(^(load|unload|reload)\s+room\s+"([^"\n]+)"\s*$)REGEX", std::regex::icase);
    static const std::regex enterRoomPattern(R"REGEX(^(?:enter|go\s+to|switch\s+to)\s+room\s+"([^"\n]+)"\s+at\s+(\{[^{}]+\}|\(\s*.+\s*\))\s*$)REGEX", std::regex::icase);
    std::smatch roomMatch;
    if (std::regex_match(text, roomMatch, entranceSetPattern)) {
        stmt.type = StatementType::SetEntrance;
        stmt.target = canonicalVariableName(roomMatch[1].str());
        stmt.expression = "(" + roomMatch[2].str() + ", " + roomMatch[3].str() + ")";
        if (stmt.target.starts_with("_") || stmt.target.starts_with("$")) {
            error("An entrance point must be stored in a normal global variable, not an event-local or texture variable.",
                  "Example: set {entrance1::room1} to an entrance point at (0, 0)");
            return std::nullopt;
        }
        return stmt;
    }
    if (std::regex_match(text, roomMatch, roomActionPattern)) {
        const auto action = lower(roomMatch[1].str());
        stmt.type = action == "load" ? StatementType::LoadRoom : (action == "unload" ? StatementType::UnloadRoom : StatementType::ReloadRoom);
        stmt.target = roomMatch[2].str();
        return stmt;
    }
    if (std::regex_match(text, roomMatch, enterRoomPattern)) {
        stmt.type = StatementType::EnterRoom;
        stmt.target = roomMatch[1].str();
        stmt.expression = roomMatch[2].str();
        if (containsEventLocalReference(stmt.expression)) {
            error("An entrance point used to enter a room must be stored in a global variable.",
                  "Use {entrance::room_name::entrance_name} or set a global such as {entrance1::room1}.");
            return std::nullopt;
        }
        const auto entry = trim(stmt.expression);
        if (!entry.empty() && entry.front() == '(' && entry.back() == ')') {
            const auto coordinates = splitComma(std::string_view(entry).substr(1, entry.size() - 2));
            if (coordinates.size() != 2) {
                error("A room entry coordinate pair needs exactly an x and y tile coordinate.",
                      "Use: enter room \"cave\" at (5, 3)");
                return std::nullopt;
            }
        }
        return stmt;
    }

    static const std::regex createLightPattern(R"REGEX(^create\s+light\s+"([^"\n]+)"\s+at\s+\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)\s*$)REGEX", std::regex::icase);
    static const std::regex removeLightPattern(R"REGEX(^remove\s+light\s+"([^"\n]+)"\s*$)REGEX", std::regex::icase);
    static const std::regex setLightPattern(R"REGEX(^set\s+light\s+"([^"\n]+)"\s+([a-z_]+)\s+to\s+(.+?)\s*$)REGEX", std::regex::icase);
    if (std::regex_match(text, roomMatch, createLightPattern)) {
        stmt.type = StatementType::CreateLight;
        stmt.target = lower(trim(roomMatch[1].str()));
        stmt.expression = "(" + roomMatch[2].str() + ", " + roomMatch[3].str() + ")";
        return stmt;
    }
    if (std::regex_match(text, roomMatch, removeLightPattern)) {
        stmt.type = StatementType::RemoveLight;
        stmt.target = lower(trim(roomMatch[1].str()));
        return stmt;
    }
    if (std::regex_match(text, roomMatch, setLightPattern)) {
        stmt.type = StatementType::SetLight;
        stmt.target = lower(trim(roomMatch[1].str()));
        stmt.property = lower(trim(roomMatch[2].str()));
        stmt.expression = trim(roomMatch[3].str());
        static const std::unordered_set<std::string> lightProperties = {"radius", "intensity", "direction", "beam", "color"};
        if (!lightProperties.contains(stmt.property)) {
            error("Unknown light property: `" + stmt.property + "`.",
                  "Use radius, intensity, direction, beam, or color. Example: set light \"torch\" beam to 55");
            return std::nullopt;
        }
        return stmt;
    }

    static const std::regex createBeamPattern(R"REGEX(^create\s+beam\s+"([^"\n]+)"\s+from\s+\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)\s+to\s+\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)\s+using\s+(\{\$[^{}]+\})\s*$)REGEX", std::regex::icase);
    static const std::regex removeBeamPattern(R"REGEX(^remove\s+beam\s+"([^"\n]+)"\s*$)REGEX", std::regex::icase);
    static const std::regex setBeamPattern(R"REGEX(^set\s+beam\s+"([^"\n]+)"\s+([a-z_]+)\s+to\s+(.+?)\s*$)REGEX", std::regex::icase);
    if (std::regex_match(text, roomMatch, createBeamPattern)) {
        stmt.type = StatementType::CreateBeam;
        stmt.target = lower(trim(roomMatch[1].str()));
        stmt.expression = "(" + roomMatch[2].str() + ", " + roomMatch[3].str() + ")";
        stmt.secondaryExpression = "(" + roomMatch[4].str() + ", " + roomMatch[5].str() + ")";
        stmt.property = lower(trim(roomMatch[6].str()));
        return stmt;
    }
    if (std::regex_match(text, roomMatch, removeBeamPattern)) {
        stmt.type = StatementType::RemoveBeam;
        stmt.target = lower(trim(roomMatch[1].str()));
        return stmt;
    }
    if (std::regex_match(text, roomMatch, setBeamPattern)) {
        stmt.type = StatementType::SetBeam;
        stmt.target = lower(trim(roomMatch[1].str()));
        stmt.property = lower(trim(roomMatch[2].str()));
        stmt.expression = trim(roomMatch[3].str());
        static const std::unordered_set<std::string> beamProperties = {
            "start", "end", "width", "opacity", "tint", "layer", "lifetime",
            "texture_repeat", "endpoint_light", "endpoint_light_radius",
            "endpoint_light_intensity", "endpoint_light_color"
        };
        if (!beamProperties.contains(stmt.property)) {
            error("Unknown beam property: `" + stmt.property + "`.",
                  "Use start, end, width, opacity, tint, layer, lifetime, texture_repeat, endpoint_light, endpoint_light_radius, endpoint_light_intensity, or endpoint_light_color.");
            return std::nullopt;
        }
        return stmt;
    }

    std::smatch tileMatch;
    static const std::regex placeOverlayTilePattern(R"TILE(^place\s+overlay\s+tile\s+(\{\$[^{}]+\})\s+at\s+\((-?\d+)\s*,\s*(-?\d+)\)(?:\s+layer\s+"(background|foreground|above_entities)")?\s*$)TILE", std::regex::icase);
    if (std::regex_match(text, tileMatch, placeOverlayTilePattern)) {
        stmt.type = StatementType::PlaceOverlayTile;
        stmt.target = lower(trim(tileMatch[1].str()));
        stmt.expression = tileMatch[2].str() + "|" + tileMatch[3].str();
        stmt.property = tileMatch[4].matched ? lower(trim(tileMatch[4].str())) : "foreground";
        return stmt;
    }
    static const std::regex clearOverlayTilePattern(R"TILE(^clear\s+overlay\s+tile\s+at\s+\((-?\d+)\s*,\s*(-?\d+)\)(?:\s+layer\s+"(background|foreground|above_entities)")?\s*$)TILE", std::regex::icase);
    if (std::regex_match(text, tileMatch, clearOverlayTilePattern)) {
        stmt.type = StatementType::ClearOverlayTile;
        stmt.expression = tileMatch[1].str() + "|" + tileMatch[2].str();
        stmt.property = tileMatch[3].matched ? lower(trim(tileMatch[3].str())) : "foreground";
        return stmt;
    }
    static const std::regex placeTilePattern(R"TILE(^place\s+tile\s+(\{\$[^{}]+\})\s+at\s+\((-?\d+)\s*,\s*(-?\d+)\)\s*$)TILE", std::regex::icase);
    if(std::regex_match(text, tileMatch, placeTilePattern)) {
        stmt.type=StatementType::PlaceTile; stmt.target=lower(trim(tileMatch[1].str())); stmt.expression=tileMatch[2].str()+"|"+tileMatch[3].str(); return stmt;
    }
    static const std::regex clearTilePattern(R"TILE(^clear\s+tile\s+at\s+\((-?\d+)\s*,\s*(-?\d+)\)\s*$)TILE", std::regex::icase);
    if(std::regex_match(text, tileMatch, clearTilePattern)) {
        stmt.type=StatementType::ClearTile; stmt.expression=tileMatch[1].str()+"|"+tileMatch[2].str(); return stmt;
    }

    // Entity/path/projectile commands share the renderer-neutral sprite container.
    std::smatch entityMatch;
    static const std::regex createPathPattern(R"PATH(^create\s+path\s+"([^"\n]+)"\s+through\s+tiles?\s+(.+?)\s*$)PATH", std::regex::icase);
    if (std::regex_match(text, entityMatch, createPathPattern)) {
        stmt.type = StatementType::CreatePath;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.expression = trim(entityMatch[2].str());
        if (stmt.expression.find('(') == std::string::npos || stmt.expression.find(')') == std::string::npos) {
            error("A path needs at least one tile coordinate.", "Use: create path \"guard_route\" through tiles (10, 7), (20, 7), (20, 12)");
            return std::nullopt;
        }
        return stmt;
    }
    static const std::regex setPathPattern(R"PATH(^set\s+path\s+of\s+(?:entity\s+)?"([^"\n]+)"\s+to\s+path\s+"([^"\n]+)"\s+with\s+speed\s+"?([0-9]+(?:\.[0-9]+)?)"?\s*$)PATH", std::regex::icase);
    if (std::regex_match(text, entityMatch, setPathPattern)) {
        stmt.type = StatementType::SetEntityPath;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.expression = lower(trim(entityMatch[2].str()));
        stmt.property = trim(entityMatch[3].str());
        return stmt;
    }
    static const std::regex setEntityInteractionFlagPattern(R"INTERACT(^set\s+entity\s+"([^"\n]+)"\s+interactable\s+to\s+(true|false)\s*$)INTERACT", std::regex::icase);
    if (std::regex_match(text, entityMatch, setEntityInteractionFlagPattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.property = "interactable";
        stmt.expression = lower(trim(entityMatch[2].str()));
        return stmt;
    }
    static const std::regex setEntityInteractionRangePattern(R"INTERACT(^set\s+entity\s+"([^"\n]+)"\s+interaction\s+range\s+to\s+(.+)$)INTERACT", std::regex::icase);
    if (std::regex_match(text, entityMatch, setEntityInteractionRangePattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.property = "interaction_range";
        stmt.expression = trim(entityMatch[2].str());
        return stmt;
    }
    static const std::regex setEntityBlocksMovementPattern(R"ENTITY(^set\s+entity\s+"([^"\n]+)"\s+blocks\s+movement\s+to\s+(true|false)\s*$)ENTITY", std::regex::icase);
    if (std::regex_match(text, entityMatch, setEntityBlocksMovementPattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.property = "blocks_movement";
        stmt.expression = lower(trim(entityMatch[2].str()));
        return stmt;
    }
    static const std::regex setEntityTweenCollisionResponsePattern(R"ENTITY(^set\s+entity\s+"([^"\n]+)"\s+tween\s+collision\s+response\s+to\s+"(ignore|stop|slide)"\s*$)ENTITY", std::regex::icase);
    if (std::regex_match(text, entityMatch, setEntityTweenCollisionResponsePattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.property = "tween_collision_response";
        stmt.expression = lower(trim(entityMatch[2].str()));
        return stmt;
    }
    static const std::regex setEntityFlagsPattern(R"ENTITY(^set\s+entity\s+"([^"\n]+)"\s+(avoids\s+solid\s+tiles|collision)\s+to\s+(true|false)\s*$)ENTITY", std::regex::icase);
    if (std::regex_match(text, entityMatch, setEntityFlagsPattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        const auto prop = lower(trim(entityMatch[2].str()));
        stmt.property = prop.starts_with("avoids") ? "avoid_solid_tiles" : "collision";
        stmt.expression = lower(trim(entityMatch[3].str()));
        return stmt;
    }
    static const std::regex setEntityHitboxPattern(R"ENTITY(^set\s+entity\s+"([^"\n]+)"\s+hitbox\s+to\s+\(([^)]+)\)\s+offset\s+\(([^)]+)\)\s*$)ENTITY", std::regex::icase);
    if (std::regex_match(text, entityMatch, setEntityHitboxPattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.property = "hitbox";
        stmt.expression = "(" + trim(entityMatch[2].str()) + ")|(" + trim(entityMatch[3].str()) + ")";
        return stmt;
    }
    static const std::regex setEntityClassificationPattern(R"ENTITY(^set\s+entity\s+"([^"\n]+)"\s+(kind|team|tags)\s+to\s+"([^"\n]*)"\s*$)ENTITY", std::regex::icase);
    if (std::regex_match(text, entityMatch, setEntityClassificationPattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.property = lower(trim(entityMatch[2].str()));
        stmt.expression = trim(entityMatch[3].str());
        return stmt;
    }
    static const std::regex setEntityCollisionChannelsPattern(R"ENTITY(^set\s+entity\s+"([^"\n]+)"\s+collision\s+(layer|mask|projectile\s+mask)\s+to\s+"([^"\n]+)"\s*$)ENTITY", std::regex::icase);
    if (std::regex_match(text, entityMatch, setEntityCollisionChannelsPattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        const std::string property = lower(trim(entityMatch[2].str()));
        stmt.property = property == "projectile mask" ? "projectile_collision_mask" : "collision_" + property;
        stmt.expression = trim(entityMatch[3].str());
        return stmt;
    }
    static const std::regex setEntityNumberPattern(R"ENTITY(^set\s+entity\s+"([^"\n]+)"\s+(health|max_health)\s+to\s+([0-9]+)\s*$)ENTITY", std::regex::icase);
    if (std::regex_match(text, entityMatch, setEntityNumberPattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.property = lower(trim(entityMatch[2].str()));
        stmt.expression = trim(entityMatch[3].str());
        return stmt;
    }
    static const std::regex setAnimationSheetPattern(R"ENTITY(^set\s+entity\s+"([^"\n]+)"\s+animation\s+sheet\s+to\s+\(([^)]+)\)\s+columns\s+(\d+)\s+rows\s+(\d+)\s*$)ENTITY", std::regex::icase);
    if (std::regex_match(text, entityMatch, setAnimationSheetPattern)) {
        stmt.type = StatementType::SetEntityProperty;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.property = "animation_sheet";
        stmt.expression = "(" + trim(entityMatch[2].str()) + ")|" + entityMatch[3].str() + "|" + entityMatch[4].str();
        return stmt;
    }
    static const std::regex defineAnimationPattern(
        R"ANIM(^define\s+animation\s+"([^"\n]+)"\s+for\s+entity\s+"([^"\n]+)"\s+frames\s+(\d+)\s+to\s+(\d+)\s+row\s+(facing|\d+)\s+at\s+([0-9]+(?:\.[0-9]+)?)\s+fps\s+(once|looping)\s*$)ANIM",
        std::regex::icase);
    if (std::regex_match(text, entityMatch, defineAnimationPattern)) {
        stmt.type = StatementType::DefineAnimation;
        stmt.property = lower(trim(entityMatch[1].str())); // clip name
        stmt.target = lower(trim(entityMatch[2].str()));   // entity name
        stmt.expression = entityMatch[3].str() + "|" + entityMatch[4].str();
        stmt.secondaryExpression = lower(trim(entityMatch[5].str())); // row or "facing"
        stmt.detailExpression = entityMatch[6].str() + "|" + lower(trim(entityMatch[7].str()));
        return stmt;
    }
    static const std::regex playAnimationPattern(
        R"ANIM(^play\s+animation\s+"([^"\n]+)"\s+for\s+entity\s+"([^"\n]+)"\s*$)ANIM",
        std::regex::icase);
    if (std::regex_match(text, entityMatch, playAnimationPattern)) {
        stmt.type = StatementType::PlayAnimation;
        stmt.property = lower(trim(entityMatch[1].str())); // clip name
        stmt.target = lower(trim(entityMatch[2].str()));   // entity name
        return stmt;
    }
    static const std::regex stopAnimationPattern(
        R"ANIM(^stop\s+animation\s+for\s+entity\s+"([^"\n]+)"\s*$)ANIM",
        std::regex::icase);
    if (std::regex_match(text, entityMatch, stopAnimationPattern)) {
        stmt.type = StatementType::StopAnimation;
        stmt.target = lower(trim(entityMatch[1].str()));
        return stmt;
    }
    if (startsWithInsensitive(text, "define animation ")) {
        error("I couldn't parse this animation definition.",
              "Use: define animation \"wake\" for entity \"player\" frames 0 to 3 row facing at 8 fps once");
        return std::nullopt;
    }
    if (startsWithInsensitive(text, "play animation ")) {
        error("I couldn't parse this animation playback command.",
              "Use: play animation \"wake\" for entity \"player\"");
        return std::nullopt;
    }
    if (startsWithInsensitive(text, "stop animation ")) {
        error("I couldn't parse this animation stop command.",
              "Use: stop animation for entity \"player\"");
        return std::nullopt;
    }
    static const std::regex tweenEntityPattern(
        R"TWEEN(^tween\s+entity\s+"([^"\n]+)"\s+to\s+\((.*)\)\s+over\s+(.+?)\s+seconds?\s+easing\s+(linear|ease_in|ease_out|ease_in_out)(?:\s+with\s+arc\s+height\s+(.+?))?\s*$)TWEEN",
        std::regex::icase);
    if (std::regex_match(text, entityMatch, tweenEntityPattern)) {
        stmt.type = StatementType::TweenEntity;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.expression = "(" + trim(entityMatch[2].str()) + ")";
        stmt.secondaryExpression = trim(entityMatch[3].str());
        stmt.property = lower(trim(entityMatch[4].str()));
        if (entityMatch[5].matched) stmt.detailExpression = trim(entityMatch[5].str());
        return stmt;
    }
    static const std::regex cancelEntityTweenPattern(
        R"TWEEN(^cancel\s+tween\s+for\s+entity\s+"([^"\n]+)"\s*$)TWEEN", std::regex::icase);
    if (std::regex_match(text, entityMatch, cancelEntityTweenPattern)) {
        stmt.type = StatementType::CancelEntityTween;
        stmt.target = lower(trim(entityMatch[1].str()));
        return stmt;
    }
    if (startsWithInsensitive(text, "tween entity ")) {
        error("I couldn't parse this entity tween.",
              R"HINT(Use: tween entity "boulder" to (640, 240) over 0.8 seconds easing ease_in_out, optionally followed by `with arc height 96`. Easing can be linear, ease_in, ease_out, or ease_in_out.)HINT");
        return std::nullopt;
    }
    if (startsWithInsensitive(text, "cancel tween ")) {
        error("I couldn't parse this tween cancellation.",
              "Use: cancel tween for entity \"boulder\"");
        return std::nullopt;
    }
    static const std::regex moveEntityInputPattern(R"INPUT(^move\s+entity\s+"([^"\n]+)"\s+using\s+(wasd|arrows)\s+at\s+speed\s+([0-9]+(?:\.[0-9]+)?)\s*$)INPUT", std::regex::icase);
    if (std::regex_match(text, entityMatch, moveEntityInputPattern)) {
        stmt.type = StatementType::MoveEntityInput;
        stmt.target = lower(trim(entityMatch[1].str()));
        stmt.property = lower(trim(entityMatch[2].str()));
        stmt.expression = trim(entityMatch[3].str());
        return stmt;
    }
    // Pattern syntax is deliberately compact: the shape chooses an angular layout,
    // while count/speed/damage stay explicit so beginners can reason about cost.
    static const std::regex projectilePatternPattern(R"PATTERN(^fire\s+projectile\s+pattern\s+(radial|fan)\s+from\s+(?:entity\s+)?"([^"\n]+)"\s+toward\s+(?:entity\s+)?"([^"\n]+)"\s+with\s+texture\s+(\{\$[^{}]+\})\s+count\s+(\d+)(?:\s+spread\s+([0-9]+(?:\.[0-9]+)?))?\s+speed\s+([0-9]+(?:\.[0-9]+)?)\s+damage\s+(\d+)\s*$)PATTERN", std::regex::icase);
    std::smatch patternMatch;
    if (std::regex_match(text, patternMatch, projectilePatternPattern)) {
        const std::string kind = lower(trim(patternMatch[1].str()));
        unsigned int count = 0, damage = 0;
        float spread = 0.0F, speed = 0.0F;
        if (!parseUnsigned(patternMatch[5].str(), count) || count < 1 || count > 64) {
            error("Projectile pattern count must be between 1 and 64.",
                  "Use a modest count such as `count 8`; one command may create at most 64 projectiles.");
            return std::nullopt;
        }
        if (kind == "fan") {
            if (!patternMatch[6].matched || !parseNumber(patternMatch[6].str(), spread) || spread < 0.0F || spread > 180.0F) {
                error("A fan pattern needs a spread between 0 and 180 degrees.",
                      R"HINT(Example: fire projectile pattern fan from "boss" toward "player" with texture {$orb} count 5 spread 60 speed 4 damage 1)HINT");
                return std::nullopt;
            }
        } else if (patternMatch[6].matched) {
            error("Radial patterns do not use a spread value.",
                  "Use `count 8 speed 4 damage 1` for a full-circle radial burst.");
            return std::nullopt;
        }
        if (!parseNumber(patternMatch[7].str(), speed) || speed <= 0.0F || speed > 100.0F ||
            !parseUnsigned(patternMatch[8].str(), damage) || damage < 1 || damage > 10000) {
            error("Projectile pattern speed or damage is outside the supported range.",
                  "Use speed greater than 0 and at most 100 tiles per second, and damage from 1 to 10000.");
            return std::nullopt;
        }
        stmt.type = StatementType::SpawnProjectilePattern;
        stmt.target = lower(trim(patternMatch[2].str())); // entity that emits the pattern
        stmt.property = lower(trim(patternMatch[3].str())); // entity the projectiles are allowed to damage
        const std::string textureToken = lower(trim(patternMatch[4].str()));
        const std::string textureId = textureToken.substr(2, textureToken.size() - 3);
        // Store a compact, delimiter-separated record like other command statements.
        // All fields are parser-validated and cannot contain the delimiter.
        stmt.expression = textureId + "|" + kind + "|" + std::to_string(count) + "|" +
                          std::to_string(spread) + "|" + std::to_string(speed) + "|" + std::to_string(damage);
        return stmt;
    }
    if (startsWithInsensitive(text, "fire projectile pattern ")) {
        error("I couldn't parse this projectile pattern.",
              R"HINT(Use `fire projectile pattern radial from "boss" toward "player" with texture {$orb} count 8 speed 4 damage 1` or a fan with `count 5 spread 60`.)HINT");
        return std::nullopt;
    }

    static const std::regex fireProjectilePattern(R"PROJECTILE(^(?:fire|spawn)\s+projectile\s+from\s+(?:entity\s+)?"([^"\n]+)"\s+toward\s+(?:entity\s+)?"([^"\n]+)"\s+with\s+texture\s+(\{\$[^{}]+\})\s+speed\s+([0-9]+(?:\.[0-9]+)?)\s+damage\s+(\d+)\s*$)PROJECTILE", std::regex::icase);
    if (std::regex_match(text, entityMatch, fireProjectilePattern)) {
        stmt.type = StatementType::SpawnProjectile;
        stmt.target = lower(trim(entityMatch[1].str())); // owner
        stmt.property = lower(trim(entityMatch[2].str())); // target
        const std::string textureToken = lower(trim(entityMatch[3].str()));
        stmt.expression = textureToken.substr(2, textureToken.size() - 3);
        stmt.expression += "|" + trim(entityMatch[4].str()) + "|" + trim(entityMatch[5].str());
        return stmt;
    }

    // Minimal renderer-neutral sprite objects. Coordinates and sizes are in pixels.
    static const std::regex createSpritePattern(R"SPRITE(^create\s+(sprite|entity)\s+"([^"\n]+)"\s+with\s+texture\s+(\{\$[^{}]+\})\s*$)SPRITE", std::regex::icase);
    static const std::regex setSpritePropertyPattern(R"PROPERTY(^set\s+(x-coordinate|y-coordinate|position|size|rotation|texture|visible|visibility|speed)\s+of\s+((?:sprite|entity|object)\s+)?("[^"\n]+"|\{[^{}]+\})\s+to\s+(.+?)\s*$)PROPERTY", std::regex::icase);
    static const std::regex setSpritePattern(R"SPRITE(^set\s+(sprite|entity)\s+"([^"\n]+)"\s+(position|size|texture|visible|speed)\s+to\s+(.+?)\s*$)SPRITE", std::regex::icase);
    static const std::regex moveSpritePattern(R"SPRITE(^move\s+(sprite|entity)\s+"([^"\n]+)"\s+by\s+\((.*)\)\s*$)SPRITE", std::regex::icase);
    static const std::regex deleteSpritePattern(R"SPRITE(^(?:delete|remove)\s+(?:sprite|entity)\s+"([^"\n]+)"\s*$)SPRITE", std::regex::icase);
    if (std::regex_match(text, roomMatch, setSpritePropertyPattern)) {
        stmt.type = StatementType::SetSprite;
        const std::string objectKind = roomMatch[2].matched ? lower(trim(roomMatch[2].str())) : "object";
        stmt.target = objectKind + " " + trim(roomMatch[3].str());
        stmt.property = lower(trim(roomMatch[1].str()));
        if (stmt.property == "visibility") stmt.property = "visible";
        stmt.expression = trim(roomMatch[4].str());
        if ((stmt.property == "position" || stmt.property == "size") &&
            (stmt.expression.size() < 5 || stmt.expression.front() != '(' || stmt.expression.back() != ')')) {
            error("Sprite position and size need a coordinate pair.",
                  "Use: set position of sprite \"player\" to (640, 360), or size to (48, 48)");
            return std::nullopt;
        }
        return stmt;
    }
    if (std::regex_match(text, roomMatch, createSpritePattern)) {
        stmt.type = StatementType::CreateSprite;
        stmt.target = lower(trim(roomMatch[2].str()));
        stmt.expression = trim(roomMatch[3].str());
        stmt.property = lower(trim(roomMatch[1].str())) == "entity" ? "entity" : "sprite";
        return stmt;
    }
    if (std::regex_match(text, roomMatch, setSpritePattern)) {
        stmt.type = StatementType::SetSprite;
        stmt.target = lower(trim(roomMatch[2].str()));
        stmt.property = lower(trim(roomMatch[3].str()));
        stmt.expression = trim(roomMatch[4].str());
        if (stmt.property == "position" || stmt.property == "size") {
            if (stmt.expression.size() < 5 || stmt.expression.front() != '(' || stmt.expression.back() != ')') {
                error("Sprite position and size need a coordinate pair.",
                      "Use: set sprite \"player\" position to (640, 360), or size to (48, 48)");
                return std::nullopt;
            }
        } else if (stmt.property == "texture" &&
                   !(stmt.expression.size() >= 4 && stmt.expression.front() == '{' && stmt.expression[1] == '$' && stmt.expression.back() == '}')) {
            error("A sprite texture must refer to an automatic texture variable.",
                  "Use: set sprite \"player\" texture to {$crate}, with crate.png inside assets/textures/");
            return std::nullopt;
        } else if (stmt.property == "visible" && lower(stmt.expression) != "true" && lower(stmt.expression) != "false") {
            error("Sprite visibility must be true or false.",
                  "Use: set sprite \"player\" visible to false");
            return std::nullopt;
        }
        return stmt;
    }
    if (std::regex_match(text, roomMatch, moveSpritePattern)) {
        stmt.type = StatementType::MoveSprite;
        stmt.target = lower(trim(roomMatch[2].str()));
        stmt.property = lower(trim(roomMatch[1].str()));
        stmt.expression = "(" + trim(roomMatch[3].str()) + ")";
        return stmt;
    }
    if (std::regex_match(text, roomMatch, deleteSpritePattern)) {
        stmt.type = StatementType::DeleteSprite;
        stmt.target = lower(trim(roomMatch[1].str()));
        return stmt;
    }

    static const std::regex loopTimesPattern(R"LOOP(^(?:loop|repeat)\s+(.+?)\s+times\s*:$)LOOP", std::regex::icase);
    std::smatch controlMatch;
    if (std::regex_match(text, controlMatch, loopTimesPattern)) {
        stmt.type = StatementType::LoopTimes;
        stmt.expression = trim(controlMatch[1].str());
        if (stmt.expression.empty()) {
            error("A counted loop needs the number of repetitions.", "Use: loop 5 times:");
            return std::nullopt;
        }
        return stmt;
    }
    static const std::regex forEachLoopPattern(
        R"FOREACH(^for\s+each\s+(?:item|element|value)\s+in\s+(\{[^{}]+\})\s*:$)FOREACH",
        std::regex::icase);
    std::smatch listLoopMatch;
    if (std::regex_match(text, listLoopMatch, forEachLoopPattern)) {
        const std::string listExpression = trim(listLoopMatch[1].str());
        if (listExpression.find("::*") == std::string::npos) {
            error("A for-each loop needs a list variable ending in `::*`.",
                  "Use: for each item in {inventory::*}:");
            return std::nullopt;
        }
        stmt.type = StatementType::LoopList;
        stmt.expression = canonicalVariableName(listExpression);
        return stmt;
    }
    if (startsWithInsensitive(text, "loop ") && endsWithColon(text)) {
        std::string listExpression = trim(std::string_view(text).substr(5, text.size() - 6));
        const std::string loweredListExpression = lower(listExpression);
        if (startsWithInsensitive(loweredListExpression, "over ") ||
            startsWithInsensitive(loweredListExpression, "through ")) {
            const auto space = listExpression.find(' ');
            listExpression = trim(std::string_view(listExpression).substr(space + 1));
        }
        if (listExpression.size() >= 4 && listExpression.front() == '{' && listExpression.back() == '}' && listExpression.find("::*") != std::string::npos) {
            stmt.type = StatementType::LoopList;
            stmt.expression = canonicalVariableName(listExpression);
            return stmt;
        }
        error("This loop form is not supported yet. Use a counted loop or a list wildcard.",
              "Examples: `loop 5 times:`, `loop over {inventory::*}:`, and `for each item in {inventory::*}:`.");
        return std::nullopt;
    }
    if (startsWithInsensitive(text, "while ") && endsWithColon(text)) {
        stmt.type = StatementType::While;
        stmt.expression = trim(std::string_view(text).substr(6, text.size() - 7));
        if (stmt.expression.empty()) {
            error("A while loop needs a condition.", "Use: while {_count} > 0:");
            return std::nullopt;
        }
        return stmt;
    }
    static const std::regex waitPattern(R"WAIT(^(?:wait|halt)(?:\s+for)?\s+(.+?)\s*(milliseconds?|msecs?|ms|seconds?|secs?|s|ticks?|tick|minutes?|mins?|m|hours?|hrs?|h)\s*$)WAIT", std::regex::icase);
    if (std::regex_match(text, controlMatch, waitPattern)) {
        stmt.type = StatementType::Wait;
        stmt.expression = trim(controlMatch[1].str());
        stmt.property = lower(trim(controlMatch[2].str()));
        if (stmt.expression.empty()) {
            error("Wait needs a duration.", "Use: wait 1 second");
            return std::nullopt;
        }
        return stmt;
    }
    if (lowered == "exit loop" || lowered == "stop loop") {
        stmt.type = StatementType::ExitLoop;
        return stmt;
    }
    if (lowered == "continue" || lowered == "continue loop" || lowered == "continue this loop") {
        stmt.type = StatementType::ContinueLoop;
        return stmt;
    }

    if (startsWithInsensitive(text, "if ") && endsWithColon(text)) {
        stmt.type = StatementType::If;
        stmt.expression = trim(std::string_view(text).substr(3, text.size() - 4));
        if (stmt.expression.empty()) error("The if condition is empty.", "Example: if {score} > 10:", 4, 1);
        return stmt;
    }
    if (startsWithInsensitive(text, "seed random stream ")) {
        const auto rest = std::string_view(text).substr(std::string_view("seed random stream ").size());
        const auto with = findPhraseOutsideQuotes(rest, "with");
        if (with == std::string_view::npos) {
            error("A random stream seed needs the word `with` between its name and seed.",
                  "Example: seed random stream \"loot\" with 12345");
            return std::nullopt;
        }
        stmt.type = StatementType::SeedRandomStream;
        stmt.target = trim(rest.substr(0, with));
        stmt.expression = trim(rest.substr(with + 4));
        if (stmt.target.empty() || stmt.expression.empty()) {
            error("A random stream seed needs a stream name and an integer seed.",
                  "Example: seed random stream \"loot\" with 12345");
            return std::nullopt;
        }
        return stmt;
    }

    if (startsWithInsensitive(text, "set ")) {
        const auto rest = std::string_view(text).substr(4);
        const auto to = findPhraseOutsideQuotes(rest, "to");
        if (to == std::string_view::npos) {
            error("A set command needs the word `to` between the variable and its value.",
                  "Example: set {score} to 10", 1, static_cast<int>(text.size()));
            return std::nullopt;
        }
        stmt.type = StatementType::Set;
        stmt.target = canonicalVariableName(rest.substr(0, to));
        stmt.expression = trim(rest.substr(to + 2));
        if (stmt.target.starts_with("$")) {
            error("Texture variables are automatic, read-only names created from files in assets/textures/.",
                  "Place a file such as assets/textures/stone_up.png, then read it as {$stone_up}.");
            return std::nullopt;
        }
        if (stmt.target.empty() || stmt.expression.empty()) {
            error("The variable name or value is missing.", "Example: set {score} to 10, or set {inventory::*} to \"key\", \"potion\"", 1, static_cast<int>(text.size()));
            return std::nullopt;
        }
        if (stmt.target.find("*") != std::string::npos && !stmt.target.ends_with("::*")) {
            error("A list wildcard must end with `::*`.", "Use: set {inventory::*} to \"key\", \"potion\"");
            return std::nullopt;
        }
        const auto rawTarget = lower(trim(rest.substr(0, to)));
        const bool engineSetting = rawTarget.starts_with("engine::");
        if (!engineSetting && (rawTarget.find('{') == std::string::npos || rawTarget.find('}') == std::string::npos)) {
            error("Variables must be wrapped in curly braces. Engine settings are the one exception.",
                  "Use: set {score} to 10   (or set engine::vsync to true for a built-in engine setting)",
                  5, std::max(1, static_cast<int>(to)));
            return std::nullopt;
        }
        return stmt;
    }
    const bool addToVariable = startsWithInsensitive(text, "add ");
    const bool increaseVariable = startsWithInsensitive(text, "increase ");
    const bool subtractFromVariable = startsWithInsensitive(text, "subtract ");
    const bool decreaseVariable = startsWithInsensitive(text, "decrease ");
    const bool removeFromList = startsWithInsensitive(text, "remove ");
    if (addToVariable || increaseVariable || subtractFromVariable || decreaseVariable || removeFromList) {
        const bool adding = addToVariable || increaseVariable;
        const bool reverseOrder = increaseVariable || decreaseVariable;
        const bool removeAll = startsWithInsensitive(text, "remove all ");
        const std::size_t prefix = addToVariable ? 4U : increaseVariable ? 9U :
            subtractFromVariable ? 9U : decreaseVariable ? 9U : (removeAll ? 11U : 7U);
        const std::string_view rest(text.data() + prefix, text.size() - prefix);
        const std::string_view markerText = reverseOrder ? "by" : (adding ? "to" : (removeFromList ? "from" : "from"));
        const auto marker = findPhraseOutsideQuotes(rest, markerText);
        if (marker == std::string_view::npos) {
            error(adding ? (reverseOrder ? "The increase command is missing `by`." : "The add command is missing `to`.") :
                  (reverseOrder ? "The decrease command is missing `by`." : "This command is missing `from`."),
                  adding ? "Use `add 1 to {score}` or `increase {score} by 1`." :
                  "Use `subtract 1 from {score}`, `decrease {score} by 1`, or `remove \"potion\" from {inventory::*}`.",
                  static_cast<int>(prefix + 1U), static_cast<int>(rest.size()));
            return std::nullopt;
        }
        stmt.type = adding ? StatementType::Add : StatementType::Subtract;
        if (reverseOrder) {
            const std::string rawTarget = trim(rest.substr(0, marker));
            stmt.target = canonicalVariableName(rawTarget);
            stmt.expression = trim(rest.substr(marker + markerText.size()));
            if (rawTarget.find('{') == std::string::npos || rawTarget.find('}') == std::string::npos) {
                error("The variable being increased or decreased must be written inside curly braces.",
                      "Use `increase {score} by 1` or `decrease {score} by 1`.");
                return std::nullopt;
            }
        } else {
            stmt.expression = trim(rest.substr(0, marker));
            const auto rawTarget = trim(rest.substr(marker + markerText.size()));
            stmt.target = canonicalVariableName(rawTarget);
            if (rawTarget.find('{') == std::string::npos || rawTarget.find('}') == std::string::npos) {
                error("The destination must be a variable written inside curly braces.",
                      adding ? "Use: add 1 to {score}" : "Use: subtract 1 from {score} or remove \"potion\" from {inventory::*}");
                return std::nullopt;
            }
        }
        if (removeAll) stmt.property = "all";
        if (stmt.target.empty() || stmt.expression.empty()) {
            error("The amount or destination variable is missing.",
                  "Use `add 1 to {score}`, `increase {score} by 1`, or `remove \"item\" from {items::*}`.");
            return std::nullopt;
        }
        if (removeFromList && !stmt.target.ends_with("::*")) {
            error("The `remove` effect removes matching values from a list variable.",
                  "Use: remove \"potion\" from {inventory::*}. Use `subtract amount from {score}` for numeric subtraction.");
            return std::nullopt;
        }
        return stmt;
    }
    if (startsWithInsensitive(text, "toggle ")) {
        stmt.type = StatementType::Toggle;
        const auto rawTarget = trim(std::string_view(text).substr(7));
        stmt.target = canonicalVariableName(rawTarget);
        if (stmt.target.empty()) { error("Toggle needs a variable.", "Example: toggle {paused}"); return std::nullopt; }
        if (!lower(rawTarget).starts_with("engine::") && (rawTarget.find('{') == std::string::npos || rawTarget.find('}') == std::string::npos)) {
            error("The target must be a variable wrapped in curly braces.", "Use: toggle {paused} (or toggle engine::vsync for a built-in setting)", 8, static_cast<int>(rawTarget.size()));
            return std::nullopt;
        }
        return stmt;
    }
    if (startsWithInsensitive(text, "broadcast ") || startsWithInsensitive(text, "send ")) {
        stmt.type = StatementType::Broadcast;
        const std::size_t prefixLength = startsWithInsensitive(text, "broadcast ") ? 10U : 5U;
        stmt.expression = trim(std::string_view(text).substr(prefixLength));
        if (stmt.expression.empty()) { error("Send/broadcast needs a message or value.", "Example: send \"Hello, world!\""); return std::nullopt; }
        return stmt;
    }
    if (startsWithInsensitive(text, "open canvas ")) {
        stmt.type = StatementType::OpenCanvas;
        stmt.target = unquote(trim(std::string_view(text).substr(12)));
        if (stmt.target.empty()) { error("Open canvas needs a name.", "Example: open canvas \"PauseMenu\""); return std::nullopt; }
        return stmt;
    }
    if (startsWithInsensitive(text, "close canvas ")) {
        stmt.type = StatementType::CloseCanvas;
        stmt.target = unquote(trim(std::string_view(text).substr(13)));
        if (stmt.target.empty()) { error("Close canvas needs a name.", "Example: close canvas \"PauseMenu\""); return std::nullopt; }
        return stmt;
    }
    if (startsWithInsensitive(text, "delete ") || startsWithInsensitive(text, "clear ")) {
        stmt.type = StatementType::DeleteVariable;
        const std::size_t prefix = startsWithInsensitive(text, "delete ") ? 7U : 6U;
        stmt.target = canonicalVariableName(trim(std::string_view(text).substr(prefix)));
        if (stmt.target.empty() || stmt.target.front() == '$' || stmt.target.front() == '@') {
            error("Delete/clear needs a writable variable or list.", "Use `delete {old_value}` or `clear {inventory::*}`.");
            return std::nullopt;
        }
        return stmt;
    }
    if (lowered == "return" || startsWithInsensitive(text, "return ")) {
        stmt.type = StatementType::Return;
        stmt.expression = lowered == "return" ? "none" : trim(std::string_view(text).substr(7));
        return stmt;
    }
    if (lowered == "stop") { stmt.type = StatementType::Stop; return stmt; }

    if (startsWithInsensitive(text, "if ") && !endsWithColon(text))
        error("An if statement must end with a colon (`:`).", "Example: if {score} > 10:", static_cast<int>(text.size()), 1);
    else
        error("Unknown or unsupported statement: `" + text + "`.", commandHint(text), 1, static_cast<int>(std::max<std::size_t>(1, text.size())));
    return std::nullopt;
}

// Parse statements until indentation returns to this block's parent level. When a
// statement opens a block, this function calls itself to build that block's body; this
// is a simple recursive-descent approach and mirrors how nested if/loop code is written.
std::vector<Statement> parseStatements(const std::vector<SourceLine>& lines, std::size_t& index,
                                       int parentIndent, const std::filesystem::path& source,
                                       std::vector<Diagnostic>& diagnostics) {
    std::vector<Statement> statements;
    const int expectedIndent = parentIndent + 4;
    while (index < lines.size() && lines[index].indent > parentIndent) {
        const SourceLine line = lines[index];
        if (lower(line.text) == "else:") break;
        if (line.indent != expectedIndent) {
            appendDiagnostic(diagnostics, {false, source, line.number,
                line.indent < expectedIndent ? "This line is indented less than the current block requires." : "This line is indented too far for the current block.",
                "CX auto-detects 2-space or 4-space indentation per file and treats each tab as one indentation unit. Align sibling statements and use one consistent style within each file.", 1, 1});
        }
        std::vector<Diagnostic> statementDiagnostics;
        auto maybeStatement = parseStatement(line, source, statementDiagnostics);
        appendDiagnostics(diagnostics, std::move(statementDiagnostics));
        ++index;
        if (!maybeStatement) continue;
        Statement statement = std::move(*maybeStatement);
        if (statement.type == StatementType::If || statement.type == StatementType::LoopTimes || statement.type == StatementType::LoopList || statement.type == StatementType::While) {
            statement.body = parseStatements(lines, index, line.indent, source, diagnostics);
            if (statement.body.empty()) {
                const std::string label = statement.type == StatementType::If ? "if" : ((statement.type == StatementType::LoopTimes || statement.type == StatementType::LoopList) ? "loop" : "while loop");
                appendDiagnostic(diagnostics, {true, source, line.number, "This " + label + " block is empty.", "Indent at least one statement beneath the block header.", 1, 1});
            }
            if (statement.type == StatementType::If && index < lines.size() && lines[index].indent == line.indent && lower(lines[index].text) == "else:") {
                const auto elseLine = lines[index++];
                statement.elseBody = parseStatements(lines, index, elseLine.indent, source, diagnostics);
                if (statement.elseBody.empty()) appendDiagnostic(diagnostics, {true, source, elseLine.number, "This else block is empty.", "Indent at least one statement beneath else:.", 1, 1});
            }
        }
        statements.push_back(std::move(statement));
    }
    return statements;
}

bool parseEventHeader(const std::string& header, EventHandler& event) {
    const auto text = lower(trim(header));
    // Startup aliases are equivalent: all run once when the project becomes active.
    if (text == "on load:" || text == "on start:" || text == "on startup:" ||
        text == "on project start:" || text == "on project load:" || text == "on initialize:") {
        event.type = EventType::Load;
        return true;
    }
    // A tick is one fixed logic update, not a rendered frame.
    if (text == "on tick:" || text == "every tick:" || text == "on every tick:" ||
        text == "each tick:" || text == "on each tick:") {
        event.type = EventType::Tick;
        return true;
    }
    static const std::regex periodicPattern(R"PERIODIC(^(?:on\s+)?every\s+(\d+(?:\.\d+)?)\s*(milliseconds?|msecs?|ms|seconds?|secs?|s|ticks?|tick|minutes?|mins?|m|hours?|hrs?|h)\s*:$)PERIODIC", std::regex::icase);
    std::smatch periodicMatch;
    if (std::regex_match(text, periodicMatch, periodicPattern)) {
        try {
            const double amount = std::stod(periodicMatch[1].str());
            const std::string unit = lower(periodicMatch[2].str());
            double seconds = amount;
            if (unit == "ms" || unit.starts_with("msec") || unit.starts_with("millisecond")) seconds *= 0.001;
            else if (unit == "m" || unit.starts_with("min")) seconds *= 60.0;
            else if (unit == "h" || unit.starts_with("hour") || unit.starts_with("hr")) seconds *= 3600.0;
            else if (unit == "t" || unit.starts_with("tick")) seconds *= 1.0 / 60.0; // Resolution is limited by engine tick rate.
            if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 604800.0) return false;
            event.type = EventType::Periodic;
            event.intervalValue = amount;
            event.intervalUnit = unit;
            return true;
        } catch (...) { return false; }
    }
    if (text == "on file attach:" || text == "on file attached:") {
        event.type = EventType::FileAttach;
        return true;
    }
    static const std::regex projectileHitPattern(R"HIT(^on\s+projectile\s+hit\s+entity\s+"([^"\n]+)"\s*:$)HIT", std::regex::icase);
    std::smatch projectileHitMatch;
    if (std::regex_match(header, projectileHitMatch, projectileHitPattern)) {
        event.type = EventType::ProjectileHit;
        event.selector = lower(trim(projectileHitMatch[1].str()));
        return true;
    }
    static const std::regex tweenCompletePattern(
        R"TWEEN(^on\s+tween\s+complete\s+for\s+entity\s+"([^"\n]+)"\s*:$)TWEEN",
        std::regex::icase);
    std::smatch tweenCompleteMatch;
    if (std::regex_match(header, tweenCompleteMatch, tweenCompletePattern)) {
        event.type = EventType::TweenComplete;
        event.selector = lower(trim(tweenCompleteMatch[1].str()));
        return true;
    }
    static const std::regex animationCompletePattern(
        R"ANIM(^on\s+animation\s+complete\s+"([^"\n]+)"\s+for\s+entity\s+"([^"\n]+)"\s*:$)ANIM",
        std::regex::icase);
    std::smatch animationCompleteMatch;
    if (std::regex_match(header, animationCompleteMatch, animationCompletePattern)) {
        event.type = EventType::AnimationComplete;
        event.secondarySelector = lower(trim(animationCompleteMatch[1].str())); // clip name
        event.selector = lower(trim(animationCompleteMatch[2].str()));          // entity name
        return true;
    }
    static const std::regex interactionFocusPattern(
        R"FOCUS(^on\s+entity\s+"([^"\n]+)"\s+focus\s+(enter|exit)\s+entity\s+"([^"\n]+)"\s*:$)FOCUS",
        std::regex::icase);
    std::smatch interactionFocusMatch;
    if (std::regex_match(header, interactionFocusMatch, interactionFocusPattern)) {
        event.type = lower(interactionFocusMatch[2].str()) == "enter"
            ? EventType::FocusEnter : EventType::FocusExit;
        event.selector = lower(trim(interactionFocusMatch[1].str()));
        event.secondarySelector = lower(trim(interactionFocusMatch[3].str()));
        return true;
    }
    static const std::regex triggerPattern(
        R"TRIGGER(^on\s+entity\s+"([^"\n]+)"\s+(enter|exit)\s+trigger\s+"([^"\n]+)"\s*:$)TRIGGER",
        std::regex::icase);
    std::smatch triggerMatch;
    if (std::regex_match(header, triggerMatch, triggerPattern)) {
        event.type = lower(triggerMatch[2].str()) == "enter"
            ? EventType::TriggerEnter : EventType::TriggerExit;
        event.selector = lower(trim(triggerMatch[1].str()));
        event.secondarySelector = lower(trim(triggerMatch[3].str()));
        return true;
    }
    if (text == "on press escape:" || text == "on press key escape:" ||
        text == "when escape is pressed:") {
        event.type = EventType::KeyPress;
        event.selector = "ESCAPE";
        return true;
    }
    static const std::regex keyPattern(R"REGEX(^on\s+(?:press\s+key|key\s+press)\s+"?([^"\s:]+)"?\s*:$)REGEX", std::regex::icase);
    static const std::regex keyPressedPattern(R"REGEX(^on\s+key\s+"([^"\n]+)"\s+pressed\s*:$)REGEX", std::regex::icase);
    static const std::regex whenKeyPressedPattern(R"REGEX(^when\s+key\s+"([^"\n]+)"\s+is\s+pressed\s*:$)REGEX", std::regex::icase);
    static const std::regex buttonPattern(R"REGEX(^on\s+click\s+button\s+"([^"\n]+)"\s*:$)REGEX", std::regex::icase);
    static const std::regex buttonClickPattern(R"REGEX(^on\s+button\s+click\s+"([^"\n]+)"\s*:$)REGEX", std::regex::icase);
    static const std::regex buttonClickedPattern(R"REGEX(^on\s+button\s+"([^"\n]+)"\s+(?:click|clicked)\s*:$)REGEX", std::regex::icase);
    static const std::regex whenButtonClickedPattern(R"REGEX(^when\s+button\s+"([^"\n]+)"\s+is\s+clicked\s*:$)REGEX", std::regex::icase);
    std::smatch match;
    if (std::regex_match(header, match, keyPattern) ||
        std::regex_match(header, match, keyPressedPattern) ||
        std::regex_match(header, match, whenKeyPressedPattern)) {
        event.type = EventType::KeyPress;
        event.selector = canonicalEventKey(match[1].str());
        return true;
    }
    if (std::regex_match(header, match, buttonPattern) ||
        std::regex_match(header, match, buttonClickPattern) ||
        std::regex_match(header, match, buttonClickedPattern) ||
        std::regex_match(header, match, whenButtonClickedPattern)) {
        event.type = EventType::ButtonClick;
        event.selector = lower(match[1].str());
        return true;
    }
    return false;
}

std::string eventHeaderHint(const std::string& line) {
    if (lower(line).find("load") != std::string::npos || lower(line).find("start") != std::string::npos)
        return "Use `on load:` or its aliases `on start:` and `on startup:`.";
    if (lower(line).find("every") != std::string::npos) return "Use `every 1 second:` or `every 250 milliseconds:`. The interval must be positive and no longer than 7 days.";
    if (lower(line).find("tick") != std::string::npos) return "Use `on tick:` for every logic tick, or `every 1 second:` for a scheduled interval.";
    if (lower(line).find("press") != std::string::npos || lower(line).find("key") != std::string::npos)
        return "Use `on press key \"SPACE\":`, `on key \"SPACE\" pressed:`, or `when key \"SPACE\" is pressed:`.";
    if (lower(line).find("click") != std::string::npos || lower(line).find("button") != std::string::npos)
        return "Use `on click button \"resume_btn\":`, `on button click \"resume_btn\":`, or `when button \"resume_btn\" is clicked:`.";
    if (lower(line).find("file") != std::string::npos || lower(line).find("attach") != std::string::npos)
        return "Use exactly: on file attach:";
    if (lower(line).find("tween") != std::string::npos)
        return "Use: on tween complete for entity \"boulder\":";
    if (lower(line).find("focus") != std::string::npos)
        return "Use: on entity \"player\" focus enter entity \"chest\": or on entity \"player\" focus exit entity \"chest\":";
    if (lower(line).find("trigger") != std::string::npos || lower(line).find("enter") != std::string::npos || lower(line).find("exit") != std::string::npos)
        return "Use: on entity \"player\" enter trigger \"door_area\": or on entity \"player\" exit trigger \"door_area\":";
    return "Valid event headers include: on load:, on tick:, every 1 second:, on press key \"SPACE\":, on click button \"button_id\":, on file attach:, named trigger enter/exit events, and named tween completion events.";
}

} // namespace

std::string canonicalVariableName(std::string_view raw) {
    auto name = trim(raw);
    if (name.size() >= 2 && name.front() == '{' && name.back() == '}') name = trim(std::string_view(name).substr(1, name.size() - 2));
    return lower(name);
}

std::string canonicalEventKey(std::string_view raw) {
    const auto key = upper(trim(raw));
    if (key == "RETURN") return "ENTER";
    if (key == "ESC") return "ESCAPE";
    return key.empty() ? "UNKNOWN" : key;
}

// Load project scripts in a stable filename order. Stable ordering makes startup
// behavior reproducible and keeps diagnostics consistent from one run to the next.
ScriptProgram ScriptParser::parseDirectory(const std::filesystem::path& directory) const {
    ScriptProgram combined;
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) || !std::filesystem::is_directory(directory, ec)) {
        appendDiagnostic(combined.diagnostics, {false, directory, 0, "The scripts directory could not be found.",
                                        "This check is read-only. Launch `cx-engine <project-folder>` or interactive `cx-cli <project-folder>` once to scaffold an empty project, or create `scripts/` and add a `.cx` file.", 1, 1});
        return combined;
    }
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(directory, ec), end; it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".cx") files.push_back(it->path());
    }
    if (ec) appendDiagnostic(combined.diagnostics, {true, directory, 0, "Some script files could not be scanned: " + ec.message(),
                                            "Check that the scripts folder is readable.", 1, 1});
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        auto part = parseFile(file);
        ++combined.filesLoaded;
        combined.bootstrap.insert(combined.bootstrap.end(), std::make_move_iterator(part.bootstrap.begin()), std::make_move_iterator(part.bootstrap.end()));
        combined.handlers.insert(combined.handlers.end(), std::make_move_iterator(part.handlers.begin()), std::make_move_iterator(part.handlers.end()));
        combined.canvases.insert(combined.canvases.end(), std::make_move_iterator(part.canvases.begin()), std::make_move_iterator(part.canvases.end()));
        for (auto& [name, function] : part.functions) {
            if (!combined.functions.emplace(name, std::move(function)).second) {
                appendDiagnostic(combined.diagnostics, {false, function.file, function.line,
                    "Duplicate function definition `" + name + "` across script files.",
                    "Rename one of the functions; function names must be unique across scripts.", 1, static_cast<int>(name.size())});
            }
        }
        appendDiagnostics(combined.diagnostics, std::move(part.diagnostics));
    }
    if (files.empty()) appendDiagnostic(combined.diagnostics, {true, directory, 0, "No `.cx` files were found in the scripts folder.",
                                                       "Place one or more `.cx` files under scripts/. The interactive runtime can scaffold a new empty project folder.", 1, 1});
    std::unordered_set<std::string> canvasNames;
    for (const auto& canvas : combined.canvases) {
        if (!canvasNames.insert(lower(canvas.name)).second)
            appendDiagnostic(combined.diagnostics, {false, canvas.file, canvas.line, "Duplicate canvas name: `" + canvas.name + "`.",
                                            "Each canvas must have a unique name. Rename one of the canvas declarations.", 1, static_cast<int>(canvas.name.size())});
    }
    return combined;
}

ScriptProgram ScriptParser::parseFile(const std::filesystem::path& file) const {
    std::ifstream input(file);
    if (!input) {
        ScriptProgram program;
        appendDiagnostic(program.diagnostics, {false, file, 0, "The script file could not be opened.",
                                       "Check that the file exists and that the engine has permission to read it.", 1, 1});
        return program;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseText(buffer.str(), file);
}

// This is the main parsing entry point. It handles top-level declarations and attaches
// parsed bodies to a ScriptProgram; it collects diagnostics so the caller can show all
// detected problems at once instead of stopping at the first typo.
ScriptProgram ScriptParser::parseText(const std::string& text, const std::filesystem::path& sourceName) const {
    ScriptProgram program;
    if (sourceName != "<memory>" && sourceName != "<console>") program.filesLoaded = 1;
    const auto lines = prepareLines(text);
    std::size_t index = 0;
    while (index < lines.size()) {
        const auto line = lines[index];
        if (line.indent != 0) {
            appendDiagnostic(program.diagnostics, {false, sourceName, line.number, "Unexpected indentation at the top level.",
                                           "Top-level declarations start at the far left. Only statements inside an event, if, or canvas are indented.", 1, 1});
            ++index;
            continue;
        }
        static const std::regex functionPattern(R"FUNC(^function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*:$)FUNC", std::regex::icase);
        std::smatch functionMatch;
        if (std::regex_match(line.text, functionMatch, functionPattern)) {
            ScriptFunction function;
            function.name = lower(functionMatch[1].str());
            function.file = sourceName;
            function.line = line.number;
            const std::string parameterText = trim(functionMatch[2].str());
            if (!parameterText.empty()) {
                std::istringstream params(parameterText);
                std::string part;
                std::unordered_set<std::string> seen;
                while (std::getline(params, part, ',')) {
                    part = trim(part);
                    if (part.size() >= 2 && part.front() == '{' && part.back() == '}') part = trim(std::string_view(part).substr(1, part.size() - 2));
                    const auto typeMarker = part.find(':');
                    if (typeMarker != std::string::npos) part = trim(std::string_view(part).substr(0, typeMarker));
                    if (part.starts_with("_")) part.erase(part.begin());
                    if (part.empty() || !std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; })) {
                        appendDiagnostic(program.diagnostics, {false, sourceName, line.number,
                            "Function parameter names must be simple identifiers.",
                            "Use `function clamp(value, maximum):` and read parameters as `{_value}` and `{_maximum}`.", 1, static_cast<int>(line.text.size())});
                        continue;
                    }
                    const auto normalized = lower(part);
                    if (!seen.insert(normalized).second) {
                        appendDiagnostic(program.diagnostics, {false, sourceName, line.number,
                            "Duplicate function parameter `" + part + "`.", "Give every parameter a unique name.", 1, static_cast<int>(line.text.size())});
                        continue;
                    }
                    function.parameters.push_back(normalized);
                }
            }
            ++index;
            function.body = parseStatements(lines, index, 0, sourceName, program.diagnostics);
            if (function.body.empty()) appendDiagnostic(program.diagnostics, {true, sourceName, line.number,
                "This function has no statements.", "Indent at least one statement beneath the function header.", 1, 1});
            if (!program.functions.emplace(function.name, std::move(function)).second)
                appendDiagnostic(program.diagnostics, {false, sourceName, line.number,
                    "A function with that name is already defined.", "Rename the function so each script function has a unique name.", 1, static_cast<int>(line.text.size())});
            continue;
        }
        EventHandler event;
        if (parseEventHeader(line.text, event)) {
            event.file = sourceName; event.line = line.number;
            ++index;
            event.body = parseStatements(lines, index, 0, sourceName, program.diagnostics);
            if (event.body.empty()) appendDiagnostic(program.diagnostics, {true, sourceName, line.number, "This event has no statements.", "Indent at least one statement under the event header.", 1, 1});
            program.handlers.push_back(std::move(event));
            continue;
        }
        static const std::regex canvasPattern(R"REGEX(^canvas\s+"([^"\n]+)"\s*:$)REGEX", std::regex::icase);
        std::smatch match;
        if (std::regex_match(line.text, match, canvasPattern)) {
            CanvasDefinition canvas;
            canvas.name = match[1].str(); canvas.file = sourceName; canvas.line = line.number;
            ++index;
            bool sawLine = false;
            while (index < lines.size() && lines[index].indent > 0) {
                sawLine = true;
                if (lines[index].indent != 4) appendDiagnostic(program.diagnostics, {false, sourceName, lines[index].number,
                    "Canvas elements must be indented exactly one level.", "Use one indentation level (two spaces, four spaces, or one tab depending on this file's detected style).", 1, 1});
                auto element = parseCanvasElement(lines[index], sourceName, program.diagnostics);
                if (element) {
                    if (!element->id.empty()) {
                        for (const auto& existing : canvas.elements) {
                            if (!existing.id.empty() && lower(existing.id) == lower(element->id))
                                appendDiagnostic(program.diagnostics, {false, sourceName, lines[index].number, "Duplicate button id: `" + element->id + "`.",
                                    "Button ids must be unique within their canvas. Give this button a different id.", 1, static_cast<int>(element->id.size())});
                        }
                    }
                    canvas.elements.push_back(std::move(*element));
                }
                ++index;
            }
            if (!sawLine) appendDiagnostic(program.diagnostics, {true, sourceName, line.number, "This canvas has no elements.", "Add an indented create panel, create text, or create button line.", 1, 1});
            for (std::size_t elementIndex = 0; elementIndex < canvas.elements.size(); ++elementIndex) {
                const auto& element = canvas.elements[elementIndex];
                if (element.layoutMode != CanvasLayoutMode::Relative) continue;
                const bool hasEarlierTarget = std::any_of(canvas.elements.begin(), canvas.elements.begin() + static_cast<std::ptrdiff_t>(elementIndex), [&](const CanvasElement& prior) {
                    return lower(prior.id) == lower(element.relativeToId);
                });
                if (!hasEarlierTarget) {
                    appendDiagnostic(program.diagnostics, {false, sourceName, element.line,
                        "Relative UI element `" + element.id + "` references `" + element.relativeToId + "`, which must be defined earlier in the same canvas.",
                        "Create the target button/panel first, then place dependent elements to its left, right, above, or below.", 1, static_cast<int>(element.id.size())});
                }
            }
            program.canvases.push_back(std::move(canvas));
            continue;
        }
        if (startsWithInsensitive(line.text, "on ") || startsWithInsensitive(line.text, "every ")) {
            appendDiagnostic(program.diagnostics, {false, sourceName, line.number, "I couldn't understand this event header: `" + line.text + "`.",
                                           eventHeaderHint(line.text), 1, static_cast<int>(line.text.size())});
            ++index;
            // Consume the event's indented block so it doesn't produce a cascade of misleading top-level errors.
            while (index < lines.size() && lines[index].indent > 0) ++index;
            continue;
        }
        if (((startsWithInsensitive(line.text, "if ") || startsWithInsensitive(line.text, "loop ") ||
              startsWithInsensitive(line.text, "while ")) && endsWithColon(line.text))) {
            std::vector<Diagnostic> statementDiagnostics;
            auto maybeStatement = parseStatement(line, sourceName, statementDiagnostics);
            appendDiagnostics(program.diagnostics, std::move(statementDiagnostics));
            ++index;
            if (maybeStatement) {
                Statement statement = std::move(*maybeStatement);
                statement.body = parseStatements(lines, index, line.indent, sourceName, program.diagnostics);
                if (statement.body.empty()) {
                    const std::string label = statement.type == StatementType::If ? "if" : ((statement.type == StatementType::LoopTimes || statement.type == StatementType::LoopList) ? "loop" : "while loop");
                    appendDiagnostic(program.diagnostics, {true, sourceName, line.number,
                        "This top-level " + label + " block is empty.", "Indent at least one statement beneath the block header.", 1, 1});
                }
                if (statement.type == StatementType::If && index < lines.size() && lines[index].indent == line.indent && lower(lines[index].text) == "else:") {
                    const auto elseLine = lines[index++];
                    statement.elseBody = parseStatements(lines, index, elseLine.indent, sourceName, program.diagnostics);
                    if (statement.elseBody.empty()) appendDiagnostic(program.diagnostics, {true, sourceName, elseLine.number,
                        "This else block is empty.", "Indent at least one statement beneath else:.", 1, 1});
                }
                if (referencesEventLocal(statement)) {
                    appendDiagnostic(program.diagnostics, {false, sourceName, line.number,
                        "Event-local variables (for example, {_score}) are only available while an event is running.",
                        "Move this block inside an event such as `on load:` or use a global variable such as {score}.",
                        1, static_cast<int>(line.text.size()), line.text});
                } else {
                    program.bootstrap.push_back(std::move(statement));
                }
            }
            continue;
        }
        if (startsWithInsensitive(line.text, "set ") || startsWithInsensitive(line.text, "add ") ||
            startsWithInsensitive(line.text, "subtract ") || startsWithInsensitive(line.text, "remove ") ||
            startsWithInsensitive(line.text, "toggle ") || startsWithInsensitive(line.text, "broadcast ") ||
            startsWithInsensitive(line.text, "send ") || startsWithInsensitive(line.text, "delete ") ||
            startsWithInsensitive(line.text, "clear ") || startsWithInsensitive(line.text, "return")) {
            std::vector<Diagnostic> statementDiagnostics;
            auto statement = parseStatement(line, sourceName, statementDiagnostics);
            appendDiagnostics(program.diagnostics, std::move(statementDiagnostics));
            if (statement) {
                if (referencesEventLocal(*statement)) {
                    appendDiagnostic(program.diagnostics, {false, sourceName, line.number,
                        "Event-local variables (for example, {_score}) are only available while an event is running.",
                        "Move this command inside an event such as `on load:` or use a global variable such as {score}.",
                        1, static_cast<int>(line.text.size()), line.text});
                } else {
                    program.bootstrap.push_back(std::move(*statement));
                }
            }
            ++index;
            continue;
        }
        if (startsWithInsensitive(line.text, "canvas") && !std::regex_match(line.text, match, canvasPattern)) {
            appendDiagnostic(program.diagnostics, {false, sourceName, line.number, "Canvas declarations use a quoted name and end with a colon.",
                                           "Example: canvas \"PauseMenu\":", 1, static_cast<int>(line.text.size())});
            ++index;
            continue;
        }
        std::vector<Diagnostic> statementDiagnostics;
        auto possibleStatement = parseStatement(line, sourceName, statementDiagnostics);
        if (possibleStatement) {
            if (referencesEventLocal(*possibleStatement)) {
                appendDiagnostic(program.diagnostics, {false, sourceName, line.number,
                    "Event-local variables (for example, {_score}) are only available while an event is running.",
                    "Move this command inside an event such as `on load:` or use a global variable such as {score}.",
                    1, static_cast<int>(line.text.size()), line.text});
            } else {
                program.bootstrap.push_back(std::move(*possibleStatement));
            }
            ++index;
            continue;
        }
        if (!statementDiagnostics.empty()) {
            appendDiagnostics(program.diagnostics, std::move(statementDiagnostics));
        } else {
            appendDiagnostic(program.diagnostics, {false, sourceName, line.number, "Unknown top-level declaration: `" + line.text + "`.",
                "At the top level, use an event header, a `canvas \"Name\":` declaration, or a global command such as `set {score} to 0`.",
                1, static_cast<int>(line.text.size())});
        }
        ++index;
    }
    return program;
}

} // namespace cx
