// d2d_helpers.h - one-stop owner of the D2D/DWrite factory singletons and
// a tiny COM-ptr helper.  Two windows (main + debugger) both render with
// Direct2D, so we share the factories rather than create one per window.

#pragma once

#include <d2d1.h>
#include <dwrite.h>

namespace apple1::win {

// Process-wide factory singletons.  Created lazily on first call.
ID2D1Factory*  d2d_factory();
IDWriteFactory* dwrite_factory();

// Tear down singletons on app exit.  Call once from WinMain after the
// message loop returns.
void shutdown_factories();

// Tiny RAII wrapper for IUnknown-derived COM pointers.  Not as feature-
// complete as Microsoft::WRL::ComPtr but enough for our needs and no
// extra headers.
template <typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&)            = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { release(); p = o.p; o.p = nullptr; }
        return *this;
    }
    ~ComPtr() { release(); }

    void release() { if (p) { p->Release(); p = nullptr; } }
    T* get() const { return p; }
    T** addr()     { release(); return &p; }
    T*  operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

} // namespace apple1::win
