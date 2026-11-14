#include "cx/variable_storage.hpp"

#include "cx/value.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef CX_HAVE_ZLIB
#include <zlib.h>
#endif

namespace cx {
namespace {

// Disk format map: variables.csv is the editable, authoritative representation when it
// exists. variables.cxv is a compact cache/fallback. Keep these helpers private so the
// runtime only depends on typed VariableMap values, not on CSV/compression details.

std::string csvField(std::string_view input) {
    std::string value;
    value.reserve(input.size() + 2);
    bool quote = false;
    for (const char c : input) {
        if (c == ',') quote = true;
        if (c == '"') { value += "\"\""; quote = true; continue; }
        // String values are already encoded by escapeText() before reaching this
        // layer. CSV quoting must escape CSV syntax (commas/quotes), not backslashes:
        // escaping them twice turns a real newline into the two characters "\n".
        if (c == '\n') { value += "\\n"; continue; }
        if (c == '\r') { value += "\\r"; continue; }
        value.push_back(c);
    }
    if (quote || value.find_first_of(",\"\n\r") != std::string::npos) return "\"" + value + "\"";
    return value;
}

bool splitCsv(std::string_view line, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool quoted = false;
    bool justClosedQuote = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                    justClosedQuote = true;
                }
            } else {
                field.push_back(c);
            }
            continue;
        }

        if (justClosedQuote) {
            // After a closing quote, only a delimiter or end-of-line is legal.
            // Rejecting stray suffix characters prevents malformed rows from being
            // silently interpreted as a different variable name or value.
            if (c != ',') return false;
            fields.push_back(std::move(field));
            field.clear();
            justClosedQuote = false;
        } else if (c == ',') {
            fields.push_back(std::move(field));
            field.clear();
        } else if (c == '"') {
            if (!field.empty()) return false; // A quote may only open an empty field.
            quoted = true;
        } else {
            field.push_back(c);
        }
    }
    if (quoted) return false;
    fields.push_back(std::move(field));
    return true;
}

std::string unescapeText(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[++i];
            if (next == 'n') out.push_back('\n');
            else if (next == 'r') out.push_back('\r');
            else out.push_back(next);
        } else out.push_back(value[i]);
    }
    return out;
}

std::string escapeText(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out.push_back(c);
    }
    return out;
}

