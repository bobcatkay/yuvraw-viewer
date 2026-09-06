#include "FHdrPresenter.h"

// FShader.h 会引入 GLAD；先由 Windows SDK 定义 APIENTRY。
#include <windows.h>

#include "gl/FShader.h"
#include "Util.h"

#include <glad/glad.h>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

FHdrPresenter* FHdrPresenter::Instance = nullptr;

// =============================================================================
// WGL_NV_DX_interop2
//
// GLAD 只生成了 GL core 的加载器，不含 WGL 扩展，所以这几个入口自己声明、
// 自己用 wglGetProcAddress 取 —— 比重新生成一份 GLAD 省事，也不会影响别的代码。
// =============================================================================

#ifndef WGL_ACCESS_READ_ONLY_NV
#define WGL_ACCESS_READ_ONLY_NV     0x00000000
#define WGL_ACCESS_READ_WRITE_NV    0x00000001
#define WGL_ACCESS_WRITE_DISCARD_NV 0x00000002
#endif

namespace
{
    typedef HANDLE(WINAPI* PFN_wglDXOpenDeviceNV)(void* dxDevice);
    typedef BOOL(WINAPI* PFN_wglDXCloseDeviceNV)(HANDLE hDevice);
    typedef HANDLE(WINAPI* PFN_wglDXRegisterObjectNV)(HANDLE hDevice, void* dxObject, GLuint name, GLenum type, GLenum access);
    typedef BOOL(WINAPI* PFN_wglDXUnregisterObjectNV)(HANDLE hDevice, HANDLE hObject);
    typedef BOOL(WINAPI* PFN_wglDXLockObjectsNV)(HANDLE hDevice, GLint count, HANDLE* hObjects);
    typedef BOOL(WINAPI* PFN_wglDXUnlockObjectsNV)(HANDLE hDevice, GLint count, HANDLE* hObjects);
    typedef const char* (WINAPI* PFN_wglGetExtensionsStringARB)(HDC hdc);

    struct FInteropApi
    {
        PFN_wglDXOpenDeviceNV       OpenDevice = nullptr;
        PFN_wglDXCloseDeviceNV      CloseDevice = nullptr;
        PFN_wglDXRegisterObjectNV   RegisterObject = nullptr;
        PFN_wglDXUnregisterObjectNV UnregisterObject = nullptr;
        PFN_wglDXLockObjectsNV      LockObjects = nullptr;
        PFN_wglDXUnlockObjectsNV    UnlockObjects = nullptr;

        bool IsValid() const
        {
            return OpenDevice && CloseDevice && RegisterObject
                && UnregisterObject && LockObjects && UnlockObjects;
        }
    };

    /**
     * 加载 WGL_NV_DX_interop2 的入口
     *
     * 先查扩展字符串再取函数指针：有的驱动即使不支持扩展，wglGetProcAddress 也会
     * 返回非空的桩函数，直接调用会在运行时莫名其妙地失败。
     */
    bool LoadInteropApi(FInteropApi& OutApi)
    {
        HDC hdc = wglGetCurrentDC();

        if (!hdc)
        {
            return false;
        }

        auto getExtensions = reinterpret_cast<PFN_wglGetExtensionsStringARB>(
            wglGetProcAddress("wglGetExtensionsStringARB"));

        if (!getExtensions)
        {
            return false;
        }

        const char* extensions = getExtensions(hdc);

        if (!extensions || !strstr(extensions, "WGL_NV_DX_interop2"))
        {
            return false;
        }

        OutApi.OpenDevice       = reinterpret_cast<PFN_wglDXOpenDeviceNV>(wglGetProcAddress("wglDXOpenDeviceNV"));
        OutApi.CloseDevice      = reinterpret_cast<PFN_wglDXCloseDeviceNV>(wglGetProcAddress("wglDXCloseDeviceNV"));
        OutApi.RegisterObject   = reinterpret_cast<PFN_wglDXRegisterObjectNV>(wglGetProcAddress("wglDXRegisterObjectNV"));
        OutApi.UnregisterObject = reinterpret_cast<PFN_wglDXUnregisterObjectNV>(wglGetProcAddress("wglDXUnregisterObjectNV"));
        OutApi.LockObjects      = reinterpret_cast<PFN_wglDXLockObjectsNV>(wglGetProcAddress("wglDXLockObjectsNV"));
        OutApi.UnlockObjects    = reinterpret_cast<PFN_wglDXUnlockObjectsNV>(wglGetProcAddress("wglDXUnlockObjectsNV"));

        return OutApi.IsValid();
    }

