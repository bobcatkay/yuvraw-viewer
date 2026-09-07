#include "FLocalization.h"
#include "FAsyncImageLoader.h"
#include "FUserSettings.h"

#include "Image/FImageFormatDesc.h"
#include "Image/FImageLimits.h"
#include "Image/FImageLoader.h"
#include "Image/FResolutionGuess.h"
#include "gl/FTexture.h"
#include "gl/FSparseTexture.h"
#include "Util.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstddef>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

namespace
{
    constexpr int32_t kHiddenContextWidth = 1;
    constexpr int32_t kHiddenContextHeight = 1;
    constexpr int32_t kFallbackContextMajorVersion = 3;
    constexpr int32_t kFallbackContextMinorVersion = 3;
    constexpr size_t kLoadErrorBufferSize = 384;

    using FContinuePredicate = std::function<bool()>;

    int64_t ToMilliseconds(
        std::chrono::steady_clock::duration Duration)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Duration).count();
    }

    const char* TargetName(EImageLoadTarget Target)
    {
        return Target == EImageLoadTarget::Main ? "main" : "compare";
    }

    std::string BuildLoadErrorText(
        const FImageLoadParams& Params,
        uint64_t FileSize,
        bool bSelfDescribing)
    {
        if (bSelfDescribing)
        {
            return FLocalization::Text(EUiText::DecodeFileFailed);
        }

        if (!Params.IsValid())
        {
            return FLocalization::Text(EUiText::IncompleteLoadParameters);
        }

        const size_t imageSize = FImageFormatDesc::CalculateFrameSize(
            Params.Format,
            Params.Width,
            Params.Height,
            Params.Stride);

        if (imageSize > 0 && FileSize > 0 &&
            static_cast<uint64_t>(imageSize) > FileSize)
        {
            char buffer[kLoadErrorBufferSize];
            std::snprintf(
                buffer,
                sizeof(buffer),
                FLocalization::Text(EUiText::LoadFileTooSmall),
                static_cast<unsigned long long>(imageSize),
                static_cast<unsigned long long>(FileSize));
            return buffer;
        }

        return FLocalization::Text(EUiText::DecodeParametersFailed);
    }

    FImageLoadParams ResolveAutomaticParams(
        const std::string& FilePath,
        uint64_t FileSize,
        bool bSelfDescribing)
    {
        FImageLoadParams params = FImageLoadParams::Default();

        if (bSelfDescribing)
        {
            return params;
        }

        int32_t parsedWidth = 0;
        int32_t parsedHeight = 0;
        const EImageFormat parsedFormat =
            ParseImageInfoFromFilename(FilePath, parsedWidth, parsedHeight);

        if (parsedWidth > 0 && parsedHeight > 0)
        {
            params.Width = parsedWidth;
            params.Height = parsedHeight;
            params.Stride = 0;
        }

        if (parsedFormat != EImageFormat::Unknown)
        {
            params.SetDetectedFormat(parsedFormat);
        }

        if (FileSize == 0 || params.Format == EImageFormat::Unknown)
        {
            return params;
        }

        int32_t visibleWidth = 0;
        int32_t detectedStride = 0;

        if (TryResolveAndroidSemiplanarStride(
                FilePath,
                params.Format,
                params.Width,
                params.Height,
                FileSize,
                visibleWidth,
                detectedStride))
        {
            params.Width = visibleWidth;
            params.Stride = detectedStride;
        }

        if (!FResolutionGuess::Matches(
                params.Format,
                params.Width,
                params.Height,
                params.Stride,
                FileSize))
        {
            const std::vector<FResolutionCandidate> candidates =
                FResolutionGuess::Guess(params.Format, FileSize, 1);

            if (!candidates.empty())
            {
                LOGD(
                    "ResolveAutomaticParams",
                    "Filename geometry %dx%d does not match %llu bytes, using %dx%d",
                    params.Width,
                    params.Height,
                    static_cast<unsigned long long>(FileSize),
                    candidates[0].Width,
                    candidates[0].Height);
                params.Width = candidates[0].Width;
                params.Height = candidates[0].Height;
                params.Stride = 0;
            }
        }

        return params;
    }

    FImageLoadResult DecodeRequest(
        FImageLoadRequest Request,
        const FContinuePredicate& ShouldContinue)
    {
        const auto workerStart = std::chrono::steady_clock::now();

        FImageLoadResult result;
        result.RequestId = Request.RequestId;
        result.Target = Request.Target;
        result.TextureLoadOptions = Request.TextureLoadOptions;
        result.FilePath = Request.FilePath;
        result.AttemptCount = static_cast<int32_t>(Request.Attempts.size());
        result.Timings.QueueWaitMilliseconds = Request.EnqueuedAt.time_since_epoch().count() != 0
            ? ToMilliseconds(workerStart - Request.EnqueuedAt)
            : 0;

        const auto inspectionStart = std::chrono::steady_clock::now();
        std::error_code ec;
        const std::filesystem::path nativePath =
            std::filesystem::u8path(Request.FilePath);
        const bool bRegularFile =
            !Request.FilePath.empty() &&
            std::filesystem::is_regular_file(nativePath, ec);

        if (!bRegularFile)
        {
            result.LastError = FLocalization::Text(EUiText::FileNoLongerExists);
            result.Timings.InspectionMilliseconds =
                ToMilliseconds(std::chrono::steady_clock::now() - inspectionStart);
            result.Timings.WorkerTotalMilliseconds =
                ToMilliseconds(std::chrono::steady_clock::now() - workerStart);
            return result;
        }

        ec.clear();
        const uintmax_t nativeFileSize = std::filesystem::file_size(nativePath, ec);
        result.FileSize = ec ? 0 : static_cast<uint64_t>(nativeFileSize);
        const bool bSelfDescribing =
            FImageLoaderFactory::IsSelfDescribingFile(Request.FilePath);
        result.Timings.InspectionMilliseconds =
            ToMilliseconds(std::chrono::steady_clock::now() - inspectionStart);

        if (Request.Attempts.empty())
        {
            Request.Attempts.push_back(FImageLoadAttempt{});
            result.AttemptCount = 1;
        }

        for (size_t attemptIndex = 0;
             attemptIndex < Request.Attempts.size();
             ++attemptIndex)
        {
            if (ShouldContinue && !ShouldContinue())
            {
                break;
            }

            const FImageLoadAttempt& attempt = Request.Attempts[attemptIndex];
            result.ParameterSource = attempt.DiagnosticSource;
            FImageLoadParams params = attempt.Mode == EImageLoadMode::Automatic
                ? ResolveAutomaticParams(Request.FilePath, result.FileSize, bSelfDescribing)
                : attempt.Params;
            params.ConstrainStorageLayout();

            const FImageLoadParams* paramsPtr =
                (!bSelfDescribing && params.IsValid()) ? &params : nullptr;
            const auto decodeStart = std::chrono::steady_clock::now();
            EImageLoadError loadError = EImageLoadError::None;
            std::unique_ptr<FImageData> decoded =
                FImageLoaderFactory::LoadImage(Request.FilePath, paramsPtr, &loadError);
            result.Timings.DecodeMilliseconds +=
                ToMilliseconds(std::chrono::steady_clock::now() - decodeStart);
            result.Params = params;

            if (!decoded || !decoded->IsValid())
            {
                if (loadError == EImageLoadError::ResourceLimit)
                {
                    char buffer[kLoadErrorBufferSize] = {};
                    std::snprintf(buffer, sizeof(buffer),
                        FLocalization::Text(EUiText::ImageResourceLimitDetails),
                        FImageLimits::kMaximumDimension,
                        FImageLimits::kMaximumPixelCount / FImageLimits::kUnitsPerMebi,
                        FImageLimits::kMaximumFrameBytes / FImageLimits::kUnitsPerMebi);
                    result.LastError = buffer;
                    continue;
                }
                // RAW 截断保留实际字节数提示，其余使用加载器给出的准确失败类别。
                result.LastError = loadError != EImageLoadError::None &&
                    !(loadError == EImageLoadError::TruncatedData && !bSelfDescribing)
                    ? GetImageLoadErrorText(loadError) : BuildLoadErrorText(
                    params,
                    result.FileSize,
                    bSelfDescribing);
                continue;
            }

            // 自描述格式以及按扩展名成功兜底的格式，都以解码结果为最终真值。
            if (!paramsPtr)
            {
                result.Params.SetDetectedFormat(decoded->GetFormat());
                result.Params.Width = decoded->GetWidth();
                result.Params.Height = decoded->GetHeight();
                result.Params.Stride = 0;
            }

            result.ImageData = std::move(decoded);
            result.LastError.clear();
            result.SuccessfulAttemptIndex = static_cast<int32_t>(attemptIndex);
            break;
        }

        result.Timings.WorkerTotalMilliseconds =
            ToMilliseconds(std::chrono::steady_clock::now() - workerStart);
        return result;
    }

    struct FContextHints
    {
        int ClientApi = GLFW_OPENGL_API;
        int Major = kFallbackContextMajorVersion;
        int Minor = kFallbackContextMinorVersion;
        int Profile = GLFW_OPENGL_CORE_PROFILE;
        int ForwardCompatible = GLFW_FALSE;
        int DebugContext = GLFW_FALSE;
        int CreationApi = GLFW_NATIVE_CONTEXT_API;
        int Robustness = GLFW_NO_ROBUSTNESS;
        int ReleaseBehavior = GLFW_ANY_RELEASE_BEHAVIOR;
        int NoError = GLFW_FALSE;
    };

    FContextHints ReadContextHints(GLFWwindow* Window)
    {
        FContextHints hints;

        if (!Window)
        {
            return hints;
        }

        hints.ClientApi = glfwGetWindowAttrib(Window, GLFW_CLIENT_API);
        hints.Major = glfwGetWindowAttrib(Window, GLFW_CONTEXT_VERSION_MAJOR);
        hints.Minor = glfwGetWindowAttrib(Window, GLFW_CONTEXT_VERSION_MINOR);
        hints.Profile = glfwGetWindowAttrib(Window, GLFW_OPENGL_PROFILE);
        hints.ForwardCompatible = glfwGetWindowAttrib(Window, GLFW_OPENGL_FORWARD_COMPAT);
        hints.DebugContext = glfwGetWindowAttrib(Window, GLFW_OPENGL_DEBUG_CONTEXT);
        hints.CreationApi = glfwGetWindowAttrib(Window, GLFW_CONTEXT_CREATION_API);
        hints.Robustness = glfwGetWindowAttrib(Window, GLFW_CONTEXT_ROBUSTNESS);
        hints.ReleaseBehavior = glfwGetWindowAttrib(Window, GLFW_CONTEXT_RELEASE_BEHAVIOR);
        hints.NoError = glfwGetWindowAttrib(Window, GLFW_CONTEXT_NO_ERROR);
        return hints;
    }

    void ApplyContextHints(const FContextHints& Hints, bool bVisible)
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, bVisible ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, Hints.ClientApi);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, Hints.Major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, Hints.Minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, Hints.Profile);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, Hints.ForwardCompatible);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, Hints.DebugContext);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, Hints.CreationApi);
        glfwWindowHint(GLFW_CONTEXT_ROBUSTNESS, Hints.Robustness);
        glfwWindowHint(GLFW_CONTEXT_RELEASE_BEHAVIOR, Hints.ReleaseBehavior);
        glfwWindowHint(GLFW_CONTEXT_NO_ERROR, Hints.NoError);
    }

    bool HasMatchingFunctionAddress(const char* Name, GLFWglproc LoadedAddress)
    {
        const GLFWglproc contextAddress = glfwGetProcAddress(Name);
        return contextAddress != nullptr && contextAddress == LoadedAddress;
    }

    bool ValidateGlobalGladDispatchForCurrentContext()
    {
        // 本项目的 GLAD 是进程级全局函数表，不是逐 Context dispatch。GLFW 明确不保证
        // 不同 Context 的函数地址相同，所以只有逐项确认后台 Context 与主 Context 的
        // 上传/同步入口完全一致后才允许并发使用；否则安全退回主线程上传。
        struct FFunctionAddress
        {
            const char* Name;
            GLFWglproc Address;
        };

        const FFunctionAddress required[] = {
            { "glGenTextures", reinterpret_cast<GLFWglproc>(glad_glGenTextures) },
            { "glDeleteTextures", reinterpret_cast<GLFWglproc>(glad_glDeleteTextures) },
            { "glBindTexture", reinterpret_cast<GLFWglproc>(glad_glBindTexture) },
            { "glTexParameteri", reinterpret_cast<GLFWglproc>(glad_glTexParameteri) },
            { "glPixelStorei", reinterpret_cast<GLFWglproc>(glad_glPixelStorei) },
            { "glTexImage2D", reinterpret_cast<GLFWglproc>(glad_glTexImage2D) },
            { "glGetIntegerv", reinterpret_cast<GLFWglproc>(glad_glGetIntegerv) },
            { "glGetTexParameteriv", reinterpret_cast<GLFWglproc>(glad_glGetTexParameteriv) },
            { "glGetError", reinterpret_cast<GLFWglproc>(glad_glGetError) },
            { "glFenceSync", reinterpret_cast<GLFWglproc>(glad_glFenceSync) },
            { "glClientWaitSync", reinterpret_cast<GLFWglproc>(glad_glClientWaitSync) },
            { "glDeleteSync", reinterpret_cast<GLFWglproc>(glad_glDeleteSync) },
            { "glFlush", reinterpret_cast<GLFWglproc>(glad_glFlush) },
            { "glFinish", reinterpret_cast<GLFWglproc>(glad_glFinish) },
        };

        for (const FFunctionAddress& function : required)
        {
            if (!function.Address ||
                !HasMatchingFunctionAddress(function.Name, function.Address))
            {
                LOGW(
                    "AsyncImageLoader",
                    "Shared upload disabled because GL dispatch differs for %s",
                    function.Name);
                return false;
            }
        }

        return true;
    }

    /**
     * 只在需要 GL 的那一段持有上传 Context。
     *
     * 同一 share group 里长期有两个 Context 分别 current 在两个线程上，会让部分驱动
     *（实测 Intel UHD 770）在主线程的 wglMakeCurrent 里自旋不返回。解码与条件变量
     * 等待都不碰 GL，没有理由在那期间占着 Context。
     */
    class FScopedUploadContext
    {
    public:
        explicit FScopedUploadContext(GLFWwindow* Window)
            : AcquiredWindow(Window)
        {
            if (AcquiredWindow)
            {
                glfwMakeContextCurrent(AcquiredWindow);
            }
        }

        ~FScopedUploadContext()
        {
            if (AcquiredWindow)
            {
                glfwMakeContextCurrent(nullptr);
            }
        }

        FScopedUploadContext(const FScopedUploadContext&) = delete;
        FScopedUploadContext& operator=(const FScopedUploadContext&) = delete;

    private:
        GLFWwindow* AcquiredWindow = nullptr;
    };
}

