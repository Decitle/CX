#include "cx/diagnostics.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <utility>

namespace cx {
namespace {

Diagnostic diagnosticLimitNotice() {
    return {false, "<diagnostics>", 0,
            "The diagnostic limit of 256 entries was reached. Additional issues are not shown.",
            "Fix the first reported problems, then rerun the project or split the source into smaller files to see the next set of issues.",
            1, 1};
}

std::string friendlyPath(const std::filesystem::path& file, const std::filesystem::path& root) {
    if (file.empty()) return "<unknown>";
    if (file == "<memory>" || file == "<console>") return file.string();
    std::error_code ec;
    const auto rel = std::filesystem::relative(file, root, ec);
    if (!ec && !rel.empty() && !rel.string().starts_with("..")) return rel.generic_string();
    return file.generic_string();
}

std::string sourceLine(const Diagnostic& diagnostic) {
    if (diagnostic.line <= 0 || diagnostic.file.empty() || diagnostic.file == "<memory>" || diagnostic.file == "<console>") return {};
    std::ifstream input(diagnostic.file);
    if (!input) return {};
    std::string line;
    for (int n = 1; n <= diagnostic.line && std::getline(input, line); ++n) {
        if (n == diagnostic.line) return line;
    }
    return {};
}

std::string visibleCode(std::string line) {
    // Keep the code line compact if a generated script contains a very long line.
    constexpr std::size_t maxWidth = 180;
    if (line.size() > maxWidth) line = line.substr(0, maxWidth - 3) + "...";
    return line;
}

} // namespace

// Keep the vector bounded at the point where messages enter it. We reserve its
// last slot for a summary, so users are told that the list is incomplete rather
// than being given a misleading impression that the final error was the only one.
void appendDiagnostic(std::vector<Diagnostic>& destination, Diagnostic diagnostic) {
    if (destination.size() < maximumDiagnosticEntries - 1) {
        destination.push_back(std::move(diagnostic));
        return;
    }
    if (destination.size() == maximumDiagnosticEntries - 1)
        destination.push_back(diagnosticLimitNotice());
}

void appendDiagnostics(std::vector<Diagnostic>& destination, const std::vector<Diagnostic>& additions) {
    for (const auto& diagnostic : additions) appendDiagnostic(destination, diagnostic);
}

void appendDiagnostics(std::vector<Diagnostic>& destination, std::vector<Diagnostic>&& additions) {
    for (auto& diagnostic : additions) appendDiagnostic(destination, std::move(diagnostic));
}

// Diagnostics are part of the beginner experience: show where the problem is, quote the
// source line when available, put a caret under the relevant text, and state a possible fix.
// This function formats the message; parsing/runtime code decides what the error means.
void printDiagnostic(const Diagnostic& diagnostic, const std::filesystem::path& projectRoot) {
    const char* severity = diagnostic.warning ? "WARNING" : "ERROR";
    std::cerr << '[' << severity << "] ";
    if (!diagnostic.file.empty()) {
        std::cerr << friendlyPath(diagnostic.file, projectRoot);
        if (diagnostic.line > 0) std::cerr << ':' << diagnostic.line;
        std::cerr << '\n';
    } else {
        std::cerr << "CX Engine\n";
    }
    std::cerr << "  " << diagnostic.message << '\n';

    std::string code = diagnostic.sourceLineText.empty() ? sourceLine(diagnostic) : diagnostic.sourceLineText;
    if (code.empty() && diagnostic.file == "<console>") code = "(command entered in the console)";
    if (!code.empty()) {
        // Parser columns are measured against its indentation-trimmed source text.
        // When the reporter reads the raw file line, account for leading whitespace
        // so the caret stays directly under the offending token inside event blocks.
        int columnOffset = 0;
        if (diagnostic.sourceLineText.empty() && diagnostic.file != "<console>" &&
            diagnostic.file != "<memory>" && diagnostic.message.find("indentation") == std::string::npos) {
            for (const char ch : code) {
                if (ch == ' ') ++columnOffset;
                else if (ch == '\t') columnOffset += 4;
                else break;
            }
        }
        std::string displayCode;
        displayCode.reserve(code.size());
        for (const char ch : code) {
            if (ch == '\t') displayCode.append(4, ' ');
            else displayCode.push_back(ch);
        }
        const std::string shown = visibleCode(displayCode);
        const int number = std::max(0, diagnostic.line);
        const std::string numberText = std::to_string(number);
        std::cerr << "  " << numberText << " | " << shown << '\n';
        const int requestedColumn = diagnostic.column + columnOffset;
        const int column = std::clamp(requestedColumn, 1, static_cast<int>(shown.size()) + 1);
        int length = std::max(1, diagnostic.length);
        length = std::min(length, std::max(1, static_cast<int>(shown.size()) - column + 1));
        std::cerr << "  " << std::string(numberText.size(), ' ') << " | "
                  << std::string(static_cast<std::size_t>(column - 1), ' ')
                  << '^' << std::string(static_cast<std::size_t>(std::max(0, length - 1)), '~') << '\n';
    }
    if (!diagnostic.suggestion.empty()) {
        std::istringstream lines(diagnostic.suggestion);
        std::string line;
        bool first = true;
        while (std::getline(lines, line)) {
            std::cerr << (first ? "  HOW TO FIX: " : "              ") << line << '\n';
            first = false;
        }
    }
    std::cerr << '\n';
}

void printDiagnostics(const std::vector<Diagnostic>& diagnostics, const std::filesystem::path& projectRoot) {
    for (const auto& diagnostic : diagnostics) printDiagnostic(diagnostic, projectRoot);
}

bool hasErrors(const std::vector<Diagnostic>& diagnostics) noexcept {
    for (const auto& diagnostic : diagnostics) if (!diagnostic.warning) return true;
    return false;
}

} // namespace cx
