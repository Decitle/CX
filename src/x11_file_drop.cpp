#include "cx/x11_file_drop.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace cx {

// This file is an optional operating-system adapter. It translates native X11 drag/drop events
// into plain file paths and window coordinates; core Runtime code never depends on Xlib.
namespace {

std::string decodeUriPath(std::string uri) {
    if (!uri.starts_with("file://")) return {};
    uri.erase(0, 7);
    // file://localhost/path is equivalent to file:///path. Ignore remote hosts.
    if (uri.starts_with("localhost/")) uri.erase(0, 9);
    else if (!uri.starts_with('/')) return {};
    std::string out;
    out.reserve(uri.size());
    auto hexValue = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const int high = hexValue(uri[i + 1]);
            const int low = hexValue(uri[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        out.push_back(uri[i]);
    }
    return out;
}

std::int16_t packedSigned16(unsigned long value) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(value & 0xffffUL));
}

} // namespace

struct X11FileDropBridge::State {
    Display* display{};
    Window target{};
    Window source{};
    Atom xdndAware{};
    Atom xdndEnter{};
    Atom xdndPosition{};
    Atom xdndStatus{};
    Atom xdndLeave{};
    Atom xdndDrop{};
    Atom xdndFinished{};
    Atom xdndSelection{};
    Atom xdndTypeList{};
    Atom xdndActionCopy{};
    Atom uriList{};
    Atom property{};
    int mouseX{};
    int mouseY{};
    bool acceptsUriList{};
    bool waitingForSelection{};

    State(std::uintptr_t handle) {
        display = XOpenDisplay(nullptr);
        if (!display || handle == 0U) return;
        target = static_cast<Window>(handle);
        xdndAware = XInternAtom(display, "XdndAware", False);
        xdndEnter = XInternAtom(display, "XdndEnter", False);
        xdndPosition = XInternAtom(display, "XdndPosition", False);
        xdndStatus = XInternAtom(display, "XdndStatus", False);
        xdndLeave = XInternAtom(display, "XdndLeave", False);
        xdndDrop = XInternAtom(display, "XdndDrop", False);
        xdndFinished = XInternAtom(display, "XdndFinished", False);
        xdndSelection = XInternAtom(display, "XdndSelection", False);
        xdndTypeList = XInternAtom(display, "XdndTypeList", False);
        xdndActionCopy = XInternAtom(display, "XdndActionCopy", False);
        uriList = XInternAtom(display, "text/uri-list", False);
        property = XInternAtom(display, "CXEngineXdndData", False);
        const unsigned long version = 5;
        XChangeProperty(display, target, xdndAware, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&version), 1);
        XSelectInput(display, target, StructureNotifyMask | PropertyChangeMask);
        XFlush(display);
    }

    ~State() {
        if (display) XCloseDisplay(display);
    }

    bool available() const noexcept { return display && target != 0; }

    bool inspectTypes(Window dragSource, const XClientMessageEvent& enter) {
        if ((enter.data.l[1] & 1L) != 0L) {
            Atom actualType{};
            int actualFormat{};
            unsigned long itemCount{}, bytesAfter{};
            unsigned char* raw{};
            const int status = XGetWindowProperty(display, dragSource, xdndTypeList, 0, 256, False,
                                                   XA_ATOM, &actualType, &actualFormat, &itemCount,
                                                   &bytesAfter, &raw);
            bool found = false;
            if (status == Success && raw && actualFormat == 32 && actualType == XA_ATOM) {
                const auto* atoms = reinterpret_cast<const Atom*>(raw);
                for (unsigned long i = 0; i < itemCount; ++i) if (atoms[i] == uriList) found = true;
            }
            if (raw) XFree(raw);
            return found;
        }
        return static_cast<Atom>(enter.data.l[2]) == uriList ||
               static_cast<Atom>(enter.data.l[3]) == uriList ||
               static_cast<Atom>(enter.data.l[4]) == uriList;
    }

    void sendStatus(bool accept) {
        if (!source) return;
        XEvent reply{};
        reply.xclient.type = ClientMessage;
        reply.xclient.display = display;
        reply.xclient.window = source;
        reply.xclient.message_type = xdndStatus;
        reply.xclient.format = 32;
        reply.xclient.data.l[0] = static_cast<long>(target);
        reply.xclient.data.l[1] = accept ? 1L : 0L;
        reply.xclient.data.l[2] = 0L;
        reply.xclient.data.l[3] = 0L;
        reply.xclient.data.l[4] = accept ? static_cast<long>(xdndActionCopy) : 0L;
        XSendEvent(display, source, False, NoEventMask, &reply);
        XFlush(display);
    }

    void sendFinished(bool success) {
        if (!source) return;
        XEvent reply{};
        reply.xclient.type = ClientMessage;
        reply.xclient.display = display;
        reply.xclient.window = source;
        reply.xclient.message_type = xdndFinished;
        reply.xclient.format = 32;
        reply.xclient.data.l[0] = static_cast<long>(target);
        reply.xclient.data.l[1] = success ? 1L : 0L;
        reply.xclient.data.l[2] = success ? static_cast<long>(xdndActionCopy) : 0L;
        XSendEvent(display, source, False, NoEventMask, &reply);
        XFlush(display);
    }
};