class FAsyncImageLoader::FImpl
{
public:
    struct FReadyResult
    {
        FImageLoadResult Result;
        GLsync Fence = nullptr;
    };

    bool Initialize(GLFWwindow* InMainWindow)
    {
        if (bInitialized)
        {
            return true;
        }

        MainWindow = InMainWindow;
        TryCreateSharedContext();
        bSparseTexturesAvailable = FSparseTexture::IsSupported();

        try
        {
            Worker = std::thread([this]() { WorkerMain(); });
        }
        catch (const std::exception& exception)
        {
            LOGE(
                "AsyncImageLoader",
                "Failed to start image loading thread: %s",
                exception.what());
            DestroyUploadWindow();
            return false;
        }

        bInitialized = true;
        LOGI(
            "AsyncImageLoader",
            "Image loading thread started (shared texture upload: %d)",
            bSharedUploadAvailable ? 1 : 0);
        return true;
    }

    uint64_t Submit(FImageLoadRequest Request)
    {
        std::lock_guard<std::mutex> lock(Mutex);

        if (!bInitialized || bStopping)
        {
            return 0;
        }

        const uint64_t requestId = Mailbox.Submit(std::move(Request));
        WorkAvailable.notify_all();
        return requestId;
    }

    bool Poll(FImageLoadResult& OutResult)
    {
        std::unique_lock<std::mutex> lock(Mutex);

        if (!ReadyResult)
        {
            return false;
        }

        if (!Mailbox.IsLatest(ReadyResult->Result.RequestId))
        {
            FReadyResult stale = std::move(*ReadyResult);
            ReadyResult.reset();
            lock.unlock();
            DestroyReadyResult(stale);
            WorkAvailable.notify_all();
            return false;
        }

        if (ReadyResult->Fence)
        {
            const GLenum wait = glClientWaitSync(ReadyResult->Fence, 0, 0);

            if (wait == GL_TIMEOUT_EXPIRED)
            {
                return false;
            }

            if (wait == GL_WAIT_FAILED)
            {
                LOGW(
                    "AsyncImageLoader",
                    "%s",
                    "Shared upload fence wait failed; retrying texture creation on the main context");
                glDeleteSync(ReadyResult->Fence);
                ReadyResult->Fence = nullptr;
                ReadyResult->Result.TextureData.reset();
                ReadyResult->Result.bNeedsMainThreadUpload =
                    ReadyResult->Result.HasDecodedImage();
                ReadyResult->Result.bSharedUploadUsed = false;
            }
            else
            {
                glDeleteSync(ReadyResult->Fence);
                ReadyResult->Fence = nullptr;
            }
        }

        OutResult = std::move(ReadyResult->Result);
        ReadyResult.reset();
        lock.unlock();
        WorkAvailable.notify_all();
        return true;
    }

