#include "FHdrDisplay.h"

#include "Util.h"

#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <vector>

// 老一些的 Windows SDK 里没有这个常量，缺了它整段 SDR 白电平查询就编不过。
// 值取自 wingdi.h（DISPLAYCONFIG_DEVICE_INFO_TYPE 枚举）
#ifndef DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL
#define DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL 11
#endif

using Microsoft::WRL::ComPtr;

namespace
{
    /**
     * 由 GDI 设备名（\\.\DISPLAY1 这种）查 SDR 白电平
     *
     * DXGI 不给这个值，只能走 Win32 的显示配置接口：
     * 先 QueryDisplayConfig 列出全部路径，再按 source 的 GDI 名字找到对应那条，
     * 最后用它的 target 去问 SDR 白电平。
     *
     * @return 查不到时返回 0，调用方应保留默认值
     */
    float QuerySdrWhiteNits(const wchar_t* GdiDeviceName)
    {
        UINT32 pathCount = 0;
        UINT32 modeCount = 0;

        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        {
            return 0.0f;
        }

        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                               &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
        {
            return 0.0f;
        }

        paths.resize(pathCount);

        for (const DISPLAYCONFIG_PATH_INFO& path : paths)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
            sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            sourceName.header.size = sizeof(sourceName);
            sourceName.header.adapterId = path.sourceInfo.adapterId;
            sourceName.header.id = path.sourceInfo.id;

            if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS)
            {
                continue;
            }

            if (wcscmp(sourceName.viewGdiDeviceName, GdiDeviceName) != 0)
            {
                continue;
            }

            // 找到了这条路径，问它的 target 要白电平
            struct FSdrWhiteLevel
            {
                DISPLAYCONFIG_DEVICE_INFO_HEADER header;
                ULONG SDRWhiteLevel;
            } whiteLevel = {};

            whiteLevel.header.type =
                static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL);
            whiteLevel.header.size = sizeof(whiteLevel);
            whiteLevel.header.adapterId = path.targetInfo.adapterId;
            whiteLevel.header.id = path.targetInfo.id;

            if (DisplayConfigGetDeviceInfo(&whiteLevel.header) != ERROR_SUCCESS)
            {
                return 0.0f;
            }

            // 单位是 "白电平 / 80 * 1000"，反算回 cd/m^2
            return static_cast<float>(whiteLevel.SDRWhiteLevel) / 1000.0f * 80.0f;
        }

        return 0.0f;
    }
}

namespace FHdrDisplay
{
    FHdrDisplayInfo Query(void* Hwnd)
    {
        FHdrDisplayInfo info;

        HWND hwnd = static_cast<HWND>(Hwnd);

        if (!hwnd)
        {
            return info;
        }

        // 先确定窗口落在哪块屏上。MONITORINFOEX 里带 GDI 设备名，
        // 后面查 SDR 白电平和匹配 DXGI 输出都靠它
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

        if (!monitor)
        {
            return info;
        }

        MONITORINFOEXW monitorInfo = {};
        monitorInfo.cbSize = sizeof(monitorInfo);

        if (!GetMonitorInfoW(monitor, &monitorInfo))
        {
            return info;
        }

        ComPtr<IDXGIFactory1> factory;

        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        {
            return info;
        }

        for (UINT adapterIndex = 0; ; ++adapterIndex)
        {
            ComPtr<IDXGIAdapter1> adapter;

            if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            for (UINT outputIndex = 0; ; ++outputIndex)
            {
                ComPtr<IDXGIOutput> output;

                if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }

                ComPtr<IDXGIOutput6> output6;

                if (FAILED(output.As(&output6)))
                {
                    // IDXGIOutput6 是 Windows 10 1703 引入的，拿不到就没法判断 HDR
                    continue;
                }

                DXGI_OUTPUT_DESC1 desc = {};

                if (FAILED(output6->GetDesc1(&desc)))
                {
                    continue;
                }

                if (desc.Monitor != monitor)
                {
                    continue;
                }

                info.bValid = true;

                // 只有 PQ + BT.2020 这一种色彩空间代表"系统已进入 HDR 模式"
                info.bHdrEnabled = (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);

                info.MaxNits = desc.MaxLuminance;
                info.MinNits = desc.MinLuminance;
                info.MaxFullFrameNits = desc.MaxFullFrameLuminance;

                char nameBuffer[64] = {};
                WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1, nameBuffer, sizeof(nameBuffer) - 1, nullptr, nullptr);
                info.OutputName = nameBuffer;

                const float sdrWhite = QuerySdrWhiteNits(monitorInfo.szDevice);

                if (sdrWhite > 1.0f)
                {
                    info.SdrWhiteNits = sdrWhite;
                }

                // 有些显示器/驱动把 MaxLuminance 报成 0，用它当上限会让整幅图变黑
                if (info.MaxNits < info.SdrWhiteNits)
                {
                    info.MaxNits = info.SdrWhiteNits;
                }

                return info;
            }
        }

        return info;
    }
}
