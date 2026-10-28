/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxWindowsSurface.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/HelpersCairo.h"
#include "mozilla/gfx/Logging.h"

#include "cairo.h"
#include "cairo-win32.h"

#include "nsString.h"
#include "nsIMemoryReporter.h"

class Win32SurfaceMemoryReporter final : public nsIMemoryReporter
{
    ~Win32SurfaceMemoryReporter() {}
public:
    NS_DECL_ISUPPORTS
    NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                              nsISupports* aData, bool aAnonymize) override
    {
        cairo_win32_memory_usage_t usage;
        cairo_win32_get_memory_usage(&usage);
        const char* names[] = {"unclassified", "content", "texture", "offscreen",
                               "native-drawing", "compositor", "snapshot"};
        static_assert(sizeof(names) / sizeof(names[0]) == CAIRO_WIN32_MEMORY_ROLE_COUNT,
                      "Every native backing role must be reported");
        for (size_t i = 0; i < CAIRO_WIN32_MEMORY_ROLE_COUNT; ++i) {
            nsAutoCString path("explicit/gfx/cairo-win32-dib/");
            path.Append(names[i]);
            nsresult rv = aHandleReport->Callback(EmptyCString(), path, KIND_NONHEAP,
              UNITS_BYTES, usage.dib_bytes[i], NS_LITERAL_CSTRING(
              "Native pixel storage of DIB sections owned by Cairo, measured once at the backing owner using actual bitmap stride and height. Excludes borrowed HDCs, D3D surfaces, wrapper objects and image aliases. These are allocated bytes, not resident pages. The legacy gfx-surface-win32 estimate overlaps this storage."), aData);
            NS_ENSURE_SUCCESS(rv, rv);
            nsAutoCString countPath("gfx-win32-surfaces/dib/");
            countPath.Append(names[i]);
            rv = aHandleReport->Callback(EmptyCString(), countPath, KIND_OTHER,
              UNITS_COUNT, usage.dib_count[i], NS_LITERAL_CSTRING(
              "Live Cairo-owned DIB sections by backing-owner role. Image views and additional references do not add another backing."), aData);
            NS_ENSURE_SUCCESS(rv, rv);
        }
        MOZ_COLLECT_REPORT("gfx-win32-ddb-estimated-bytes", KIND_OTHER, UNITS_BYTES,
          usage.ddb_estimated_bytes, "Estimated pixel bytes of Cairo-owned device-dependent bitmaps. Driver storage can differ and is not assumed to be process-private or resident. Excluded from explicit.");
        MOZ_COLLECT_REPORT("gfx-win32-surfaces/ddb", KIND_OTHER, UNITS_COUNT,
          usage.ddb_count, "Live Cairo-owned device-dependent bitmaps.");
        MOZ_COLLECT_REPORT("gfx-win32-surfaces/external", KIND_OTHER, UNITS_COUNT,
          usage.external_count, "Live Cairo surfaces wrapping externally owned HDC/D3D storage. No pixel bytes are charged to these wrappers.");
        MOZ_COLLECT_REPORT("gfx-win32-surfaces/unmeasured-bitmaps", KIND_OTHER, UNITS_COUNT,
          usage.unknown_bitmap_count, "Cairo-owned bitmaps whose backing could not be measured using GetObject. Their pixel bytes are not included in the measured totals.");
        return NS_OK;
    }
};

NS_IMPL_ISUPPORTS(Win32SurfaceMemoryReporter, nsIMemoryReporter)

void
gfxWindowsSurface::RegisterMemoryReporter()
{
    mozilla::RegisterStrongMemoryReporter(new Win32SurfaceMemoryReporter());
}

gfxWindowsSurface::gfxWindowsSurface(HDC dc, uint32_t flags) :
    mOwnsDC(false), mDC(dc), mWnd(nullptr)
{
    InitWithDC(flags);
}

void
gfxWindowsSurface::MakeInvalid(mozilla::gfx::IntSize& size)
{
    size = mozilla::gfx::IntSize(-1, -1);
}

gfxWindowsSurface::gfxWindowsSurface(const mozilla::gfx::IntSize& realSize, gfxImageFormat imageFormat) :
    mOwnsDC(false), mWnd(nullptr)
{
    mozilla::gfx::IntSize size(realSize);
    if (!mozilla::gfx::Factory::CheckSurfaceSize(size))
        MakeInvalid(size);

    cairo_format_t cformat = GfxFormatToCairoFormat(imageFormat);
    cairo_surface_t *surf =
        cairo_win32_surface_create_with_dib(cformat, size.width, size.height);

    Init(surf);
    cairo_win32_surface_set_memory_role(surf, CAIRO_WIN32_MEMORY_OFFSCREEN);

    if (CairoStatus() == CAIRO_STATUS_SUCCESS) {
        mDC = cairo_win32_surface_get_dc(CairoSurface());
        RecordMemoryUsed(size.width * size.height * 4 + sizeof(gfxWindowsSurface));
    } else {
        mDC = nullptr;
    }
}

