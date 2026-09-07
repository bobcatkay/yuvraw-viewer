#include "FSparseTexture.h"
#include "Image/FImageLimits.h"
#include "Util.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    // ARB_sparse_texture tokens are absent from the bundled GLAD extension set.
    constexpr GLenum kTextureSparse = 0x91A6;
    constexpr GLenum kVirtualPageSizeIndex = 0x91A7;
    constexpr GLenum kNumVirtualPageSizes = 0x91A8;
    constexpr GLenum kVirtualPageSizeX = 0x9195;
    constexpr GLenum kVirtualPageSizeY = 0x9196;
    constexpr GLenum kVirtualPageSizeZ = 0x9197;
    constexpr GLenum kMaximumSparseTextureSize = 0x9198;
    constexpr GLenum kNumSparseLevels = 0x91AA;
    constexpr size_t kMaximumPageBytes = 4u * FImageLimits::kUnitsPerMebi;
    constexpr int32_t kFilterBorderTexels = 1;
    constexpr int32_t kMaximumPageSizeChoices = 64;
    constexpr int32_t kRgbaChannelCount = 4;
    using FPageCommitmentProc = void (APIENTRY*)(GLenum, GLint, GLint, GLint, GLint,
        GLsizei, GLsizei, GLsizei, GLboolean);

    struct FFunctions
    {
        PFNGLTEXSTORAGE2DPROC Storage = nullptr;
        PFNGLGETINTERNALFORMATIVPROC InternalFormat = nullptr;
        FPageCommitmentProc Commit = nullptr;

        bool Load()
        {
            if (!glfwGetCurrentContext() || !glfwExtensionSupported("GL_ARB_sparse_texture"))
                return false;
            Storage = reinterpret_cast<PFNGLTEXSTORAGE2DPROC>(glfwGetProcAddress("glTexStorage2D"));
            InternalFormat = reinterpret_cast<PFNGLGETINTERNALFORMATIVPROC>(glfwGetProcAddress("glGetInternalformativ"));
            Commit = reinterpret_cast<FPageCommitmentProc>(glfwGetProcAddress("glTexPageCommitmentARB"));
            return Storage && InternalFormat && Commit;
        }
    };

    struct FPageRect
    {
        int32_t Left = 0, Top = 0, Right = 0, Bottom = 0;
        bool Contains(int32_t X, int32_t Y) const
        {
            return X >= Left && X < Right && Y >= Top && Y < Bottom;
        }
        bool operator==(const FPageRect& Other) const
        {
            return Left == Other.Left && Top == Other.Top && Right == Other.Right && Bottom == Other.Bottom;
        }
    };

    enum class EPageState : uint8_t { Uncommitted, Allocated, Uploaded };

    void ClearErrors()
    {
        while (glGetError() != GL_NO_ERROR) {}
    }

    /** 每页可能从带 padding 的 CPU 行读取；不能继承 ImGui 或其它上传的 PBO/skip 状态。 */
    class FScopedUnpack
    {
    public:
        FScopedUnpack()
        {
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &Alignment);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &RowLength);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &SkipPixels);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS, &SkipRows);
            glGetIntegerv(GL_UNPACK_SWAP_BYTES, &SwapBytes);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &Buffer);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
        }
        ~FScopedUnpack()
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, Alignment);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, RowLength);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, SkipPixels);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, SkipRows);
            glPixelStorei(GL_UNPACK_SWAP_BYTES, SwapBytes);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, Buffer);
        }
    private:
        GLint Alignment = 0, RowLength = 0, SkipPixels = 0, SkipRows = 0, SwapBytes = 0, Buffer = 0;
    };
}

class FSparseTexture::FImpl
{
public:
    ~FImpl()
    {
        if (Texture) glDeleteTextures(1, &Texture);
    }