    void Cancel()
    {
        std::lock_guard<std::mutex> lock(Mutex);

        if (!bInitialized)
        {
            return;
        }

        Mailbox.Cancel();
        WorkAvailable.notify_all();
    }

    void Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(Mutex);

            if (!bInitialized && !Worker.joinable() && !UploadWindow)
            {
                return;
            }

            bStopping = true;
            Mailbox.Cancel();
            WorkAvailable.notify_all();
        }

        if (Worker.joinable())
        {
            Worker.join();
        }

        GLFWwindow* previousContext = glfwGetCurrentContext();
        GLFWwindow* uploadWindowToDestroy = UploadWindow;

        if (MainWindow)
        {
            glfwMakeContextCurrent(MainWindow);
        }

        std::optional<FReadyResult> ready;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            ready = std::move(ReadyResult);
            ReadyResult.reset();
        }

        if (ready)
        {
            DestroyReadyResult(*ready);
        }

        DestroyUploadWindow();

        // 不恢复到刚刚销毁的隐藏 Context；正常应用路径会恢复主 Context。
        if (previousContext && previousContext != uploadWindowToDestroy)
        {
            glfwMakeContextCurrent(previousContext);
        }

        bInitialized = false;
        bStopping = false;
        bSharedUploadAvailable = false;
        MainWindow = nullptr;
        LOGI("AsyncImageLoader", "%s", "Image loading thread stopped");
    }

    bool IsInitialized() const
    {
        std::lock_guard<std::mutex> lock(Mutex);
        return bInitialized;
    }

    bool IsSharedUploadAvailable() const
    {
        std::lock_guard<std::mutex> lock(Mutex);
        return bSharedUploadAvailable;
    }