gfxWindowsSurface::gfxWindowsSurface(cairo_surface_t *csurf) :
    mOwnsDC(false), mWnd(nullptr)
{
    if (cairo_surface_status(csurf) == 0)
        mDC = cairo_win32_surface_get_dc(csurf);
    else
        mDC = nullptr;

    MOZ_ASSERT(cairo_surface_get_type(csurf) != CAIRO_SURFACE_TYPE_WIN32_PRINTING);

    Init(csurf, true);
}

void
gfxWindowsSurface::InitWithDC(uint32_t flags)
{
    if (flags & FLAG_IS_TRANSPARENT) {
        Init(cairo_win32_surface_create_with_alpha(mDC));
    } else {
        Init(cairo_win32_surface_create(mDC));
    }
}

already_AddRefed<gfxASurface>
gfxWindowsSurface::CreateSimilarSurface(gfxContentType aContent,
                                        const mozilla::gfx::IntSize& aSize)
{
    if (!mSurface || !mSurfaceValid) {
        return nullptr;
    }

    cairo_surface_t *surface;
    if (GetContentType() == gfxContentType::COLOR_ALPHA) {
        // When creating a similar surface to a transparent surface, ensure
        // the new surface uses a DIB. cairo_surface_create_similar won't
        // use  a DIB for a gfxContentType::COLOR surface if this surface doesn't
        // have a DIB (e.g. if we're a transparent window surface). But
        // we need a DIB to perform well if the new surface is composited into
        // a surface that's the result of create_similar(gfxContentType::COLOR_ALPHA)
        // (e.g. a backbuffer for the window) --- that new surface *would*
        // have a DIB.
        gfxImageFormat gformat =
            gfxPlatform::GetPlatform()->OptimalFormatForContent(aContent);
        cairo_format_t cformat = GfxFormatToCairoFormat(gformat);
        surface = cairo_win32_surface_create_with_dib(cformat, aSize.width,
                                                      aSize.height);
    } else {
        surface =
          cairo_surface_create_similar(mSurface, (cairo_content_t)(int)aContent,
                                       aSize.width, aSize.height);
    }

    if (cairo_surface_status(surface)) {
        cairo_surface_destroy(surface);
        return nullptr;
    }

    RefPtr<gfxASurface> result = Wrap(surface, aSize);
    cairo_surface_destroy(surface);
    return result.forget();
}

gfxWindowsSurface::~gfxWindowsSurface()
{
    if (mOwnsDC) {
        if (mWnd)
            ::ReleaseDC(mWnd, mDC);
        else
            ::DeleteDC(mDC);
    }
}

HDC
gfxWindowsSurface::GetDC()
{
    return cairo_win32_surface_get_dc (CairoSurface());
}

already_AddRefed<gfxImageSurface>
gfxWindowsSurface::GetAsImageSurface()
{
    if (!mSurfaceValid) {
        NS_WARNING ("GetImageSurface on an invalid (null) surface; who's calling this without checking for surface errors?");
        return nullptr;
    }

    NS_ASSERTION(CairoSurface() != nullptr, "CairoSurface() shouldn't be nullptr when mSurfaceValid is TRUE!");

    cairo_surface_t *isurf = cairo_win32_surface_get_image(CairoSurface());
    if (!isurf)
        return nullptr;

    RefPtr<gfxImageSurface> result = gfxASurface::Wrap(isurf).downcast<gfxImageSurface>();
    result->SetOpaqueRect(GetOpaqueRect());

    return result.forget();
}

const mozilla::gfx::IntSize
gfxWindowsSurface::GetSize() const
{
    if (!mSurfaceValid) {
        NS_WARNING ("GetImageSurface on an invalid (null) surface; who's calling this without checking for surface errors?");
        return mozilla::gfx::IntSize(-1, -1);
    }

    NS_ASSERTION(mSurface != nullptr, "CairoSurface() shouldn't be nullptr when mSurfaceValid is TRUE!");

    return mozilla::gfx::IntSize(cairo_win32_surface_get_width(mSurface),
                      cairo_win32_surface_get_height(mSurface));
}