// Sort names before writing. Stable ordering makes diffs readable and reproducible even
// though Runtime stores variables in an unordered_map (whose iteration order can vary).
std::string serializeCsv(const VariableStorage::VariableMap& input) {
    std::vector<std::pair<std::string, Value>> sorted;
    sorted.reserve(input.size());
    for (const auto& [name, value] : input) {
        if (name.empty() || name.starts_with("engine::") || name.starts_with("_") ||
            name.starts_with("^") || name.starts_with("$") || value.isObjectReference()) continue;
        sorted.emplace_back(name, value);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::ostringstream out;
    out << "# CX Engine persistent variables. Edit or delete rows while the engine is closed.\n";
    out << "# name,type,value\n";
    for (const auto& [name, value] : sorted) {
        std::string type = "none", raw;
        const auto& storage = value.storage();
        if (std::holds_alternative<bool>(storage)) { type = "bool"; raw = value.toString(); }
        else if (std::holds_alternative<double>(storage)) { type = "number"; raw = value.toString(); }
        else if (std::holds_alternative<std::string>(storage)) { type = "string"; raw = escapeText(value.toString()); }
        out << csvField(name) << ',' << type << ',' << csvField(raw) << '\n';
    }
    return out.str();
}

// Be forgiving about damaged individual rows: report a warning and continue loading the
// rows that are still valid. This is a recovery policy, not proof that every edit is safe;
// tests should cover malformed names, types, numbers, and quoted fields.
bool parseCsv(std::string_view text, VariableStorage::VariableMap& output, std::string& warning,
              std::string_view sourceName = "variables.csv") {
    std::istringstream input{std::string(text)};
    std::string line;
    std::size_t lineNumber = 0;
    std::size_t warningCount = 0;
    std::vector<std::string> fields;
    constexpr std::size_t maxWarningsToShow = 8;
    warning.clear();

    // Preserve good rows from a human-edited CSV, but never hide the fact that the
    // file was imperfect. Cache recovery uses the return value to decide whether it
    // is safe to reconstruct a missing CSV from the decoded contents.
    const auto noteInvalidRow = [&](std::string message) {
        if (warningCount < maxWarningsToShow) {
            if (!warning.empty()) warning += '\n';
            warning += std::move(message);
        }
        ++warningCount;
    };

    while (std::getline(input, line)) {
        ++lineNumber;
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.starts_with('#') || lower(trimmed) == "name,type,value") continue;
        if (!splitCsv(line, fields) || fields.size() != 3) {
            noteInvalidRow(std::string(sourceName) + " line " + std::to_string(lineNumber) +
                           " is malformed and was skipped. Expected: name,type,value.");
            continue;
        }
        const std::string name = lower(trim(fields[0]));
        const std::string type = lower(trim(fields[1]));
        const std::string raw = fields[2];
        if (name.empty() || name.starts_with("_") || name.starts_with("^") || name.starts_with("$") || name.starts_with("engine::")) {
            noteInvalidRow(std::string(sourceName) + " line " + std::to_string(lineNumber) +
                           " uses a reserved or empty variable name and was skipped.");
            continue;
        }
        if (type == "none") {
            if (!trim(raw).empty()) {
                noteInvalidRow(std::string(sourceName) + " line " + std::to_string(lineNumber) +
                               " has a value for type none; the row was skipped.");
                continue;
            }
            output[name] = Value{};
        } else if (type == "bool") {
            const auto lowered = lower(trim(raw));
            if (lowered != "true" && lowered != "false") {
                noteInvalidRow(std::string(sourceName) + " line " + std::to_string(lineNumber) + " has an invalid bool value.");
                continue;
            }
            output[name] = Value(lowered == "true");
        } else if (type == "number") {
            const auto numberText = trim(raw);
            // Reuse the Value parser so persisted numeric values work on standard
            // libraries that do not implement floating-point std::from_chars
            // (notably the Apple libc++ shipped with current Xcode runners).
            const Value parsed = Value::fromString(numberText);
            const double number = parsed.numberOr();
            if (!parsed.isNumber() || !std::isfinite(number)) {
                noteInvalidRow(std::string(sourceName) + " line " + std::to_string(lineNumber) + " has an invalid number.");
                continue;
            }
            output[name] = Value(number);
        } else if (type == "string") {
            output[name] = Value(unescapeText(raw));
        } else {
            noteInvalidRow(std::string(sourceName) + " line " + std::to_string(lineNumber) +
                           " has an unknown type; use string, number, bool, or none.");
        }
    }

    if (warningCount > maxWarningsToShow) {
        warning += "\n... " + std::to_string(warningCount - maxWarningsToShow) +
                   " additional invalid row(s) omitted from this diagnostic.";
    }
    return warningCount == 0;
}

void appendU32(std::string& out, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>((value >> shift) & 0xffU));
}
std::uint32_t readU32(std::string_view input, std::size_t offset) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + 1])) << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + 2])) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + 3])) << 24U);
}

// CXVB cache header: four-byte magic, 32-bit format marker, 32-bit uncompressed size,
// then either zlib-compressed bytes or the built-in PackBits-style fallback. The declared
// size is checked before allocating memory during decompression.
bool compressCache(std::string_view source, std::string& result, std::string& error) {
    result.clear();
    result.append("CXVB", 4);
    appendU32(result, 1U);
    if (source.size() > std::numeric_limits<std::uint32_t>::max()) { error = "Variable store is too large for the compressed cache."; return false; }
    appendU32(result, static_cast<std::uint32_t>(source.size()));
#ifdef CX_HAVE_ZLIB
    uLongf size = compressBound(static_cast<uLong>(source.size()));
    std::string compressed(static_cast<std::size_t>(size), '\0');
    const int status = compress2(reinterpret_cast<Bytef*>(compressed.data()), &size,
                                 reinterpret_cast<const Bytef*>(source.data()), static_cast<uLong>(source.size()), Z_BEST_COMPRESSION);
    if (status != Z_OK) { error = "zlib could not compress variables.cxv (code " + std::to_string(status) + ")."; return false; }
    compressed.resize(static_cast<std::size_t>(size));
    result += compressed;
#else
    // A portable compact fallback uses a simple PackBits-style encoding. The binary file
    // still has the same CXVB header, but flag 0 denotes fallback encoding.
    result[4] = 0;
    std::string packed;
    for (std::size_t i = 0; i < source.size();) {
        std::size_t run = 1;
        while (i + run < source.size() && source[i + run] == source[i] && run < 127) ++run;
        if (run >= 4) {
            packed.push_back(static_cast<char>(0x80U | static_cast<unsigned char>(run)));
            packed.push_back(source[i]);
            i += run;
        } else {
            const std::size_t start = i;
            i += run;
            while (i < source.size() && i - start < 127) {
                run = 1;
                while (i + run < source.size() && source[i + run] == source[i] && run < 4) ++run;
                if (run >= 4) break;
                i += run;
            }
            packed.push_back(static_cast<char>(static_cast<unsigned char>(i - start)));
            packed.append(source.substr(start, i - start));
        }
    }
    result += packed;
#endif
    return true;
}