    bool Create(int32_t InWidth, int32_t InHeight, GLint InInternalFormat,
        GLenum InFormat, GLenum InType, int32_t InBytesPerTexel)
    {
        if (InWidth <= 0 || InHeight <= 0 || InBytesPerTexel <= 0 || !Functions.Load())
            return false;
        ClearErrors();
        Width = InWidth;
        Height = InHeight;
        Format = InFormat;
        Type = InType;
        BytesPerTexel = InBytesPerTexel;
        // RGB sparse storage is optional; RGBA storage preserves RGB values and supplies opaque alpha.
        const GLint internalFormat = InInternalFormat == GL_RGB8 ? GL_RGBA8 :
            InInternalFormat == GL_RGB16 ? GL_RGBA16 : InInternalFormat;
        const int32_t storageBytes = InInternalFormat == GL_RGB8 ? kRgbaChannelCount :
            InInternalFormat == GL_RGB16 ? kRgbaChannelCount * static_cast<int32_t>(sizeof(uint16_t)) : InBytesPerTexel;
        GLint count = 0, maximumSize = 0;
        Functions.InternalFormat(GL_TEXTURE_2D, internalFormat, kNumVirtualPageSizes, 1, &count);
        glGetIntegerv(kMaximumSparseTextureSize, &maximumSize);
        const GLenum capabilityError = glGetError();
        if (capabilityError != GL_NO_ERROR || count <= 0 || count > kMaximumPageSizeChoices || maximumSize <= 0)
        {
            LOGW("SparseTexture", "Sparse format query failed: format=0x%X choices=%d max=%d error=0x%X",
                static_cast<unsigned>(internalFormat), count, maximumSize, static_cast<unsigned>(capabilityError));
            return false;
        }
        std::vector<GLint> xs(count), ys(count), zs(count);
        Functions.InternalFormat(GL_TEXTURE_2D, internalFormat, kVirtualPageSizeX, count, xs.data());
        Functions.InternalFormat(GL_TEXTURE_2D, internalFormat, kVirtualPageSizeY, count, ys.data());
        Functions.InternalFormat(GL_TEXTURE_2D, internalFormat, kVirtualPageSizeZ, count, zs.data());
        if (glGetError() != GL_NO_ERROR) return false;
        GLint pageIndex = -1;
        for (GLint i = 0; i < count; ++i)
        {
            if (xs[i] <= 0 || ys[i] <= 0 || zs[i] != 1 || xs[i] > maximumSize || ys[i] > maximumSize)
                continue;
            const int64_t paddedWidth = ((static_cast<int64_t>(Width) + xs[i] - 1) / xs[i]) * xs[i];
            const int64_t paddedHeight = ((static_cast<int64_t>(Height) + ys[i] - 1) / ys[i]) * ys[i];
            const uint64_t pageBytes = static_cast<uint64_t>(xs[i]) * ys[i] * storageBytes;
            if (paddedWidth > maximumSize || paddedHeight > maximumSize || pageBytes > kMaximumPageBytes)
                continue;
            if (pageIndex >= 0 && pageBytes >= PageBytes) continue;
            pageIndex = i;
            PageWidth = xs[i];
            PageHeight = ys[i];
            StorageWidth = static_cast<int32_t>(paddedWidth);
            StorageHeight = static_cast<int32_t>(paddedHeight);
            PageBytes = static_cast<size_t>(pageBytes);
        }
        if (pageIndex < 0)
        {
            LOGW("SparseTexture", "No usable sparse page layout for %dx%d, format=0x%X max=%d",
                Width, Height, static_cast<unsigned>(internalFormat), maximumSize);
            return false;
        }
        Columns = StorageWidth / PageWidth;
        Rows = StorageHeight / PageHeight;
        size_t pageCount = 0;
        if (!FImageLimits::TryMultiplySize(static_cast<size_t>(Columns), static_cast<size_t>(Rows), pageCount))
            return false;
        Resident.assign(pageCount, EPageState::Uncommitted);
        glGenTextures(1, &Texture);
        glBindTexture(GL_TEXTURE_2D, Texture);
        glTexParameteri(GL_TEXTURE_2D, kTextureSparse, GL_TRUE);
        glTexParameteri(GL_TEXTURE_2D, kVirtualPageSizeIndex, pageIndex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        Functions.Storage(GL_TEXTURE_2D, 1, internalFormat, StorageWidth, StorageHeight);
        GLint sparseLevels = 0;
        glGetTexParameteriv(GL_TEXTURE_2D, kNumSparseLevels, &sparseLevels);
        const GLenum storageError = glGetError();
        if (storageError != GL_NO_ERROR || sparseLevels < 1)
        {
            LOGW("SparseTexture", "Sparse storage failed: %dx%d levels=%d error=0x%X",
                StorageWidth, StorageHeight, sparseLevels, static_cast<unsigned>(storageError));
            glDeleteTextures(1, &Texture);
            Texture = 0;
            return false;
        }
        CurrentContext = glfwGetCurrentContext();
        LOGI("SparseTexture", "Created sparse plane %dx%d, storage=%dx%d page=%dx%d (%zu bytes)",
            Width, Height, StorageWidth, StorageHeight, PageWidth, PageHeight, PageBytes);
        return true;
    }

    FPageRect GetRect(const FTextureViewRegion& Region) const
    {
        if (!Texture || !std::isfinite(Region.MinU) || !std::isfinite(Region.MinV) ||
            !std::isfinite(Region.MaxU) || !std::isfinite(Region.MaxV) ||
            Region.MaxU <= Region.MinU || Region.MaxV <= Region.MinV) return {};
        const auto begin = [](float U, int32_t Dimension, int32_t Page) {
            return std::max(0, static_cast<int32_t>(std::floor(std::clamp(U, 0.0f, 1.0f) * Dimension))
                - kFilterBorderTexels) / Page;
        };
        const auto end = [](float U, int32_t Dimension, int32_t Page) {
            const int32_t pixel = std::min(Dimension,
                static_cast<int32_t>(std::ceil(std::clamp(U, 0.0f, 1.0f) * Dimension)) + kFilterBorderTexels);
            return static_cast<int32_t>((static_cast<int64_t>(pixel) + Page - 1) / Page);
        };
        return { begin(Region.MinU, Width, PageWidth), begin(Region.MinV, Height, PageHeight),
            end(Region.MaxU, Width, PageWidth), end(Region.MaxV, Height, PageHeight) };
    }

    bool CheckError(FTextureCreateError& OutError)
    {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) return true;
        OutError = { error == GL_OUT_OF_MEMORY ? ETextureCreateError::OutOfMemory : ETextureCreateError::OpenGlFailure,
            Width, Height, 0, error };
        return false;
    }