    /// scRGB 的定义：线性 BT.709，1.0 = 80 cd/m^2
    constexpr float kScrgbWhiteNits = 80.0f;

    /// 显示器状态的探测间隔（毫秒）。跨屏拖动要能跟上，又不必每帧问 DXGI
    constexpr uint64_t kProbeIntervalMs = 500;

    const char* kCompositeVertexShader = R"(
        #version 330 core

        out vec2 vUV;

        void main()
        {
            // 覆盖全屏的单个三角形，不需要 VBO（core profile 仍要绑一个 VAO）
            vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));

            // GL 的帧缓冲原点在左下，D3D 纹理原点在左上，CopyResource 不会翻转，
            // 所以在这里把 V 翻过来，否则呈现出来的画面上下颠倒
            vUV = vec2(p.x, 1.0 - p.y);

            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
    )";

    const char* kCompositeFragmentShader = R"(
        #version 330 core

        in vec2 vUV;
        out vec4 FragColor;

        uniform sampler2D uSource;

        // SdrWhiteNits / 80
        uniform float uScrgbScale;

        // 保号的扩展 sRGB 解码。与 Image/FColorTransform.h 的 SrgbDecodeExtended 对应
        vec3 SrgbDecodeExtended(vec3 c)
        {
            vec3 a  = abs(c);
            vec3 lo = a / 12.92;
            vec3 hi = pow((a + 0.055) / 1.055, vec3(2.4));

            return sign(c) * mix(lo, hi, step(vec3(0.04045), a));
        }

        void main()
        {
            // FBO-A 的约定：扩展 sRGB 编码，1.0 = 显示器 SDR 白电平。
            // 解码成线性后按 scRGB 的 80nit 基准缩放，就是交换链要的值
            vec3 encoded = texture(uSource, vUV).rgb;

            FragColor = vec4(SrgbDecodeExtended(encoded) * uScrgbScale, 1.0);
        }
    )";
}

/**
 * D3D11 / interop 的私有状态
 *
 * 放进 pimpl 是为了不把 <d3d11.h> 和 <windows.h> 泄漏进头文件 ——
 * FHdrPresenter.h 被图像查看器包含，那边可不想要 windows.h 里的一堆宏。
 */
struct FHdrPresenter::FImpl
{
    HWND Hwnd = nullptr;

    FInteropApi Api;

    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<IDXGISwapChain3> SwapChain;

    /// 与 GL 共享的 fp16 纹理（合成 pass 的目标）
    ComPtr<ID3D11Texture2D> SharedTexture;

    /// wglDXOpenDeviceNV 的返回值
    HANDLE InteropDevice = nullptr;

    /// SharedTexture 注册进 GL 后的句柄
    HANDLE SharedTextureHandle = nullptr;

    /// ImGui 整帧渲染的目标（普通 GL 纹理）
    GLuint SceneTexture = 0;
    GLuint SceneFbo = 0;

    /// 共享纹理在 GL 侧的名字与帧缓冲
    GLuint SharedGlTexture = 0;
    GLuint SharedFbo = 0;

    /// 合成 pass
    std::unique_ptr<FShader> CompositeShader;
    GLuint EmptyVao = 0;
};

FHdrPresenter::FHdrPresenter()
    : Impl(std::make_unique<FImpl>())
{
    Instance = this;
    StatusText = u8"未初始化";
}

FHdrPresenter::~FHdrPresenter()
{
    Shutdown();

    if (Instance == this)
    {
        Instance = nullptr;
    }
}