bool decompressCache(std::string_view input, std::string& output, std::string& error) {
    if (input.size() < 12 || input.substr(0, 4) != "CXVB") { error = "variables.cxv is not a CX variable cache."; return false; }

    // The fourth header field is a codec marker, not a universal format version:
    // 0 means our built-in PackBits-style codec; 1 means zlib/DEFLATE. Decode the
    // built-in format in either build so caches can travel between machines with
    // and without zlib installed.
    const auto codec = readU32(input, 4);
    const auto size = readU32(input, 8);
    constexpr std::uint32_t maxCacheBytes = 256U * 1024U * 1024U;
    if (codec > 1U || size > maxCacheBytes) {
        error = "variables.cxv has an unsupported compression format or unreasonable size.";
        return false;
    }

    const auto payload = input.substr(12);
    if (codec == 1U) {
#ifdef CX_HAVE_ZLIB
        // The declared size is capped above before allocating the destination buffer.
        output.assign(static_cast<std::size_t>(size), '\0');
        uLongf actual = static_cast<uLongf>(size);
        const int status = uncompress(reinterpret_cast<Bytef*>(output.data()), &actual,
                                      reinterpret_cast<const Bytef*>(payload.data()),
                                      static_cast<uLong>(payload.size()));
        if (status != Z_OK || actual != size) {
            error = "variables.cxv could not be decompressed; the CSV copy remains editable and recoverable.";
            return false;
        }
        return true;
#else
        error = "variables.cxv uses zlib compression, but this build has no zlib support. Keep or restore variables.csv, or open the project in a zlib-enabled build.";
        return false;
#endif
    }

    // Codec 0 is the portable PackBits-style fallback. Keep all bounds checks
    // against the header's expected size so damaged counts cannot expand output
    // past the validated allocation budget or silently accept truncated data.
    output.clear();
    output.reserve(static_cast<std::size_t>(size));
    std::size_t i = 0;
    while (i < payload.size() && output.size() < size) {
        const auto token = static_cast<unsigned char>(payload[i++]);
        if (token & 0x80U) {
            const std::size_t run = token & 0x7fU;
            if (run == 0 || i >= payload.size() || run > static_cast<std::size_t>(size) - output.size()) {
                error = "variables.cxv fallback compression data is damaged.";
                return false;
            }
            output.append(run, payload[i++]);
        } else {
            const std::size_t count = token;
            if (count == 0 || i + count > payload.size() || count > static_cast<std::size_t>(size) - output.size()) {
                error = "variables.cxv fallback compression data is damaged.";
                return false;
            }
            output.append(payload.substr(i, count));
            i += count;
        }
    }
    if (output.size() != size || i != payload.size()) {
        error = "variables.cxv decompressed to an unexpected size or contains trailing data.";
        return false;
    }
    return true;
}

// Writers coordinate through one project-local lock file. The lock is deliberately kept
// on disk (and never deleted) because deleting a lock path while another process has it
// open can create two independently locked inodes and defeat mutual exclusion.
class SaveLock {
public:
    SaveLock() = default;
    SaveLock(const SaveLock&) = delete;
    SaveLock& operator=(const SaveLock&) = delete;

    ~SaveLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            if (locked_) UnlockFileEx(handle_, 0, 1, 0, &overlapped_);
            CloseHandle(handle_);
        }
#else
        if (fd_ >= 0) {
            (void)::flock(fd_, LOCK_UN);
            (void)::close(fd_);
        }
