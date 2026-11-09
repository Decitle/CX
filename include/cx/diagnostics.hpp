#pragma once

#include "cx/script.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace cx {

// A broken project should explain enough problems to fix the first batch, but a
// typo storm must not consume unlimited memory. The final slot is reserved for
// one clear message saying that more diagnostics were suppressed.
inline constexpr std::size_t maximumDiagnosticEntries = 256;
void appendDiagnostic(std::vector<Diagnostic>& destination, Diagnostic diagnostic);
void appendDiagnostics(std::vector<Diagnostic>& destination, const std::vector<Diagnostic>& additions);
void appendDiagnostics(std::vector<Diagnostic>& destination, std::vector<Diagnostic>&& additions);

// Prints a compact, beginner-friendly diagnostic to the parent terminal.
void printDiagnostic(const Diagnostic& diagnostic, const std::filesystem::path& projectRoot = ".");
void printDiagnostics(const std::vector<Diagnostic>& diagnostics, const std::filesystem::path& projectRoot = ".");
[[nodiscard]] bool hasErrors(const std::vector<Diagnostic>& diagnostics) noexcept;

} // namespace cx
