#pragma once

#include <cstdint>
#include <string>

/**
 * 窗口所在显示器的 HDR 能力
 *
 * 全部来自系统查询，不做任何猜测。bValid 为 false 时其余字段是保守的默认值。
 */
struct FHdrDisplayInfo
{
    /// 查询是否成功。失败时下面的值是默认值，不代表显示器真实能力
    bool bValid = false;

    /// 显示器当前是否处于 HDR 模式（Windows 显示设置里的"使用 HDR"）
    bool bHdrEnabled = false;

    /// 峰值亮度 cd/m^2（小面积高光）
    float MaxNits = 1000.0f;

    /// 最低亮度 cd/m^2
    float MinNits = 0.0f;

    /// 全屏白的亮度 cd/m^2。通常远低于 MaxNits
    float MaxFullFrameNits = 400.0f;

    /**
     * SDR 内容的白电平 cd/m^2
     *
     * 就是显示设置里"SDR 内容亮度"滑块的物理含义。scRGB 帧缓冲里 1.0 = 80 nit 是固定的，
     * 所以界面要按这个值缩放，否则在 HDR 下要么发灰要么刺眼。
     */
    float SdrWhiteNits = 200.0f;

    /// 显示器设备名，仅用于日志与界面展示
    std::string OutputName;
};

/**
 * 显示器 HDR 能力探测
 *
 * 走 DXGI（IDXGIOutput6::GetDesc1）+ Win32 显示配置（SDR 白电平），
 * 与渲染 API 无关 —— 哪怕将来呈现层换成 D3D11 或 Vulkan，这个文件也不用动。
 */
namespace FHdrDisplay
{
    /**
     * 查询窗口当前所在那块显示器
     *
     * **窗口跨屏拖动后要重新查**：两块屏幕的 HDR 状态和白电平可以完全不同。
     *
     * @param Hwnd 窗口句柄（HWND）
     */
    FHdrDisplayInfo Query(void* Hwnd);
}