#endif
    }

    bool acquire(const std::filesystem::path& root, std::string& error) {
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        if (ec) {
            error = "Could not create the variable storage folder: " + ec.message();
            return false;
        }
        const auto lockPath = root / ".variables.lock";
        constexpr auto timeout = std::chrono::seconds(10);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
#ifdef _WIN32
        handle_ = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            error = "Could not open the variable-save lock: " +
                    std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
            return false;
        }
        do {
            if (LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                           0, 1, 0, &overlapped_)) {
                locked_ = true;
                return true;
            }
            const DWORD code = GetLastError();
            if (code != ERROR_LOCK_VIOLATION) {
                error = "Could not acquire the variable-save lock: " +
                        std::error_code(static_cast<int>(code), std::system_category()).message();
                CloseHandle(handle_);
                handle_ = INVALID_HANDLE_VALUE;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
#else
        fd_ = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (fd_ < 0) {
            error = "Could not open the variable-save lock: " + std::error_code(errno, std::generic_category()).message();
            return false;
        }
        do {
            if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) return true;
            if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
                error = "Could not acquire the variable-save lock: " +
                        std::error_code(errno, std::generic_category()).message();
                (void)::close(fd_);
                fd_ = -1;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
#endif
        error = "Timed out after 10 seconds waiting for another process to save this project's variables.";
#ifdef _WIN32
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
#else
        (void)::close(fd_);
        fd_ = -1;
#endif
        return false;
    }

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_{};
    bool locked_ = false;
#else
    int fd_ = -1;
#endif
};

#ifndef _WIN32
bool syncDirectory(const std::filesystem::path& directory, std::string& error) {
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    const int fd = ::open(directory.c_str(), flags);
    if (fd < 0) {
        error = "Could not open the save directory for durability sync: " +
                std::error_code(errno, std::generic_category()).message();
        return false;
    }
    const int syncResult = ::fsync(fd);
    const int syncError = errno;
    (void)::close(fd);
    if (syncResult != 0) {
        error = "Could not sync the save directory after replacement: " +
                std::error_code(syncError, std::generic_category()).message();
        return false;
    }
    return true;
}
#endif

// Write beside the destination, force the new file's contents to stable storage, then
// atomically replace the destination. POSIX also syncs the containing directory so the
// rename itself is durable across a system crash on filesystems honoring fsync semantics.
// Windows uses FlushFileBuffers plus MoveFileExW(MOVEFILE_WRITE_THROUGH).
bool writeAtomic(const std::filesystem::path& path, std::string_view contents, std::string& error) {
    std::error_code ec;
    auto parent = path.parent_path();
    if (parent.empty()) parent = ".";
    std::filesystem::create_directories(parent, ec);
    if (ec) { error = "Could not create the variable storage folder: " + ec.message(); return false; }
    auto temporary = path;
    temporary += ".tmp";
#ifdef _WIN32
    HANDLE output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = "Could not write " + path.filename().string() + ": " +
                std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
        return false;
    }
    std::size_t offset = 0;
    bool writeOk = true;
    while (offset < contents.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(contents.size() - offset,
                                                   static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(output, contents.data() + offset, chunk, &written, nullptr) || written == 0) {
            writeOk = false;
            break;
        }
        offset += written;
    }
    if (writeOk && !FlushFileBuffers(output)) writeOk = false;
    DWORD writeError = writeOk ? ERROR_SUCCESS : GetLastError();
    if (!CloseHandle(output) && writeOk) {
        writeOk = false;
        writeError = GetLastError();
    }
    if (!writeOk) {
        std::filesystem::remove(temporary, ec);
        error = "Writing or flushing " + path.filename().string() + " failed: " +
                std::error_code(static_cast<int>(writeError), std::system_category()).message();
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto replaceError = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        std::filesystem::remove(temporary, ec);
        error = "Could not atomically replace " + path.filename().string() + ": " + replaceError.message();
        return false;
    }
#else
    int openFlags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;
#ifdef O_NOFOLLOW
    openFlags |= O_NOFOLLOW;
#endif
    const int output = ::open(temporary.c_str(), openFlags, 0666);
    if (output < 0) {
        error = "Could not write " + path.filename().string() + ": " +
                std::error_code(errno, std::generic_category()).message();
        return false;
    }
    std::size_t offset = 0;
    bool writeOk = true;
    int writeError = 0;
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto chunk = std::min<std::size_t>(remaining,
                            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const ssize_t written = ::write(output, contents.data() + offset, chunk);
        if (written < 0) {
            if (errno == EINTR) continue;
            writeOk = false;
            writeError = errno;
            break;
        }
        if (written == 0) {
            writeOk = false;
            writeError = EIO;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (writeOk && ::fsync(output) != 0) {
        writeOk = false;
        writeError = errno;
    }
    // POSIX close() must not be retried after EINTR: on Linux the descriptor is already closed.
    if (::close(output) != 0 && writeOk) {
        writeOk = false;
        writeError = errno;
    }
    if (!writeOk) {
        std::filesystem::remove(temporary, ec);
        error = "Writing or syncing " + path.filename().string() + " failed: " +
                std::error_code(writeError, std::generic_category()).message();
        return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        const auto replaceError = ec;
        std::filesystem::remove(temporary, ec);
        error = "Could not atomically replace " + path.filename().string() + ": " + replaceError.message();
        return false;
    }
    if (!syncDirectory(parent, error)) {
        error = path.filename().string() + " was replaced, but its containing directory could not be synced: " + error;
        return false;
    }
#endif
    return true;
}

bool readFile(const std::filesystem::path& path, std::string& contents) {
    // A save file is user data, but it is also untrusted input. Bound the read before
    // streaming a possibly accidental huge file into memory. The cache decoder has a
    // separate bound for the declared decompressed size.
    constexpr std::uintmax_t maxVariableFileBytes = 256ULL * 1024ULL * 1024ULL;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > maxVariableFileBytes) return false;

    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) return false;
    contents = std::move(buffer).str();
    return contents.size() == size;
}