X11FileDropBridge::X11FileDropBridge(std::uintptr_t nativeWindowHandle)
    : state_(std::make_unique<State>(nativeWindowHandle)) {}
X11FileDropBridge::~X11FileDropBridge() = default;
X11FileDropBridge::X11FileDropBridge(X11FileDropBridge&&) noexcept = default;
X11FileDropBridge& X11FileDropBridge::operator=(X11FileDropBridge&&) noexcept = default;

bool X11FileDropBridge::available() const noexcept { return state_ && state_->available(); }

std::vector<NativeFileDrop> X11FileDropBridge::poll() {
    std::vector<NativeFileDrop> drops;
    if (!available()) return drops;
    auto& s = *state_;
    while (XPending(s.display) > 0) {
        XEvent event{};
        XNextEvent(s.display, &event);
        if (event.type == ClientMessage && event.xclient.format == 32) {
            const auto& message = event.xclient;
            if (message.message_type == s.xdndEnter) {
                s.source = static_cast<Window>(message.data.l[0]);
                s.acceptsUriList = s.inspectTypes(s.source, message);
                s.waitingForSelection = false;
            } else if (message.message_type == s.xdndPosition) {
                const auto packed = static_cast<unsigned long>(message.data.l[2]);
                const int rootX = static_cast<int>(packedSigned16(packed >> 16U));
                const int rootY = static_cast<int>(packedSigned16(packed));
                Window child{};
                int localX{}, localY{};
                (void)XTranslateCoordinates(s.display, DefaultRootWindow(s.display), s.target,
                                            rootX, rootY, &localX, &localY, &child);
                s.mouseX = localX;
                s.mouseY = localY;
                s.sendStatus(s.acceptsUriList);
            } else if (message.message_type == s.xdndLeave) {
                s.source = 0;
                s.acceptsUriList = false;
                s.waitingForSelection = false;
            } else if (message.message_type == s.xdndDrop) {
                if (s.acceptsUriList) {
                    const Time timestamp = static_cast<Time>(message.data.l[2]);
                    XConvertSelection(s.display, s.xdndSelection, s.uriList, s.property, s.target, timestamp);
                    XFlush(s.display);
                    s.waitingForSelection = true;
                } else s.sendFinished(false);
            }
        } else if (event.type == SelectionNotify && s.waitingForSelection && event.xselection.selection == s.xdndSelection) {
            Atom actualType{};
            int actualFormat{};
            unsigned long itemCount{}, bytesAfter{};
            unsigned char* raw{};
            const int status = XGetWindowProperty(s.display, s.target, s.property, 0, 1L << 20, True,
                                                   AnyPropertyType, &actualType, &actualFormat, &itemCount,
                                                   &bytesAfter, &raw);
            bool success = false;
            if (status == Success && raw && actualFormat == 8 && actualType == s.uriList) {
                std::istringstream input(std::string(reinterpret_cast<char*>(raw), itemCount));
                std::string line;
                while (std::getline(input, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty() || line.front() == '#') continue;
                    auto decoded = decodeUriPath(line);
                    if (decoded.empty()) continue;
                    drops.push_back({std::filesystem::path(decoded), static_cast<float>(s.mouseX), static_cast<float>(s.mouseY)});
                    success = true;
                }
            }
            if (raw) XFree(raw);
            s.sendFinished(success);
            s.waitingForSelection = false;
            s.acceptsUriList = false;
            s.source = 0;
        }
    }
    return drops;
}

} // namespace cx