private:
    void TryCreateSharedContext()
    {
        if (!MainWindow)
        {
            LOGW(
                "AsyncImageLoader",
                "%s",
                "No main GLFW window; texture upload will use the main thread");
            return;
        }

        const FContextHints hints = ReadContextHints(MainWindow);
        GLFWwindow* previousContext = glfwGetCurrentContext();

        ApplyContextHints(hints, false);
        UploadWindow = glfwCreateWindow(
            kHiddenContextWidth,
            kHiddenContextHeight,
            "YUVRaw.AsyncUpload",
            nullptr,
            MainWindow);
        // GLFW window hints persist globally and ImGui creates secondary viewport Contexts later.
        // Restore the main Context contract immediately so those windows remain 3.3 core/share-compatible.
        ApplyContextHints(hints, true);

        if (!UploadWindow)
        {
            LOGW(
                "AsyncImageLoader",
                "%s",
                "Hidden shared context creation failed; using main-thread texture upload fallback");
            return;
        }

        glfwMakeContextCurrent(UploadWindow);
        bSharedUploadAvailable = ValidateGlobalGladDispatchForCurrentContext();
        glfwMakeContextCurrent(previousContext);

        if (!bSharedUploadAvailable)
        {
            glfwDestroyWindow(UploadWindow);
            UploadWindow = nullptr;
        }
    }

    void DestroyUploadWindow()
    {
        if (!UploadWindow)
        {
            return;
        }

        glfwDestroyWindow(UploadWindow);
        UploadWindow = nullptr;
    }

    bool IsLatest(uint64_t RequestId)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        return !bStopping && Mailbox.IsLatest(RequestId);
    }

    void WorkerMain()
    {
        while (true)
        {
            FImageLoadRequest request;
            {
                std::unique_lock<std::mutex> lock(Mutex);
                WorkAvailable.wait(lock, [this]() {
                    return bStopping || (Mailbox.HasPending() && !ReadyResult.has_value());
                });

                if (bStopping)
                {
                    break;
                }

                if (!Mailbox.TakePending(request))
                {
                    continue;
                }
            }

            FReadyResult ready;
            const uint64_t requestId = request.RequestId;
            const EImageLoadTarget requestTarget = request.Target;
            const std::string requestPath = request.FilePath;
            const auto requestStart = std::chrono::steady_clock::now();

            try
            {
                ready.Result = DecodeRequest(
                    std::move(request),
                    [this, requestId]() {
                        return IsLatest(requestId);
                    });
            }
            catch (const std::exception& exception)
            {
                ready.Result.RequestId = requestId;
                ready.Result.Target = requestTarget;
                ready.Result.FilePath = requestPath;
                ready.Result.LastError = FLocalization::Text(EUiText::BackgroundDecodeException);
                LOGE(
                    "AsyncImageLoader",
                    "Image request %llu threw: %s",
                    static_cast<unsigned long long>(requestId),
                    exception.what());
            }
            catch (...)
            {
                ready.Result.RequestId = requestId;
                ready.Result.Target = requestTarget;
                ready.Result.FilePath = requestPath;
                ready.Result.LastError = FLocalization::Text(EUiText::BackgroundDecodeUnknownException);
                LOGE(
                    "AsyncImageLoader",
                    "Image request %llu threw an unknown exception",
                    static_cast<unsigned long long>(requestId));
            }

            if (!IsLatest(ready.Result.RequestId))
            {
                continue;
            }

            // 已知优先稀疏时提前在后台生成预览；即使没有共享 Context 也不把这部分 CPU 工作挪到 UI。
            // 未超过阈值先试普通整图，只有上传失败后才由 FTextureData 解除 Context 并准备回退预览。
            if (ready.Result.HasDecodedImage() && bSparseTexturesAvailable &&
                ready.Result.TextureLoadOptions.PrefersSparse(
                    ready.Result.ImageData->GetWidth(), ready.Result.ImageData->GetHeight()))
            {
                const auto previewStart = std::chrono::steady_clock::now();
                try
                {
                    ready.Result.TextureData = std::make_unique<FTextureData>();
                    ready.Result.TextureData->PrepareSparsePreview(ready.Result.ImageData.get(),
                        [this, requestId]() { return IsLatest(requestId); });
                }
                catch (const std::exception& exception)
                {
                    ready.Result.TextureData.reset();
                    LOGW("TextureLoad", "Background preview preparation failed: %s", exception.what());
                }
                ready.Result.Timings.TexturePreviewMilliseconds =
                    ToMilliseconds(std::chrono::steady_clock::now() - previewStart);
            }
            if (!IsLatest(ready.Result.RequestId)) continue;

            // 上传和失败回收（glDeleteSync / glDeleteTextures）都要 Context，
            // 所以持有区间必须覆盖本轮末尾的发布与丢弃分支。
            const FScopedUploadContext uploadContext(
                bSharedUploadAvailable && UploadWindow && ready.Result.HasDecodedImage()
                    ? UploadWindow
                    : nullptr);

            if (ready.Result.HasDecodedImage())
            {
                try
                {
                    if (bSharedUploadAvailable && UploadWindow)
                    {
                        const auto uploadStart = std::chrono::steady_clock::now();
                        if (!ready.Result.TextureData)
                            ready.Result.TextureData = std::make_unique<FTextureData>();
                        FTextureCreateError textureError;
                        const bool bUploaded = ready.Result.TextureData->CreateFromImageData(
                            ready.Result.ImageData.get(), &textureError, ready.Result.TextureLoadOptions,
                            [this, requestId]() { return IsLatest(requestId); });
                        const int64_t previewMilliseconds =
                            ready.Result.TextureData->GetPreviewPreparationMilliseconds();
                        ready.Result.Timings.TexturePreviewMilliseconds += previewMilliseconds;
                        ready.Result.Timings.WorkerUploadMilliseconds =
                            std::max<int64_t>(0, ToMilliseconds(std::chrono::steady_clock::now() - uploadStart)
                                - previewMilliseconds);

                        if (bUploaded && ready.Result.TextureData->IsValid())
                        {
                            ready.Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

                            if (ready.Fence)
                            {
                                // 跨 Context 等待不能替生产者刷新命令流；fence 后必须显式 Flush。
                                glFlush();
                                ready.Result.bSharedUploadUsed = true;
                            }
                            else
                            {
                                // fence 创建失败时在后台 Context 自己等完，仍不阻塞 UI 线程。
                                glFinish();
                                ready.Result.bSharedUploadUsed = true;
                            }
                        }
                        else
                        {
                            ready.Result.LastError = textureError.GetText();
                            ready.Result.TextureData.reset();
                            ready.Result.bNeedsMainThreadUpload = true;
                            LOGW(
                                "AsyncImageLoader",
                                "%s",
                                "Shared-context texture upload failed; scheduling main-context retry");
                        }
                    }
                    else
                    {
                        ready.Result.bNeedsMainThreadUpload = true;
                    }
                }
                catch (const std::exception& exception)
                {
                    ready.Result.TextureData.reset();
                    ready.Result.bNeedsMainThreadUpload = true;
                    ready.Result.bSharedUploadUsed = false;
                    LOGW(
                        "AsyncImageLoader",
                        "Shared-context texture upload threw; scheduling main-context retry: %s",
                        exception.what());
                }
                catch (...)
                {
                    ready.Result.TextureData.reset();
                    ready.Result.bNeedsMainThreadUpload = true;
                    ready.Result.bSharedUploadUsed = false;
                    LOGW(
                        "AsyncImageLoader",
                        "%s",
                        "Shared-context texture upload threw an unknown exception; scheduling main-context retry");
                }
            }

            // DecodeRequest 在纹理上传前结束；这里覆盖为真正的后台阶段总耗时。
            ready.Result.Timings.WorkerTotalMilliseconds =
                ToMilliseconds(std::chrono::steady_clock::now() - requestStart);

            if (!IsLatest(ready.Result.RequestId))
            {
                DestroyReadyResult(ready);
                continue;
            }

            bool bPublished = false;
            {
                std::lock_guard<std::mutex> lock(Mutex);

                if (!bStopping && Mailbox.IsLatest(ready.Result.RequestId))
                {
                    ReadyResult = std::move(ready);
                    bPublished = true;
                }
            }

            if (!bPublished)
            {
                // Shutdown 可能恰好发生在上一次代际检查与发布锁之间；此处显式
                // 删除 raw GLsync，不能只依赖 TextureData 的 RAII。
                DestroyReadyResult(ready);
            }

            WorkAvailable.notify_all();
        }
    }

    static void DestroyReadyResult(FReadyResult& Ready)
    {
        if (Ready.Fence)
        {
            glDeleteSync(Ready.Fence);
            Ready.Fence = nullptr;
        }

        Ready.Result.TextureData.reset();
    }

    GLFWwindow* MainWindow = nullptr;
    GLFWwindow* UploadWindow = nullptr;
    std::thread Worker;

    mutable std::mutex Mutex;
    std::condition_variable WorkAvailable;
    FImageLoadRequestMailbox Mailbox;
    std::optional<FReadyResult> ReadyResult;

    bool bInitialized = false;
    bool bStopping = false;
    bool bSharedUploadAvailable = false;
    bool bSparseTexturesAvailable = false;
};

