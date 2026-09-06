#include "FAsyncJob.h"

#include "Util.h"

#include <chrono>
#include <utility>

FAsyncJob::FAsyncJob()
    : Total(0)
    , Completed(0)
    , bRunning(false)
{
}

FAsyncJob::~FAsyncJob()
{
    // 关窗时任务可能还在跑，而工作线程读着的正是主线程马上要析构的对象。
    // 这里必须等它结束；收尾不再执行 —— 界面已经在拆了。
    if (Future.valid())
    {
        Future.wait();
    }
}

bool FAsyncJob::Start(std::string InLabel, int32_t InTotal, std::function<void()> Work, std::function<void()> InOnDone)
{
    if (bRunning || !Work)
    {
        return false;
    }

    Label = std::move(InLabel);
    Total = InTotal;
    Completed.store(0, std::memory_order_relaxed);
    OnDone = std::move(InOnDone);

    Future = std::async(std::launch::async, std::move(Work));
    bRunning = true;

    return true;
}

void FAsyncJob::Poll()
{
    if (!bRunning || !Future.valid())
    {
        return;
    }

    if (Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        return;
    }

    // 先把自身状态清干净再执行收尾：收尾里完全可能紧接着发起下一个任务
    std::future<void> finished = std::move(Future);
    std::function<void()> onDone = std::move(OnDone);

    Future = std::future<void>();
    OnDone = nullptr;
    bRunning = false;

    try
    {
        // Work 抛出的异常存在 future 里，get() 时才浮出来
        finished.get();
    }
    catch (const std::exception& e)
    {
        LOGE("Poll", "Background job \"%s\" threw: %s", Label.c_str(), e.what());
    }

    // 抛异常时也要走收尾：调用方靠它把"失败"显示出来
    if (onDone)
    {
        onDone();
    }
}