// The generation manifest is a small integrity record for the CSV/cache pair. It is
// deliberately not a source of variable values: CSV remains authoritative whenever it
// exists. FNV-1a is used only to detect accidental mismatch/corruption, not for security.
struct SaveManifest {
    std::uint64_t generation = 0;
    std::uint64_t csvSize = 0;
    std::uint64_t csvHash = 0;
    std::uint64_t cacheSize = 0;
    std::uint64_t cacheHash = 0;
};

std::uint64_t hashBytes(std::string_view bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string serializeManifest(const SaveManifest& manifest) {
    std::ostringstream output;
    output << "CXVM1\n"
           << "generation=" << manifest.generation << '\n'
           << "csv_size=" << manifest.csvSize << '\n'
           << "csv_hash=" << std::hex << std::setw(16) << std::setfill('0') << manifest.csvHash << '\n'
           << "cache_size=" << std::dec << manifest.cacheSize << '\n'
           << "cache_hash=" << std::hex << std::setw(16) << std::setfill('0') << manifest.cacheHash << '\n';
    return output.str();
}

bool parseUnsigned(std::string_view text, std::uint64_t& value, int base = 10) {
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parseManifest(std::string_view contents, SaveManifest& manifest, std::string& error) {
    // Fixed field order makes the format easy to inspect and rejects duplicate/unknown
    // fields rather than silently accepting a partially understood future format.
    std::istringstream input{std::string(contents)};
    std::string line;
    std::array<std::string, 6> lines{};
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!std::getline(input, lines[i])) {
            error = "variables.meta is incomplete.";
            return false;
        }
        if (!lines[i].empty() && lines[i].back() == '\r') lines[i].pop_back();
    }
    if (std::getline(input, line)) {
        error = "variables.meta contains unexpected trailing data.";
        return false;
    }
    if (lines[0] != "CXVM1") {
        error = "variables.meta has an unsupported format marker.";
        return false;
    }
    constexpr std::array<std::string_view, 5> keys = {
        "generation=", "csv_size=", "csv_hash=", "cache_size=", "cache_hash="
    };
    std::array<std::uint64_t, 5> values{};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const std::string_view field(lines[i + 1]);
        if (!field.starts_with(keys[i])) {
            error = "variables.meta has an unexpected or reordered field.";
            return false;
        }
        const auto raw = field.substr(keys[i].size());
        const int base = (i == 2 || i == 4) ? 16 : 10;
        if ((base == 16 && raw.size() != 16) || !parseUnsigned(raw, values[i], base)) {
            error = "variables.meta contains an invalid numeric field.";
            return false;
        }
    }
    if (values[0] == 0) {
        error = "variables.meta has an invalid zero generation.";
        return false;
    }
    manifest = SaveManifest{values[0], values[1], values[2], values[3], values[4]};
    return true;
}

bool readManifest(const std::filesystem::path& path, SaveManifest& manifest,
                  bool& exists, std::string& error) {
    std::error_code ec;
    exists = std::filesystem::exists(path, ec);
    if (ec) {
        error = "Could not inspect variables.meta: " + ec.message();
        return false;
    }
    if (!exists) {
        error.clear();
        return true;
    }
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError || size > 1024) {
        error = sizeError
            ? "variables.meta could not be inspected: " + sizeError.message()
            : "variables.meta exceeds the 1 KiB metadata limit.";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "variables.meta could not be opened for reading.";
        return false;
    }
    std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (contents.size() != size) {
        error = "variables.meta could not be read completely.";
        return false;
    }
    return parseManifest(contents, manifest, error);
}

