#pragma once

#include "Image/FImageLoadParams.h"
#include "FLocalization.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/**
 * 跨会话保存的用户偏好
 *
 * 存放在 %APPDATA%\YUVRaw\settings.ini，格式是最朴素的 key=value。
 * 首次启动仅复制旧 ImageDevTool 目录中尚未迁入的设置和图片属性缓存，不修改旧文件。
 * 只放"下次启动希望恢复"的少量状态；主窗口边界与六项基础主题色在这里保存，
 * Dock 面板布局仍归 imgui.ini 管，两者互不干涉。
 */
namespace FUserSettings
{
    FLocalization::ELanguage GetLanguage();
    /// 保存成功后才生效；失败时保留原语言，调用方可显示失败提示。
    bool SetLanguage(FLocalization::ELanguage Language);

    /**
     * 用户命名的一组无头图像加载参数。
     *
     * 预设只是现有 EImageFormat 的别名与参数快照，不会创建新的运行时格式描述；
     * 着色器、纹理和平面布局仍由 Params.Format 对应的 FImageFormatDesc 驱动。
     */
    struct FImageFormatPreset
    {
        std::string Name;
        FImageLoadParams Params;

        bool operator==(const FImageFormatPreset& Other) const
        {
            return Name == Other.Name &&
                Params.Format == Other.Params.Format &&
                Params.Width == Other.Params.Width &&
                Params.Height == Other.Params.Height &&
                Params.Stride == Other.Params.Stride &&
                Params.BitsPerPixel == Other.Params.BitsPerPixel &&
                Params.BayerPattern == Other.Params.BayerPattern &&
                Params.ByteOrder == Other.Params.ByteOrder &&
                Params.SampleAlignment == Other.Params.SampleAlignment;
        }

        bool operator!=(const FImageFormatPreset& Other) const
        {
            return !(*this == Other);
        }
    };