FAsyncImageLoader::FAsyncImageLoader()
    : Impl(std::make_unique<FImpl>())
{
}

FAsyncImageLoader::~FAsyncImageLoader()
{
    Shutdown();
}

bool FAsyncImageLoader::Initialize(GLFWwindow* MainWindow)
{
    return Impl && Impl->Initialize(MainWindow);
}

uint64_t FAsyncImageLoader::Submit(FImageLoadRequest Request)
{
    // 设置存储只在主线程访问；后台与主线程回退共用不可变的请求快照。
    Request.TextureLoadOptions = FUserSettings::GetTextureLoadOptions();
    return Impl ? Impl->Submit(std::move(Request)) : 0;
}

bool FAsyncImageLoader::Poll(FImageLoadResult& OutResult)
{
    return Impl && Impl->Poll(OutResult);
}

void FAsyncImageLoader::Cancel()
{
    if (Impl)
    {
        Impl->Cancel();
    }
}

void FAsyncImageLoader::Shutdown()
{
    if (Impl)
    {
        Impl->Shutdown();
    }
}

bool FAsyncImageLoader::IsInitialized() const
{
    return Impl && Impl->IsInitialized();
}

bool FAsyncImageLoader::IsSharedUploadAvailable() const
{
    return Impl && Impl->IsSharedUploadAvailable();
}