bool manifestMatchesCsv(const SaveManifest& manifest, std::string_view csv) {
    return manifest.csvSize == csv.size() && manifest.csvHash == hashBytes(csv);
}

bool manifestMatchesCache(const SaveManifest& manifest, std::string_view cache) {
    return manifest.cacheSize == cache.size() && manifest.cacheHash == hashBytes(cache);
}

SaveManifest makeManifest(std::string_view csv, std::string_view cache, std::uint64_t generation) {
    return SaveManifest{generation, static_cast<std::uint64_t>(csv.size()), hashBytes(csv),
                        static_cast<std::uint64_t>(cache.size()), hashBytes(cache)};
}

// Keep a one-generation manual recovery copy before replacing an existing regular save
// file. This backup is deliberately not an automatic load source: variables.csv remains
// authoritative, and the engine must not guess which old file a project owner wants restored.
bool backupExistingRegularFile(const std::filesystem::path& destination, std::string& error) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(destination, ec);
    if (ec) {
        error = "Could not inspect existing " + destination.filename().string() + " before saving: " + ec.message();
        return false;
    }
    if (!exists) return true;

    const bool regularFile = std::filesystem::is_regular_file(destination, ec);
    if (ec) {
        error = "Could not inspect existing " + destination.filename().string() + " before saving: " + ec.message();
        return false;
    }
    // Unexpected destinations such as directories are not replaced or copied. The normal
    // atomic replacement below will fail safely and preserve them, with a clearer error.
    if (!regularFile) return true;

    std::string previousContents;
    if (!readFile(destination, previousContents)) {
        error = "Could not preserve existing " + destination.filename().string() +
                " before saving; it may be unreadable or exceed the 256 MiB safety limit.";
        return false;
    }

    auto backupPath = destination;
    backupPath += ".bak";
    std::string backupError;
    if (!writeAtomic(backupPath, previousContents, backupError)) {
        error = "Could not update recovery backup " + backupPath.filename().string() +
                "; the original save was left unchanged: " + backupError;
        return false;
    }
    return true;
}

} // namespace