FHdrPresenter* FHdrPresenter::Get()
{
    return Instance;
}

FColorTransform::FDisplayOutput FHdrPresenter::GetActiveDisplayOutput()
{
    if (Instance)
    {
        return Instance->GetDisplayOutput();
    }

    return FColorTransform::FDisplayOutput();
}

FColorTransform::FDisplayOutput FHdrPresenter::GetDisplayOutput() const
{
    FColorTransform::FDisplayOutput output;

    if (!IsHdrActive())
    {
        // 默认值就是 SDR，色彩管线会自然走 sRGB 输出分支
        return output;
    }

    output.bHdr = true;
    output.SdrWhiteNits = DisplayInfo.SdrWhiteNits;
    output.MaxNits = DisplayInfo.MaxNits;
    output.Primaries = EColorPrimaries::BT709;   // scRGB 就是线性 BT.709

    return output;
}

bool FHdrPresenter::IsHdrActive() const
{
    return IsHdrAvailable() && bUserEnabled;
}

bool FHdrPresenter::IsHdrAvailable() const
{
    return bInitialized && DisplayInfo.bHdrEnabled;
}

bool FHdrPresenter::IsPresentationActive() const
{
    if (!bInitialized)
    {
        return false;
    }

    // 门闩一旦合上就不再看显示器状态：用户关掉"HDR 输出"、在系统设置里关掉 HDR、
    // 或把窗口拖到 SDR 副屏，都只改色彩管线（IsHdrActive），呈现继续走 DXGI。
    // 在同一个 HWND 上让 DXGI 与 WGL 交替呈现会闪屏，还可能把 UI 线程卡住。
    return DisplayInfo.bHdrEnabled || bPresentationLatched;
}

void FHdrPresenter::SetUserEnabled(bool bEnabled)
{
    if (bUserEnabled == bEnabled)
    {
        return;
    }

    bUserEnabled = bEnabled;

    LOGD("SetUserEnabled", "HDR content output changed: enabled=%d, dxgiPresent=%d",
         bUserEnabled ? 1 : 0, IsPresentationActive() ? 1 : 0);
}