FImageLoadResult FAsyncImageLoader::DecodeOnCallingThread(FImageLoadRequest Request)
{
    Request.TextureLoadOptions = FUserSettings::GetTextureLoadOptions();
    if (Request.EnqueuedAt.time_since_epoch().count() == 0)
    {
        Request.EnqueuedAt = std::chrono::steady_clock::now();
    }

    const uint64_t requestId = Request.RequestId;
    const EImageLoadTarget target = Request.Target;
    const std::string filePath = Request.FilePath;
    const int32_t attemptCount = static_cast<int32_t>(Request.Attempts.size());

    try
    {
        return DecodeRequest(std::move(Request), FContinuePredicate());
    }
    catch (const std::exception& exception)
    {
        LOGE(
            "AsyncImageLoader",
            "Synchronous image request %llu threw: %s",
            static_cast<unsigned long long>(requestId),
            exception.what());
    }
    catch (...)
    {
        LOGE(
            "AsyncImageLoader",
            "Synchronous image request %llu threw an unknown exception",
            static_cast<unsigned long long>(requestId));
    }

    FImageLoadResult result;
    result.RequestId = requestId;
    result.Target = target;
    result.FilePath = filePath;
    result.AttemptCount = attemptCount;
    result.LastError = FLocalization::Text(EUiText::SynchronousDecodeException);
    return result;
}