    bool EnsureContext(FTextureCreateError& OutError)
    {
        if (CurrentContext != glfwGetCurrentContext())
        {
            if (!Functions.Load())
            {
                OutError = { ETextureCreateError::LimitUnavailable, Width, Height };
                return false;
            }
            CurrentContext = glfwGetCurrentContext();
        }
        return true;
    }

    bool Evict(FTextureCreateError& OutError)
    {
        bHasRetainedRect = false;
        bPagesReady = false;
        if (ResidentCount == 0) return true;
        if (!EnsureContext(OutError)) return false;
        ClearErrors();
        glBindTexture(GL_TEXTURE_2D, Texture);
        Functions.Commit(GL_TEXTURE_2D, 0, 0, 0, 0, StorageWidth, StorageHeight, 1, GL_FALSE);
        if (!CheckError(OutError)) return false;
        std::fill(Resident.begin(), Resident.end(), EPageState::Uncommitted);
        ResidentCount = 0;
        return true;
    }

    bool Trim(const FTextureViewRegion& Region, FTextureCreateError& OutError)
    {
        if (!EnsureContext(OutError)) return false;
        const FPageRect rect = GetRect(Region);
        if (bHasRetainedRect && rect == RetainedRect) return true;
        bPagesReady = false;
        if (ResidentCount == 0)
        {
            RetainedRect = rect;
            bHasRetainedRect = true;
            return true;
        }
        ClearErrors();
        glBindTexture(GL_TEXTURE_2D, Texture);
        // 先淘汰视口外页面；按行合并相邻页，避免一次缩放发出数千次 decommit。
        for (int32_t y = 0; y < Rows; ++y)
        {
            for (int32_t x = 0; x < Columns;)
            {
                const size_t index = static_cast<size_t>(y) * Columns + x;
                if (Resident[index] == EPageState::Uncommitted || rect.Contains(x, y)) { ++x; continue; }
                const int32_t left = x;
                while (x < Columns && Resident[static_cast<size_t>(y) * Columns + x] != EPageState::Uncommitted &&
                    !rect.Contains(x, y))
                    ++x;
                Functions.Commit(GL_TEXTURE_2D, 0, left * PageWidth, y * PageHeight, 0,
                    (x - left) * PageWidth, PageHeight, 1, GL_FALSE);
                if (!CheckError(OutError)) return false;
                for (int32_t evictedX = left; evictedX < x; ++evictedX)
                    Resident[static_cast<size_t>(y) * Columns + evictedX] = EPageState::Uncommitted;
                ResidentCount -= static_cast<size_t>(x - left);
            }
        }
        RetainedRect = rect;
        bHasRetainedRect = true;
        return true;
    }

