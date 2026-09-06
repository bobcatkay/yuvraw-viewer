#pragma once

/**
 * 瞬时提示（toast）
 *
 * 用于"刚做完的那件事"的回执 —— 复制、保存这类没有后续界面的动作，
 * 否则用户无从判断到底生效没有。
 *
 * 与 FUiIcons::DrawBusyOverlay 同一套路：画在主视口的**前景绘制列表**上，不开 ImGui 窗口。
 * 开窗口就要处理焦点、输入捕获和 DockSpace 的交互，而提示本身既不接收输入也不该抢焦点；
 * 画在前景列表还能压在忙碌遮罩之上，并且不受 BeginDisabled 那层置灰的透明度影响。
 *
 * 同一时刻只保留一条：这类提示都是即时回执，同时挂两条只会互相遮挡，后来的直接顶掉前一条。
 * 全部状态是文件级静态变量，**只能在主线程（出帧线程）调用**。
 */
namespace FToast
{
    /**
     * 弹一条提示
     *
     * @param Text            提示文字
     * @param DurationSeconds 总时长（含淡入淡出），到点自动消失
     */
    void Show(const char* Text, float DurationSeconds = 2.0f);

    /**
     * 每帧调用一次，放在所有面板渲染之后
     */
    void Render();
}
