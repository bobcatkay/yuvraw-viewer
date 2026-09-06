#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Image/FImageData.h"
#include "Image/FImageLoadParams.h"
#include "gl/FTextureData.h"

struct GLFWwindow;

/** 图片加载结果最终要提交到哪个固定文档槽位。 */
enum class EImageLoadTarget
{
    Main,
    Compare,
};

/** 一次加载尝试是自动识别，还是严格使用调用方给定的参数。 */
enum class EImageLoadMode
{
    Automatic,
    Explicit,
};

inline constexpr int32_t kInvalidImageLoadAttemptIndex = -1;

/**
 * 同一请求可以有多次有序尝试。
 *
 * 例如同目录属性继承先严格尝试上一张图的参数，失败后再走自动识别；跨目录对比图
 * 则可以依次尝试缓存、文件自身元数据和主图参数。工作线程只按顺序执行这些快照，
 * 不访问文档、面板或任何其它 UI 状态。
 */
struct FImageLoadAttempt
{
    EImageLoadMode Mode = EImageLoadMode::Automatic;
    FImageLoadParams Params;
    std::string DiagnosticSource;
};

/** 提交给专用加载线程的不可变请求快照。 */
struct FImageLoadRequest
{
    uint64_t RequestId = 0;
    EImageLoadTarget Target = EImageLoadTarget::Main;
    std::string FilePath;
    std::vector<FImageLoadAttempt> Attempts;
    std::chrono::steady_clock::time_point EnqueuedAt;
};

/** 分段计时，帮助区分文件检查、解码、纹理上传和排队等待。 */
struct FImageLoadTimings
{
    int64_t QueueWaitMilliseconds = 0;
    int64_t InspectionMilliseconds = 0;
    int64_t DecodeMilliseconds = 0;
    int64_t WorkerUploadMilliseconds = 0;
    int64_t MainUploadMilliseconds = 0;
    int64_t WorkerTotalMilliseconds = 0;
};

/**
 * 工作线程产物。
 *
 * Poll() 只会在共享 Context 的 fence 已完成后交出此对象，因此调用方拿到的
 * TextureData 可以直接在主 Context 中渲染。共享上传不可用或失败时，
 * bNeedsMainThreadUpload 为 true，调用方在原子提交前执行一次主线程回退上传。
 */
struct FImageLoadResult
{
    uint64_t RequestId = 0;
    EImageLoadTarget Target = EImageLoadTarget::Main;
    std::string FilePath;
    FImageLoadParams Params;
    uint64_t FileSize = 0;
    std::string LastError;
    std::string ParameterSource;

    std::unique_ptr<FImageData> ImageData;
    std::unique_ptr<FTextureData> TextureData;

    FImageLoadTimings Timings;
    int32_t SuccessfulAttemptIndex = kInvalidImageLoadAttemptIndex;
    int32_t AttemptCount = 0;
    bool bNeedsMainThreadUpload = false;
    bool bSharedUploadUsed = false;

    bool HasDecodedImage() const
    {
        return ImageData && ImageData->IsValid();
    }
};

/**
 * “一个执行中 + 一个最新待处理”邮箱。
 *
 * 类本身不加锁；FAsyncImageLoader 在同一把互斥锁下使用它。单独公开是为了让
 * 合并、取消和请求代际语义能由不依赖 OpenGL 的离线测试直接覆盖。
 */
class FImageLoadRequestMailbox
{
public:
    uint64_t Submit(FImageLoadRequest Request)
    {
        ++NextRequestId;

        // 0 作为“异步服务不可用”的保留返回值；极端回绕时跳过它。
        if (NextRequestId == 0)
        {
            ++NextRequestId;
        }

        LatestRequestId = NextRequestId;
        Request.RequestId = LatestRequestId;
        Request.EnqueuedAt = std::chrono::steady_clock::now();
        PendingRequest = std::move(Request);
        return LatestRequestId;
    }

    bool TakePending(FImageLoadRequest& OutRequest)
    {
        if (!PendingRequest)
        {
            return false;
        }

        OutRequest = std::move(*PendingRequest);
        PendingRequest.reset();
        return true;
    }

    void Cancel()
    {
        ++NextRequestId;

        if (NextRequestId == 0)
        {
            ++NextRequestId;
        }

        LatestRequestId = NextRequestId;
        PendingRequest.reset();
    }

    bool HasPending() const { return PendingRequest.has_value(); }
    bool IsLatest(uint64_t RequestId) const { return RequestId == LatestRequestId; }
    uint64_t GetLatestRequestId() const { return LatestRequestId; }

private:
    uint64_t NextRequestId = 0;
    uint64_t LatestRequestId = 0;
    std::optional<FImageLoadRequest> PendingRequest;
};

/**
 * 图片切换专用后台服务。
 *
 * 与计算差值/导出的 FAsyncJob 相互独立：它保持文件浏览器可交互，后台完成文件读取、
 * 解码与可选的共享 Context 纹理上传，并只把完全准备好的资源交给主线程原子提交。
 */
class FAsyncImageLoader
{
public:
    FAsyncImageLoader();
    ~FAsyncImageLoader();

    FAsyncImageLoader(const FAsyncImageLoader&) = delete;
    FAsyncImageLoader& operator=(const FAsyncImageLoader&) = delete;

    /**
     * 在主线程、主 OpenGL Context 当前时初始化。
     * 隐藏共享 Context 创建失败不会使初始化失败，服务会退回“后台解码 + 主线程上传”。
     */
    bool Initialize(GLFWwindow* MainWindow);

    /** 提交并替换尚未开始的旧请求，返回非零代际 ID。 */
    uint64_t Submit(FImageLoadRequest Request);

    /**
     * 主线程逐帧非阻塞轮询。共享上传尚未越过 fence 时立即返回 false。
     */
    bool Poll(FImageLoadResult& OutResult);

    /** 取消执行中结果的提交并清除最新待处理请求。 */
    void Cancel();

    /** 停止并等待工作线程，释放隐藏 Context；必须在主 Context/GLFW 仍有效时调用。 */
    void Shutdown();

    bool IsInitialized() const;
    bool IsSharedUploadAvailable() const;

    /** 同步兼容入口：复用与后台线程完全相同的参数解析与解码规则。 */
    static FImageLoadResult DecodeOnCallingThread(FImageLoadRequest Request);

    /** 共享上传不可用时，在主线程完成回退纹理创建并记录耗时。 */
    static bool UploadTextureOnCallingThread(FImageLoadResult& Result);

private:
    class FImpl;
    std::unique_ptr<FImpl> Impl;
};
