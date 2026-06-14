// d2d_helpers.cpp - factory singletons.

#include "d2d_helpers.h"

namespace apple1::win {

namespace {
ID2D1Factory*    g_d2d    = nullptr;
IDWriteFactory*  g_dwrite = nullptr;
}

ID2D1Factory* d2d_factory() {
    if (!g_d2d) {
        // Single-threaded factory is enough - both windows render from the
        // same UI thread.
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
    }
    return g_d2d;
}

IDWriteFactory* dwrite_factory() {
    if (!g_dwrite) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                            __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&g_dwrite));
    }
    return g_dwrite;
}

void shutdown_factories() {
    if (g_dwrite) { g_dwrite->Release(); g_dwrite = nullptr; }
    if (g_d2d)    { g_d2d->Release();    g_d2d    = nullptr; }
}

} // namespace apple1::win
