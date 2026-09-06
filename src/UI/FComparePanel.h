#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Image/FImageCompare.h"
#include "Image/FImageFormat.h"

class FImageDocument;

/**
 * 查看器当前显示哪一幅图
 */
enum class EViewTarget
{
    Main,        ///< 单图切换：当前显示主图
    Compare,     ///< 单图切换：当前显示对比图
    Diff,        ///< 差值图
    SideBySide,  ///< 平铺：主图与对比图可左右或上下排列，默认同步缩放与平移
};

constexpr bool IsSingleImageViewTarget(EViewTarget Target)
{
    return Target == EViewTarget::Main || Target == EViewTarget::Compare;
}

/**
 * 双图对比面板
 *
 * 调 ISP / 编码器时的主力工具：加载一张参考图，算出差值并给出
 * 最大差 / 平均差 / 差异像素占比 / PSNR。
 *
 * 差值在 CPU 上算（两边先各自转 RGB8 再相减），因此**两幅图格式可以不同**，
 * 只要分辨率一致 —— 比如拿 NV21 的解码结果和 PNG 参考图对比。
 */
class FComparePanel
{
public:
    FComparePanel();

    void Render();

    void SetDocuments(FImageDocument* InMain, FImageDocument* InCompare, FImageDocument* InDiff)
    {
        MainDocument = InMain;
        CompareDocument = InCompare;
        DiffDocument = InDiff;
    }

    /// 用户点击"选择对比图"时触发，参数是选中的路径
    void SetOnPickCompareFile(std::function<void(const std::string&)> Callback) { OnPickCompareFile = std::move(Callback); }

    /// 用户请求重算差值时触发
    void SetOnComputeDiff(std::function<void(float)> Callback) { OnComputeDiff = std::move(Callback); }

    /// 用户点击主图旁的"×"，要求移除主图
    void SetOnClearMain(std::function<void()> Callback) { OnClearMain = std::move(Callback); }

    /// 用户点击对比图旁的"×"，要求移除对比图
    void SetOnClearCompare(std::function<void()> Callback) { OnClearCompare = std::move(Callback); }

    EViewTarget GetViewTarget() const { return ViewTarget; }

    /**
     * 返回加载新对比图后应恢复的持久模式。
     * 单图切换时先显示刚加载的对比图，让用户能立即确认选择结果。
     */
    EViewTarget GetRememberedCompareViewTarget() const;

    void SetViewTarget(EViewTarget InTarget)
    {
        ViewTarget = InTarget;

        if (IsSingleImageViewTarget(InTarget))
        {
            SingleImageTarget = InTarget;
        }
    }

    void SetStats(const FCompareStats& InStats) { Stats = InStats; }
    const FCompareStats& GetStats() const { return Stats; }

    float GetGain() const { return Gain; }

private:
    /**
     * 应用面板中的用户选择；只有单图切换和平铺会更新跨会话偏好。
     */
    void SetUserSelectedViewTarget(EViewTarget InTarget);

    /**
     * 在标题右边画一个"×"按钮
     * @return 是否被点击
     */
    bool RenderClearButton(const char* Id, const char* Tooltip, bool bEnabled);

    FImageDocument* MainDocument;
    FImageDocument* CompareDocument;
    FImageDocument* DiffDocument;

    EViewTarget ViewTarget;

    /// 离开单图切换后仍记住最后显示的是主图还是对比图，重新进入时继续该目标。
    EViewTarget SingleImageTarget;

    /// 差值放大倍数：1 倍下细微差异几乎看不见
    float Gain;

    FCompareStats Stats;

    std::function<void(const std::string&)> OnPickCompareFile;
    std::function<void(float)> OnComputeDiff;
    std::function<void()> OnClearMain;
    std::function<void()> OnClearCompare;
};