bool FAsyncImageLoader::UploadTextureOnCallingThread(FImageLoadResult& Result)
{
    if (!Result.HasDecodedImage())
    {
        return false;
    }

    const auto uploadStart = std::chrono::steady_clock::now();
    std::unique_ptr<FTextureData> textureData;
    FTextureCreateError textureError;
    bool bUploaded = false;
    // 回退上传按主 Context 的实际结果报告，不能残留共享 Context 的失败原因。
    Result.LastError.clear();

    try
    {
        textureData = std::move(Result.TextureData);
        if (!textureData)
        {
            textureData = std::make_unique<FTextureData>();
        }
        bUploaded = textureData->CreateFromImageData(Result.ImageData.get(), &textureError, Result.TextureLoadOptions) &&
            textureData->IsValid();
    }
    catch (const std::bad_alloc&)
    {
        Result.LastError = FLocalization::Text(EUiText::ImageOutOfMemory);
        LOGE("AsyncImageLoader", "%s", "CPU allocation failed while preparing main-context texture upload");
    }
    catch (const std::exception& exception)
    {
        LOGE(
            "AsyncImageLoader",
            "Main-context texture upload threw: %s",
            exception.what());
    }
    catch (...)
    {
        LOGE(
            "AsyncImageLoader",
            "%s",
            "Main-context texture upload threw an unknown exception");
    }

    const int64_t previewMilliseconds = textureData ? textureData->GetPreviewPreparationMilliseconds() : 0;
    Result.Timings.TexturePreviewMilliseconds += previewMilliseconds;
    Result.Timings.MainUploadMilliseconds = std::max<int64_t>(0,
        ToMilliseconds(std::chrono::steady_clock::now() - uploadStart) - previewMilliseconds);

    if (!bUploaded)
    {
        if (Result.LastError.empty())
        {
            Result.LastError = textureError.GetText();
        }
        Result.TextureData.reset();
        Result.bNeedsMainThreadUpload = false;
        return false;
    }

    Result.TextureData = std::move(textureData);
    Result.bNeedsMainThreadUpload = false;
    return true;
}