    /**
     * 主窗口上次处于普通状态时的位置与大小。
     *
     * GLFW 的窗口坐标和尺寸都是逻辑像素；最大化时仍保存最后一次普通窗口边界，
     * 这样下次取消最大化后不会退回到铺满屏幕的错误尺寸。
     */
    struct FWindowPlacement
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Width = 0;
        int32_t Height = 0;
        bool bMaximized = false;
    };

    /// 主题颜色以 8 bit RGB 保存，不让持久层依赖 ImGui 类型。
    struct FThemeColor
    {
        uint8_t R = 0;
        uint8_t G = 0;
        uint8_t B = 0;

        constexpr bool operator==(const FThemeColor& Other) const
        {
            return R == Other.R && G == Other.G && B == Other.B;
        }

        constexpr bool operator!=(const FThemeColor& Other) const
        {
            return !(*this == Other);
        }
    };

    /**
     * 可由用户单独编辑和重置的必要主题颜色。
     *
     * 悬停、按下、选中、次要文字和浮层背景等状态色由 FUiTheme 自动推导，
     * 避免设置页暴露大量难以理解且容易互相冲突的细项。
     */
    enum class EThemeColorRole : uint8_t
    {
        Accent,
        InterfaceBackground,
        Text,
        Border,
        InputBackground,
        Control,
        Count,
    };

    inline constexpr size_t kThemeColorRoleCount =
        static_cast<size_t>(EThemeColorRole::Count);

    struct FThemePalette
    {
        std::array<FThemeColor, kThemeColorRoleCount> Colors{};

        constexpr FThemeColor& Get(EThemeColorRole Role)
        {
            return Colors[static_cast<size_t>(Role)];
        }

        constexpr const FThemeColor& Get(EThemeColorRole Role) const
        {
            return Colors[static_cast<size_t>(Role)];
        }

        constexpr bool operator==(const FThemePalette& Other) const
        {
            for (size_t colorIndex = 0;
                 colorIndex < kThemeColorRoleCount;
                 ++colorIndex)
            {
                if (Colors[colorIndex] != Other.Colors[colorIndex])
                {
                    return false;
                }
            }

            return true;
        }

        constexpr bool operator!=(const FThemePalette& Other) const
        {
            return !(*this == Other);
        }
    };

    /**
     * 可跨会话恢复的对比模式。
     *
     * 差值图是一次计算产生的临时视图，刻意不放进这个枚举，避免它覆盖用户常用模式。
     */
    enum class ECompareMode
    {
        SingleImageSwitch,
        SideBySide,
    };

    /// 图片配置缓存默认保存最近 100 张；0 表示禁用缓存。
    inline constexpr size_t kDefaultImageConfigCacheCapacity = 100;
    inline constexpr size_t kMaximumImageConfigCacheCapacity = 10000;

    /// 直方图显示方式是全局用户习惯，不随当前图片切换。
    inline constexpr bool kDefaultHistogramOverlayEnabled = false;
    inline constexpr bool kDefaultHistogramLogScaleEnabled = false;

    /// 首次运行默认使用单图切换，避免尚未加载对比图时出现空白分栏。
    inline constexpr ECompareMode kDefaultCompareMode =
        ECompareMode::SingleImageSwitch;

    /// 右侧面板标签与平铺切换按钮的默认选中色 #0076A1。
    inline constexpr FThemeColor kDefaultThemeAccentColor{ 0, 118, 161 };

    /// 六个基础色足以构造完整浅色主题；颜色顺序必须与 EThemeColorRole 保持一致。
    inline constexpr FThemePalette kDefaultThemePalette{
        std::array<FThemeColor, kThemeColorRoleCount>{
            kDefaultThemeAccentColor, // Accent
            FThemeColor{ 240, 240, 240 }, // InterfaceBackground
            FThemeColor{ 0, 0, 0 },       // Text
            FThemeColor{ 209, 216, 226 }, // Border
            FThemeColor{ 216, 221, 228 }, // InputBackground
            FThemeColor{ 42, 161, 152 },  // Control
        }
    };

    /// 单图对比切换提示只在最初三次进入该模式时显示。
    inline constexpr uint32_t kSingleImageCompareHintDisplayLimit = 3;

    /// 最近打开的文件按“最近优先”保存，避免菜单无限增长。
    inline constexpr size_t kMaximumRecentFileCount = 15;

    /// 自定义格式预设同样按最近保存优先；上限避免手工损坏的配置无限撑大下拉列表。
    inline constexpr size_t kMaximumImageFormatPresetCount = 64;

    /// 名称按 UTF-8 字节计数；128 字节足以容纳常用中文名称，同时约束配置文件大小。
    inline constexpr size_t kMaximumImageFormatPresetNameBytes = 128;

    /**
     * 读取上次主窗口的位置、大小与最大化状态。
     * 缺少任一必要字段或尺寸无效时返回 false。
     */
    bool TryGetWindowPlacement(FWindowPlacement& OutPlacement);

    /// 保存主窗口最后一次普通状态的边界与当前最大化状态。
    void SetWindowPlacement(const FWindowPlacement& Placement);

    /**
     * 上次浏览的目录。没有记录或记录已失效时返回空串。
     */
    const std::string& GetLastDirectory();

    /**
     * 记录上次浏览的目录并立即落盘。
     * 目录切换是低频操作，不值得为它引入延迟写或退出时统一保存的复杂度。
     */
    void SetLastDirectory(const std::string& Directory);

    /**
     * 最近成功打开的文件，按“最近优先”排列。
     */
    const std::vector<std::string>& GetRecentFiles();

    /**
     * 保存最近文件列表；空路径和重复项会被忽略，超过上限的旧项会被截断。
     */
    void SetRecentFiles(const std::vector<std::string>& Files);

    /**
     * 已保存的图像格式预设，按最近保存优先排列。
     */
    const std::vector<FImageFormatPreset>& GetImageFormatPresets();

    /**
     * 保存一个图像格式预设并立即落盘。
     *
     * 名称会去掉首尾 ASCII 空白；同名预设会被完整覆盖并移到列表首位。
     * 空名称、包含 ImGui 隐藏标签分隔符“##”的名称、无效 UTF-8、过长名称、
     * 无效参数或设置文件写入失败时返回 false。
     */
    bool SaveImageFormatPreset(const FImageFormatPreset& Preset);

    /**
     * 图片配置缓存容量。非法或缺失配置会回退到默认值。
     */
    size_t GetImageConfigCacheCapacity();

    /**
     * 保存图片配置缓存容量；超过上限的值会被约束。
     */
    void SetImageConfigCacheCapacity(size_t Capacity);

    /// 读取当前六项基础主题色。
    FThemePalette GetThemePalette();

    /// 保存六项基础主题色并立即落盘。
    void SetThemePalette(const FThemePalette& Palette);

    /// 读取当前主题高亮色的便捷接口。
    FThemeColor GetThemeAccentColor();

    /// 保存主题高亮色的兼容接口。
    void SetThemeAccentColor(const FThemeColor& Color);

    /// 直方图是否把 RGB 与亮度画在同一张图中。
    bool GetHistogramOverlayEnabled();

    /// 保存直方图叠加显示偏好并立即落盘。
    void SetHistogramOverlayEnabled(bool bEnabled);

    /// 直方图纵轴是否使用对数刻度。
    bool GetHistogramLogScaleEnabled();

    /// 保存直方图对数刻度偏好并立即落盘。
    void SetHistogramLogScaleEnabled(bool bEnabled);

    /// 最近一次由用户选择的“单图切换”或“平铺”模式。
    ECompareMode GetCompareMode();

    /// 保存对比模式并立即落盘；差值图不属于可保存类型。
    void SetCompareMode(ECompareMode Mode);

    /// 已展示过多少次“点击左键切换对比图”提示。
    uint32_t GetSingleImageCompareHintDisplayCount();

    /**
     * 尝试占用一次单图对比提示展示次数并立即落盘。
     * @return 尚未达到展示上限、调用方本次应显示提示时返回 true。
     */
    bool ConsumeSingleImageCompareHintDisplay();

    /**
     * 清除全部持久用户设置与本机图片属性缓存，并把当前进程内的设置恢复为默认值。
     * 保留不含用户数据的迁移标记，避免下次启动重新导入旧版设置。
     * @return 迁移已禁用且对应文件均已删除（原本不存在也视为成功）时返回 true。
     */
    bool ClearAllData();

    /**
     * 当前会话是否刚执行过“清除全部数据”，且此后没有产生新的持久设置。
     * 用于避免退出流程立即把窗口位置重新写回刚清空的设置文件。
     */
    bool WasAllDataClearedThisSession();

    /**
     * 图片属性缓存文件路径。
     *
     * 路径相关配置是本机数据，放在 %LOCALAPPDATA% 而不是漫游设置目录。
     */
    std::filesystem::path GetImageConfigCachePath();
}
