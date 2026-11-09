#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace cx {

struct NativeFileDrop {
    std::filesystem::path path;
    float x{0.0F};
    float y{0.0F};
};

// Optional X11/XWayland XDND bridge. This keeps Xlib out of the public headers and
// only attaches existing files; the scripting Runtime remains platform-independent.
class X11FileDropBridge {
public:
    explicit X11FileDropBridge(std::uintptr_t nativeWindowHandle);
    ~X11FileDropBridge();
    X11FileDropBridge(const X11FileDropBridge&) = delete;
    X11FileDropBridge& operator=(const X11FileDropBridge&) = delete;
    X11FileDropBridge(X11FileDropBridge&&) noexcept;
    X11FileDropBridge& operator=(X11FileDropBridge&&) noexcept;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] std::vector<NativeFileDrop> poll();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace cx
