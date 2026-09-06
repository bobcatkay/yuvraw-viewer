#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <string>

/**
 * 一个跑在工作线程上的后台任务，外加界面需要的"忙碌"状态
 *
 * 计算差值和导出都可能跑上几秒。它们以前直接在主线程里做，期间界面整个卡住 ——
 * 连"正在忙"的转圈动画都画不出来，因为动画本身也得靠主循环出帧。
 * 现在挪到工作线程，主线程每帧 Poll() 一次，任务完成时在**主线程**上执行收尾。
 *
 * 收尾必须回主线程：建纹理要 GL 上下文，改面板状态要 ImGui 上下文，两者都只属于主线程。
 *
 * 同一时刻只允许一个任务。发起这两个动作时整个界面都会被置灰，不可能并发发起，
 * 所以不必引入线程池，也不必处理任务之间的依赖。
 */
class FAsyncJob
{
public:
    FAsyncJob();
    ~FAsyncJob();

    FAsyncJob(const FAsyncJob&) = delete;
    FAsyncJob& operator=(const FAsyncJob&) = delete;

    /**
     * 启动一个任务
     *
     * @param InLabel  遮罩上显示的文字，例如 u8"正在导出..."
     * @param InTotal  总步数。>1 时遮罩上给出 "3 / 10"；<=1 表示进度不可知，只转圈
     * @param Work     在工作线程上执行。**不得触碰 GL / ImGui / 任何 UI 状态**
     * @param InOnDone Work 返回后，由下一次 Poll() 在主线程上执行
     * @return 已有任务在跑时不启动，返回 false
     */
    bool Start(std::string InLabel, int32_t InTotal, std::function<void()> Work, std::function<void()> InOnDone);

    /**
     * 主线程每帧调用一次。任务已完成就执行收尾并回到空闲。
     */
    void Poll();

    bool IsRunning() const { return bRunning; }

    const std::string& GetLabel() const { return Label; }

    int32_t GetTotal() const { return Total; }
    int32_t GetCompleted() const { return Completed.load(std::memory_order_relaxed); }

    /**
     * 由工作线程调用，报告已完成的步数
     */
    void ReportProgress(int32_t InCompleted) { Completed.store(InCompleted, std::memory_order_relaxed); }

private:
    std::future<void> Future;
    std::function<void()> OnDone;

    std::string Label;

    int32_t Total;
    std::atomic<int32_t> Completed;

    bool bRunning;
};