// Loading precedence is deliberate: if variables.csv exists, it wins even if some rows
// are malformed. Only when the CSV is absent do we attempt the compressed cache and rebuild
// the editable CSV. This prevents deleted CSV rows from resurrecting from an older cache.
bool VariableStorage::load(const std::filesystem::path& root, VariableMap& destination, std::string& warning) {
    destination.clear();
    warning.clear();
    const auto csvPath = root / "variables.csv";
    const auto binaryPath = root / "variables.cxv";
    std::string contents;
    std::error_code ec;

    // Check the authoritative file first and propagate filesystem inspection errors.
    // Treating an inaccessible CSV as "missing" could accidentally revive stale cache data.
    const bool csvExists = std::filesystem::exists(csvPath, ec);
    if (ec) {
        warning = "Could not inspect variables.csv: " + ec.message();
        return false;
    }
    if (csvExists) {
        if (!readFile(csvPath, contents)) {
            warning = "variables.csv could not be read, is not a regular file, or exceeds the 256 MiB safety limit.";
            return false;
        }
        // A present CSV is authoritative even if some of its rows are malformed. Keep
        // valid rows, warn about invalid ones, and never fall back to an older cache:
        // doing so could resurrect variables the project owner intentionally deleted.
        (void)parseCsv(contents, destination, warning);

        // When the CSV still matches the last committed generation, the manifest can
        // also identify a damaged/stale cache without risking the editable source. A
        // hand-edited CSV deliberately stops matching and remains warning-free here.
        SaveManifest manifest;
        bool manifestExists = false;
        std::string manifestError;
        if (readManifest(root / "variables.meta", manifest, manifestExists, manifestError) &&
            manifestExists && manifestMatchesCsv(manifest, contents)) {
            const bool cachePresent = std::filesystem::exists(binaryPath, ec);
            if (!ec && cachePresent) {
                std::string currentCache;
                if (readFile(binaryPath, currentCache) && !manifestMatchesCache(manifest, currentCache)) {
                    if (!warning.empty()) warning += '\n';
                    warning += "variables.cxv does not match the committed variables.meta generation; variables.csv remains authoritative, but cache recovery is disabled until a successful save refreshes it.";
                }
            }
        }
        return true;
    }

    const bool cacheExists = std::filesystem::exists(binaryPath, ec);
    if (ec) {
        warning = "Could not inspect variables.cxv: " + ec.message();
        return false;
    }
    if (cacheExists) {
        std::string binary, cacheError;
        if (!readFile(binaryPath, binary)) {
            warning = "variables.cxv could not be read, is not a regular file, or exceeds the 256 MiB safety limit.";
            return false;
        }

        // Newer projects carry a generation manifest. If it exists, cache restoration is
        // permitted only when the exact cache bytes match the last fully committed pair.
        // Projects from earlier releases have no manifest and retain legacy recovery behavior.
        SaveManifest manifest;
        bool manifestExists = false;
        std::string manifestError;
        const bool manifestValid = readManifest(root / "variables.meta", manifest, manifestExists, manifestError);
        if (!manifestValid || (manifestExists && !manifestMatchesCache(manifest, binary))) {
            destination.clear();
            warning = manifestValid
                ? "variables.cxv does not match variables.meta; cache recovery was refused to avoid restoring an unverified generation. "
                : "variables.meta is invalid; cache recovery was refused because the saved generation cannot be verified. " + manifestError;
            return false;
        }
        if (!decompressCache(binary, contents, cacheError)) {
            warning = cacheError.empty() ? "variables.cxv could not be decompressed." : cacheError;
            return false;
        }

        // Unlike a hand-edited CSV, cache contents were written by the engine and should
        // be complete. If the decoded table is malformed, fail recovery instead of making
        // a new CSV from a partial set of values and hiding the damaged rows.
        std::string parseWarning;
        VariableMap recoveredValues;
        if (!parseCsv(contents, recoveredValues, parseWarning, "variables.cxv")) {
            warning = "variables.cxv decoded, but its variable table is damaged; recovery was not committed. " + parseWarning;
            return false;
        }

        // Restoring a missing CSV is itself a write operation. Coordinate it with save()
        // and re-check both the authoritative path and cache generation after acquiring
        // the lock: another process may have completed a newer save while we were decoding.
        SaveLock recoveryLock;
        std::string saveError;
        if (!recoveryLock.acquire(root, saveError)) {
            destination = std::move(recoveredValues);
            warning = "Variables were recovered from variables.cxv in memory, but the project lock could not be acquired to restore variables.csv: " + saveError;
            return true;
        }
        const bool csvExistsAfterLock = std::filesystem::exists(csvPath, ec);
        if (ec) {
            destination = std::move(recoveredValues);
            warning = "Variables were recovered from variables.cxv in memory, but variables.csv could not be rechecked after acquiring the project lock: " + ec.message();
            return true;
        }
        if (csvExistsAfterLock) {
            std::string currentCsv;
            if (!readFile(csvPath, currentCsv)) {
                warning = "A newer variables.csv appeared during cache recovery but could not be read; no recovered values were committed to memory.";
                return false;
            }
            destination.clear();
            (void)parseCsv(currentCsv, destination, warning);
            return true;
        }

        std::string lockedBinary, lockedCacheError;
        if (!readFile(binaryPath, lockedBinary)) {
            warning = "variables.cxv disappeared or became unreadable while cache recovery waited for the project lock.";
            return false;
        }
        SaveManifest lockedManifest;
        bool lockedManifestExists = false;
        std::string lockedManifestError;
        const bool lockedManifestValid = readManifest(root / "variables.meta", lockedManifest,
                                                       lockedManifestExists, lockedManifestError);
        if (!lockedManifestValid || (lockedManifestExists && !manifestMatchesCache(lockedManifest, lockedBinary))) {
            warning = "variables.cxv changed while cache recovery waited for the project lock; the new cache does not match variables.meta, so recovery was refused.";
            return false;
        }
        std::string lockedContents;
        if (!decompressCache(lockedBinary, lockedContents, lockedCacheError)) {
            warning = lockedCacheError.empty() ? "variables.cxv could not be decompressed after acquiring the project lock." : lockedCacheError;
            return false;
        }
        VariableMap lockedValues;
        std::string lockedParseWarning;
        if (!parseCsv(lockedContents, lockedValues, lockedParseWarning, "variables.cxv")) {
            warning = "variables.cxv decoded, but its variable table is damaged; recovery was not committed. " + lockedParseWarning;
            return false;
        }
        if (!writeAtomic(csvPath, lockedContents, saveError)) {
            destination = std::move(lockedValues);
            warning = "Variables were recovered from variables.cxv in memory, but variables.csv could not be reconstructed: " + saveError;
            return true;
        }
        destination = std::move(lockedValues);
        warning = std::move(lockedParseWarning);
        if (!lockedManifestExists) {
            const auto migratedManifest = makeManifest(lockedContents, lockedBinary, 1);
            std::string manifestWriteError;
            if (!writeAtomic(root / "variables.meta", serializeManifest(migratedManifest), manifestWriteError)) {
                if (!warning.empty()) warning += '\n';
                warning += "The legacy cache was recovered and variables.csv restored, but variables.meta could not be created: " + manifestWriteError;
            }
        }
        return true;
    }

    // A manifest means this project has had a complete saved generation. If both data
    // representations are now absent, returning an empty map would disguise missing user
    // data as a fresh project. Fail clearly instead of silently starting with no globals.
    SaveManifest orphanedManifest;
    bool orphanedManifestExists = false;
    std::string orphanedManifestError;
    const bool orphanedManifestValid = readManifest(root / "variables.meta", orphanedManifest,
                                                      orphanedManifestExists, orphanedManifestError);
    if (!orphanedManifestValid || orphanedManifestExists) {
        warning = !orphanedManifestValid
            ? "Could not validate variables.meta while both variables.csv and variables.cxv are missing; no variables were loaded: " + orphanedManifestError
            : "variables.meta records a saved generation, but both variables.csv and variables.cxv are missing; no variables were loaded.";
        return false;
    }
    return true;
}