    bool Update(const FTextureViewRegion& Region, const uint8_t* Pixels, int32_t StrideBytes,
        size_t& Budget, FTextureCreateError& OutError)
    {
        if (!Texture || !Pixels || static_cast<int64_t>(StrideBytes) < static_cast<int64_t>(Width) * BytesPerTexel)
        {
            OutError = { ETextureCreateError::InvalidDimensions, Width, Height };
            return false;
        }
        if (!Trim(Region, OutError)) return false;
        // 视口在相同页矩形内移动时，不必逐帧扫描页表或查询 GL 上传状态。
        if (bPagesReady) return true;
        const FPageRect rect = RetainedRect;
        ClearErrors();
        glBindTexture(GL_TEXTURE_2D, Texture);
        FScopedUnpack unpack;
        for (int32_t y = rect.Top; y < rect.Bottom; ++y)
        {
            for (int32_t x = rect.Left; x < rect.Right; ++x)
            {
                const size_t index = static_cast<size_t>(y) * Columns + x;
                if (Resident[index] == EPageState::Uploaded) continue;
                if (Budget < PageBytes) return false;
                const int32_t pixelX = x * PageWidth, pixelY = y * PageHeight;
                const int32_t uploadWidth = std::min(PageWidth, Width - pixelX);
                const int32_t uploadHeight = std::min(PageHeight, Height - pixelY);
                const uint8_t* source = Pixels + static_cast<size_t>(pixelY) * StrideBytes
                    + static_cast<size_t>(pixelX) * BytesPerTexel;
                if (StrideBytes % BytesPerTexel == 0)
                {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, StrideBytes / BytesPerTexel);
                }
                else
                {
                    const size_t rowBytes = static_cast<size_t>(uploadWidth) * BytesPerTexel;
                    Scratch.resize(rowBytes * uploadHeight);
                    for (int32_t row = 0; row < uploadHeight; ++row)
                        std::memcpy(Scratch.data() + rowBytes * row, source + static_cast<size_t>(StrideBytes) * row, rowBytes);
                    source = Scratch.data();
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                }
                if (Resident[index] == EPageState::Uncommitted)
                {
                    Functions.Commit(GL_TEXTURE_2D, 0, pixelX, pixelY, 0, PageWidth, PageHeight, 1, GL_TRUE);
                    if (!CheckError(OutError)) return false;
                    // 上传失败时仍需跟踪已提交的页，保证后续回收和显存计数准确。
                    Resident[index] = EPageState::Allocated;
                    ++ResidentCount;
                }
                Budget -= PageBytes;
                glTexSubImage2D(GL_TEXTURE_2D, 0, pixelX, pixelY, uploadWidth, uploadHeight, Format, Type, source);
                if (!CheckError(OutError)) return false;
                Resident[index] = EPageState::Uploaded;
            }
        }
        bPagesReady = true;
        return true;
    }

    GLuint Texture = 0;
    int32_t Width = 0, Height = 0, StorageWidth = 0, StorageHeight = 0;
    int32_t PageWidth = 0, PageHeight = 0, Columns = 0, Rows = 0, BytesPerTexel = 0;
    GLenum Format = GL_RGBA, Type = GL_UNSIGNED_BYTE;
    size_t PageBytes = 0, ResidentCount = 0;
    std::vector<EPageState> Resident;
    std::vector<uint8_t> Scratch;
    FPageRect RetainedRect;
    bool bHasRetainedRect = false, bPagesReady = false;
    FFunctions Functions;
    GLFWwindow* CurrentContext = nullptr;
};

FSparseTexture::FSparseTexture() : Impl(std::make_unique<FImpl>()) {}
FSparseTexture::~FSparseTexture() = default;
bool FSparseTexture::IsSupported() { FFunctions functions; return functions.Load(); }
bool FSparseTexture::Create(int32_t Width, int32_t Height, GLint InternalFormat, GLenum Format,
    GLenum Type, int32_t BytesPerTexel)
{
    Impl = std::make_unique<FImpl>();
    return Impl->Create(Width, Height, InternalFormat, Format, Type, BytesPerTexel);
}
size_t FSparseTexture::EstimateResidentBytes(const FTextureViewRegion& Region) const
{
    const FPageRect rect = Impl->GetRect(Region);
    size_t pages = 0, bytes = 0;
    if (!FImageLimits::TryMultiplySize(static_cast<size_t>(rect.Right - rect.Left),
            static_cast<size_t>(rect.Bottom - rect.Top), pages) ||
        !FImageLimits::TryMultiplySize(pages, Impl->PageBytes, bytes))
        return std::numeric_limits<size_t>::max();
    return bytes;
}
bool FSparseTexture::Update(const FTextureViewRegion& Region, const uint8_t* Pixels, int32_t StrideBytes,
    size_t& Budget, FTextureCreateError& OutError)
{
    OutError = {};
    return Impl->Update(Region, Pixels, StrideBytes, Budget, OutError);
}
bool FSparseTexture::Trim(const FTextureViewRegion& Region, FTextureCreateError& OutError)
{
    OutError = {};
    return Impl->Trim(Region, OutError);
}
bool FSparseTexture::Evict(FTextureCreateError& OutError)
{
    OutError = {};
    return Impl->Evict(OutError);
}
void FSparseTexture::Bind(uint32_t Unit, bool bNearest) const
{
    glActiveTexture(GL_TEXTURE0 + Unit);
    glBindTexture(GL_TEXTURE_2D, Impl->Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, bNearest ? GL_NEAREST : GL_LINEAR);
}
float FSparseTexture::GetScaleX() const { return Impl->StorageWidth > 0 ? static_cast<float>(Impl->Width) / Impl->StorageWidth : 1.0f; }
float FSparseTexture::GetScaleY() const { return Impl->StorageHeight > 0 ? static_cast<float>(Impl->Height) / Impl->StorageHeight : 1.0f; }
size_t FSparseTexture::GetResidentBytes() const { return Impl->ResidentCount * Impl->PageBytes; }