bool FHdrPresenter::Initialize(void* Hwnd)
{
    if (bInitialized)
    {
        return true;
    }

    Impl->Hwnd = static_cast<HWND>(Hwnd);

    if (!Impl->Hwnd)
    {
        StatusText = u8"HDR 不可用：窗口句柄无效";

        return false;
    }

    DisplayInfo = FHdrDisplay::Query(Impl->Hwnd);
    LastProbeMs = GetTickCount64();

    if (!LoadInteropApi(Impl->Api))
    {
        StatusText = u8"HDR 不可用：驱动不支持 WGL_NV_DX_interop2";
        LOGE("Initialize", "WGL_NV_DX_interop2 not available");

        return false;
    }

    // --- D3D11 设备 ---
    UINT deviceFlags = 0;

    const D3D_FEATURE_LEVEL requestedLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
        requestedLevels, ARRAYSIZE(requestedLevels), D3D11_SDK_VERSION,
        &Impl->Device, &obtainedLevel, &Impl->Context);

    if (FAILED(hr))
    {
        StatusText = u8"HDR 不可用：创建 D3D11 设备失败";
        LOGE("Initialize", "D3D11CreateDevice failed: 0x%08X", static_cast<unsigned>(hr));

        return false;
    }

    // --- flip-model 交换链 ---
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;

    if (FAILED(Impl->Device.As(&dxgiDevice)) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
    {
        StatusText = u8"HDR 不可用：取 DXGI 工厂失败";

        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = 0;                                      // 跟随窗口客户区
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;        // scRGB 必须是 fp16
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;     // HDR 输出必须是 flip model
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain1;

    hr = factory->CreateSwapChainForHwnd(Impl->Device.Get(), Impl->Hwnd, &desc, nullptr, nullptr, &swapChain1);

    if (FAILED(hr))
    {
        // 同一个 HWND 上既有 GL 上下文又要建 flip 交换链，个别驱动会拒绝。
        // 真遇到再考虑给交换链单开一个子窗口，但那要额外同步尺寸与 z 序，
        // 在能直接成功的机器上不值得
        StatusText = u8"HDR 不可用：创建交换链失败";
        LOGE("Initialize", "CreateSwapChainForHwnd failed: 0x%08X", static_cast<unsigned>(hr));

        return false;
    }

    // Alt+Enter 交给我们自己（其实是不处理），别让 DXGI 抢走
    factory->MakeWindowAssociation(Impl->Hwnd, DXGI_MWA_NO_ALT_ENTER);

    if (FAILED(swapChain1.As(&Impl->SwapChain)))
    {
        StatusText = u8"HDR 不可用：IDXGISwapChain3 不可用";

        return false;
    }

    // --- 声明色彩空间：scRGB（线性 BT.709，1.0 = 80nit）---
    UINT colorSpaceSupport = 0;
    const DXGI_COLOR_SPACE_TYPE scrgb = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

    if (FAILED(Impl->SwapChain->CheckColorSpaceSupport(scrgb, &colorSpaceSupport)) ||
        (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0)
    {
        StatusText = u8"HDR 不可用：交换链不支持 scRGB";
        LOGE("Initialize", "scRGB color space not supported by swap chain");

        return false;
    }

    if (FAILED(Impl->SwapChain->SetColorSpace1(scrgb)))
    {
        StatusText = u8"HDR 不可用：SetColorSpace1 失败";

        return false;
    }

    // --- interop 设备 ---
    Impl->InteropDevice = Impl->Api.OpenDevice(Impl->Device.Get());

    if (!Impl->InteropDevice)
    {
        StatusText = u8"HDR 不可用：wglDXOpenDeviceNV 失败";
        LOGE("Initialize", "wglDXOpenDeviceNV failed (GL 与 D3D 可能不在同一块 GPU 上)");

        return false;
    }

    if (!CreateCompositeProgram())
    {
        StatusText = u8"HDR 不可用：合成着色器编译失败";

        return false;
    }

    glGenVertexArrays(1, &Impl->EmptyVao);

    bInitialized = true;

    StatusText = DisplayInfo.bHdrEnabled
        ? u8"HDR 通路就绪"
        : u8"HDR 通路就绪（当前显示器未开启 HDR）";

    LOGD("Initialize", "HDR presenter ready: hdr=%d maxNits=%.0f sdrWhite=%.0f output=%s",
         DisplayInfo.bHdrEnabled ? 1 : 0, DisplayInfo.MaxNits, DisplayInfo.SdrWhiteNits,
         DisplayInfo.OutputName.c_str());

    return true;
}

bool FHdrPresenter::CreateCompositeProgram()
{
    Impl->CompositeShader = std::make_unique<FShader>();

    if (!Impl->CompositeShader->CreateFromSource(kCompositeVertexShader, kCompositeFragmentShader))
    {
        Impl->CompositeShader.reset();

        return false;
    }

    return true;
}

void FHdrPresenter::ReleaseSizedResources()
{
    if (Impl->SharedTextureHandle && Impl->InteropDevice)
    {
        Impl->Api.UnregisterObject(Impl->InteropDevice, Impl->SharedTextureHandle);
        Impl->SharedTextureHandle = nullptr;
    }

    if (Impl->SharedFbo)
    {
        glDeleteFramebuffers(1, &Impl->SharedFbo);
        Impl->SharedFbo = 0;
    }

    if (Impl->SharedGlTexture)
    {
        glDeleteTextures(1, &Impl->SharedGlTexture);
        Impl->SharedGlTexture = 0;
    }

    if (Impl->SceneFbo)
    {
        glDeleteFramebuffers(1, &Impl->SceneFbo);
        Impl->SceneFbo = 0;
    }

    if (Impl->SceneTexture)
    {
        glDeleteTextures(1, &Impl->SceneTexture);
        Impl->SceneTexture = 0;
    }

    Impl->SharedTexture.Reset();
}

bool FHdrPresenter::EnsureResources(int32_t Width, int32_t Height)
{
    if (Width <= 0 || Height <= 0)
    {
        return false;
    }

    if (SurfaceWidth == Width && SurfaceHeight == Height && Impl->SharedTextureHandle)
    {
        return true;
    }

    ReleaseSizedResources();

    // 交换链的后台缓冲跟着窗口一起改。此时不能有任何 GetBuffer 拿到的引用还活着 ——
    // 我们每帧取用完就释放，所以这里是安全的
    if (FAILED(Impl->SwapChain->ResizeBuffers(0, static_cast<UINT>(Width), static_cast<UINT>(Height),
                                              DXGI_FORMAT_UNKNOWN, 0)))
    {
        LOGE("EnsureResources", "ResizeBuffers failed");

        return false;
    }

    // --- FBO-A：ImGui 整帧渲染的目标 ---
    glGenTextures(1, &Impl->SceneTexture);
    glBindTexture(GL_TEXTURE_2D, Impl->SceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, Width, Height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &Impl->SceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, Impl->SceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Impl->SceneTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        LOGE("EnsureResources", "Scene FBO incomplete");

        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- 共享纹理：D3D11 侧创建，注册给 GL ---
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = static_cast<UINT>(Width);
    texDesc.Height = static_cast<UINT>(Height);
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    // interop 要求资源是可共享的，漏了这个 wglDXRegisterObjectNV 会失败
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    if (FAILED(Impl->Device->CreateTexture2D(&texDesc, nullptr, &Impl->SharedTexture)))
    {
        LOGE("EnsureResources", "CreateTexture2D (shared) failed");

        return false;
    }

    // GL 侧只需要一个纹理名，存储由注册时从 D3D 资源接管，不要自己 glTexImage2D
    glGenTextures(1, &Impl->SharedGlTexture);

    Impl->SharedTextureHandle = Impl->Api.RegisterObject(
        Impl->InteropDevice, Impl->SharedTexture.Get(), Impl->SharedGlTexture,
        GL_TEXTURE_2D, WGL_ACCESS_WRITE_DISCARD_NV);

    if (!Impl->SharedTextureHandle)
    {
        LOGE("EnsureResources", "wglDXRegisterObjectNV failed");

        return false;
    }

    glGenFramebuffers(1, &Impl->SharedFbo);

    // 挂帧缓冲和查完整性都要在锁定期间做 —— 未锁定时那个纹理名在 GL 看来没有存储
    if (!Impl->Api.LockObjects(Impl->InteropDevice, 1, &Impl->SharedTextureHandle))
    {
        LOGE("EnsureResources", "wglDXLockObjectsNV failed");

        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, Impl->SharedFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Impl->SharedGlTexture, 0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Impl->Api.UnlockObjects(Impl->InteropDevice, 1, &Impl->SharedTextureHandle);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LOGE("EnsureResources", "Shared FBO incomplete: 0x%04X", static_cast<unsigned>(status));

        return false;
    }

    SurfaceWidth = Width;
    SurfaceHeight = Height;

    return true;
}

void FHdrPresenter::Update()
{
    if (!bInitialized)
    {
        return;
    }

    const uint64_t now = GetTickCount64();

    if (now - LastProbeMs < kProbeIntervalMs)
    {
        return;
    }

    LastProbeMs = now;

    const bool bWasHdr = DisplayInfo.bHdrEnabled;

    DisplayInfo = FHdrDisplay::Query(Impl->Hwnd);

    if (bWasHdr != DisplayInfo.bHdrEnabled)
    {
        StatusText = DisplayInfo.bHdrEnabled
            ? u8"HDR 通路就绪"
            : u8"HDR 通路就绪（当前显示器未开启 HDR）";

        LOGD("Update", "Display HDR state changed: %d (output=%s)",
             DisplayInfo.bHdrEnabled ? 1 : 0, DisplayInfo.OutputName.c_str());
    }
}

bool FHdrPresenter::BeginFrame(int32_t Width, int32_t Height)
{
    bFrameActive = false;

    if (!IsPresentationActive())
    {
        return false;
    }

    // 最小化产生的 0x0 只是瞬时窗口状态，不是 HDR 设备故障，不能永久关闭呈现层。
    if (Width <= 0 || Height <= 0)
    {
        return false;
    }

    if (!EnsureResources(Width, Height))
    {
        DisableAfterFailure(u8"HDR 已关闭：帧缓冲创建失败", "EnsureResources", 0);

        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, Impl->SceneFbo);
    glViewport(0, 0, Width, Height);

    // ImGui 自己不清屏，这里补上。清成不透明黑，界面背景由 ImGui 画
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    bFrameActive = true;

    return true;
}

bool FHdrPresenter::EndFrame()
{
    if (!bFrameActive)
    {
        return false;
    }

    bFrameActive = false;

    // --- 合成：扩展 sRGB 编码 -> scRGB 线性 ---
    if (!Impl->Api.LockObjects(Impl->InteropDevice, 1, &Impl->SharedTextureHandle))
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, Impl->SharedFbo);
    glViewport(0, 0, SurfaceWidth, SurfaceHeight);

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    Impl->CompositeShader->Use();
    Impl->CompositeShader->SetInt("uSource", 0);
    Impl->CompositeShader->SetFloat("uScrgbScale", DisplayInfo.SdrWhiteNits / kScrgbWhiteNits);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Impl->SceneTexture);

    glBindVertexArray(Impl->EmptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 解锁会插入 GL -> D3D 的同步，之后 D3D 才能安全读这张纹理
    Impl->Api.UnlockObjects(Impl->InteropDevice, 1, &Impl->SharedTextureHandle);

    // --- 呈现 ---
    ComPtr<ID3D11Texture2D> backBuffer;

    HRESULT hr = Impl->SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));

    if (SUCCEEDED(hr))
    {
        Impl->Context->CopyResource(backBuffer.Get(), Impl->SharedTexture.Get());

        // 后台缓冲的引用必须在 Present/ResizeBuffers 之前放掉
        backBuffer.Reset();

        hr = Impl->SwapChain->Present(1, 0);
    }

    if (FAILED(hr))
    {
        // 呈现一旦失败就整体退回 SDR，而不是每帧重试：
        // 继续留在 HDR 通路上只会让窗口卡住不刷新，而且日志会被刷屏
        DisableAfterFailure(u8"HDR 已关闭：呈现失败", "Present/GetBuffer", hr);

        return false;
    }

    // 交换链已经接管这个 HWND，之后不能再回到 WGL SwapBuffers。见 IsPresentationActive
    bPresentationLatched = true;

    return true;
}

void FHdrPresenter::DisableAfterFailure(const char* Status, const char* Where, long Code)
{
    StatusText = Status;
    bInitialized = false;
    bFrameActive = false;

    // 通路本身坏了，此时退回 WGL 是唯一选择 —— 门闩必须松开，否则窗口再也不会刷新
    bPresentationLatched = false;

    LOGE("DisableAfterFailure", "HDR path disabled at %s (0x%08X)", Where, static_cast<unsigned>(Code));
}

void FHdrPresenter::Shutdown()
{
    if (Impl->InteropDevice)
    {
        ReleaseSizedResources();

        Impl->Api.CloseDevice(Impl->InteropDevice);
        Impl->InteropDevice = nullptr;
    }
    else
    {
        ReleaseSizedResources();
    }

    if (Impl->EmptyVao)
    {
        glDeleteVertexArrays(1, &Impl->EmptyVao);
        Impl->EmptyVao = 0;
    }

    Impl->CompositeShader.reset();

    Impl->SwapChain.Reset();
    Impl->Context.Reset();
    Impl->Device.Reset();

    SurfaceWidth = 0;
    SurfaceHeight = 0;

    bInitialized = false;
    bFrameActive = false;
    bPresentationLatched = false;
}