// CSV is replaced first because it is authoritative on the next load. The cache is then
// replaced too. A cache-write failure can therefore leave the cache stale, but does not make
// the successful CSV write disappear; recovery policy for mismatched versions is tracked in
// the roadmap rather than hidden behind a risky delete-and-retry fallback.
bool VariableStorage::save(const std::filesystem::path& root, const VariableMap& variables, std::string& error) {
    error.clear();
    const std::string csv = serializeCsv(variables);
    std::string compressed;
    if (!compressCache(csv, compressed, error)) return false;

    // Only one process may rotate backups and replace this project's paired save files
    // at a time. The lock is held through every file and directory durability barrier.
    SaveLock lock;
    if (!lock.acquire(root, error)) return false;

    const auto manifestPath = root / "variables.meta";
    std::error_code inspectError;
    const bool manifestPathExists = std::filesystem::exists(manifestPath, inspectError);
    if (inspectError) {
        error = "Could not inspect variables.meta before saving: " + inspectError.message();
        return false;
    }
    if (manifestPathExists && !std::filesystem::is_regular_file(manifestPath, inspectError)) {
        error = inspectError ? "Could not inspect variables.meta before saving: " + inspectError.message()
                             : "variables.meta exists but is not a regular file; refusing to replace an unexpected destination.";
        return false;
    }
    if (inspectError) {
        error = "Could not inspect variables.meta before saving: " + inspectError.message();
        return false;
    }

    SaveManifest previousManifest;
    bool previousManifestExists = false;
    std::string previousManifestError;
    const bool previousManifestValid = readManifest(manifestPath, previousManifest,
                                                      previousManifestExists, previousManifestError);
    if (previousManifestValid && previousManifestExists &&
        previousManifest.generation == std::numeric_limits<std::uint64_t>::max()) {
        error = "variables.meta generation counter is exhausted; save was not started.";
        return false;
    }
    // A malformed old manifest is not a reason to discard an otherwise editable CSV.
    // The next complete save establishes a fresh, validated generation record.
    const std::uint64_t nextGeneration = previousManifestValid && previousManifestExists
        ? previousManifest.generation + 1
        : 1;

    // Back up both prior representations before replacing either one. If backing up
    // either save fails, abort before changing the authoritative CSV or its cache.
    if (!backupExistingRegularFile(root / "variables.csv", error)) return false;
    if (!backupExistingRegularFile(root / "variables.cxv", error)) return false;
    if (!writeAtomic(root / "variables.csv", csv, error)) return false;
    if (!writeAtomic(root / "variables.cxv", compressed, error)) {
        // The CSV is authoritative once installed, so do not mislead callers into thinking
        // neither file changed. The old manifest intentionally remains: it identifies the
        // last fully committed pair, not this partially updated one.
        error = "variables.csv was saved, but variables.cxv could not be updated: " + error;
        return false;
    }

    const auto nextManifest = makeManifest(csv, compressed, nextGeneration);
    std::string manifestError;
    if (!writeAtomic(manifestPath, serializeManifest(nextManifest), manifestError)) {
        error = "variables.csv and variables.cxv were saved, but variables.meta could not be updated; cache recovery will be refused until a complete save refreshes the manifest: " + manifestError;
        return false;
    }
    return true;
}

} // namespace cx
