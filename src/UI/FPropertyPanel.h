#pragma once

#include <memory>
#include <string>
#include <functional>
#include <cstdint>
#include <vector>
#include "Image/FDisplaySettings.h"
#include "Image/FImageData.h"
#include "Image/FImageLoadParams.h"
#include "Image/FResolutionGuess.h"

/**
 * 属性面板当前在编辑哪一幅图
 *
 * 主图与对比图各有一套加载参数和显示设置，面板同一时刻只编辑其中一套。
 */
enum class EPropertyTarget
{
    Main,
    Compare,
};

/**
 * 属性面板类
 * 负责显示和编辑图像属性
 */
class FPropertyPanel
{
public:
    FPropertyPanel();
    ~FPropertyPanel();

    /**
     * 渲染属性面板
     */
    void Render();

    /**
     * 当前编辑目标。没有对比图时恒为 Main，下拉框也不会出现。
     */
    EPropertyTarget GetTarget() const { return Target; }

    /**
     * 直接切换编辑目标，**不触发** OnTargetChanged。
     * 供调用方在"对比图被移除"这类自己已经处理过后续同步的场景使用。
     */
    void SetTarget(EPropertyTarget InTarget) { Target = InTarget; }

    /**
     * 是否同时存在主图与对比图。仅有一张图时隐藏目标下拉框。
     */
    void SetTargetSelectionAvailable(bool bAvailable) { bTargetSelectionAvailable = bAvailable; }

    /**
     * 编辑目标改变时触发，调用方需要把新目标的参数回填进来
     */
    void SetOnTargetChanged(std::function<void(EPropertyTarget)> Callback);

    /**
     * 显示设置（色彩标准/范围/通道）改变时触发
     */
    void SetOnDisplaySettingsChanged(std::function<void(const FDisplaySettings&)> Callback);

    const FDisplaySettings& GetDisplaySettings() const { return SelectedDisplay; }
    void SetDisplaySettings(const FDisplaySettings& InDisplay) { SelectedDisplay = InDisplay; }

    /**
     * 设置当前图像数据
     */
    void SetImageData(const FImageData* ImageData);

    /**
     * 一次性回填完整加载参数。切换主图/对比图时必须使用这一入口，
     * 避免 Bayer 排布、位深或存储布局残留为上一个编辑对象的值。
     */
    void SetLoadParams(const FImageLoadParams& Params);

    /**
     * 加载参数是否可编辑
     *
     * PNG/JPEG/BMP/WebP 这类自带文件头的格式，格式与尺寸由文件本身描述，
     * 面板只展示不允许改 —— 改了也只会让加载失败。
     */
    void SetParamsEditable(bool bEditable) { bParamsEditable = bEditable; }

    /**
     * 上一次加载失败的原因，空串表示没有失败
     */
    void SetLoadError(const std::string& Error) { LoadError = Error; }

    /**
     * 文件字节数，以及按当前参数算出的图像字节数
     *
     * 猜无头格式参数时最有用的一条线索：一个文件就是一幅图，两者不相等
     * 基本可以断定格式/分辨率/stride 填错了。InImageSize 传 0 表示不适用。
     */
    void SetFileInfo(uint64_t InFileSize, uint64_t InImageSize);

    /**
     * 当前文件路径
     *
     * 面板自己从文件名里解析一次分辨率，好把它作为一条**候选**与由文件大小算出来的
     * 那些并列展示。文档层已经把文件名解析的结果写进过参数，但用户随后可能改过，
     * 那时就没别的地方能回答"文件名当初说的是多少"了。
     */
    void SetSourceFile(const std::string& InPath);

    /**
     * 获取当前选择的格式
     */
    EImageFormat GetSelectedFormat() const { return SelectedFormat; }

    /**
     * 获取当前选择的宽度
     */
    int32_t GetSelectedWidth() const { return SelectedWidth; }

    /**
     * 获取当前选择的高度
     */
    int32_t GetSelectedHeight() const { return SelectedHeight; }

    /**
     * 获取当前选择的每像素位数
     */
    int32_t GetSelectedBitsPerPixel() const { return SelectedBitsPerPixel; }

    /**
     * 设置格式改变回调
     */
    void SetOnFormatChanged(std::function<void(EImageFormat)> Callback);

    /**
     * 设置分辨率改变回调
     */
    void SetOnResolutionChanged(std::function<void(int32_t, int32_t)> Callback);

    /**
     * 设置位深改变回调
     */
    void SetOnBitsPerPixelChanged(std::function<void(int32_t)> Callback);

    /**
     * 设置 stride 改变回调
     */
    void SetOnStrideChanged(std::function<void(int32_t)> Callback);

    /**
     * 设置 Bayer 排布改变回调
     */
    void SetOnBayerPatternChanged(std::function<void(EBayerPattern)> Callback);

    /**
     * 设置多字节采样字节序改变回调
     */
    void SetOnByteOrderChanged(std::function<void(EByteOrder)> Callback);

    /**
     * 设置有效位对齐改变回调
     */
    void SetOnSampleAlignmentChanged(
        std::function<void(ESampleAlignment)> Callback);

    /**
     * 用户从“图像格式”下拉框选择自定义预设时触发。
     *
     * 预设会一次性改动完整加载参数，单独回调可避免逐字段通知造成多次异步重载。
     */
    void SetOnFormatPresetApplied(std::function<void()> Callback);

    /**
     * 获取当前选择的 stride（第 0 平面每行字节数）
     */
    int32_t GetSelectedStride() const { return SelectedStride; }

    /**
     * 设置 stride（每行字节数）
     *
     * 传 0（文档里表示"紧凑排列"）时面板显示按当前格式与宽度算出的具体字节数，
     * 而不是把 0 摆给用户看。
     */
    void SetStride(int32_t Stride);

    /**
     * 获取当前选择的字节序
     */
    EByteOrder GetByteOrder() const { return SelectedByteOrder; }

    /**
     * 获取当前选择的有效位对齐
     */
    ESampleAlignment GetSampleAlignment() const
    {
        return SelectedSampleAlignment;
    }

    /**
     * 获取当前的加载参数
     */
    FImageLoadParams GetLoadParams() const;

    /**
     * 设置格式
     */
    void SetFormat(EImageFormat Format);

    /**
     * 设置分辨率
     */
    void SetResolution(int32_t Width, int32_t Height);

    /**
     * 设置位深
     */
    void SetBitsPerPixel(int32_t BitsPerPixel);

private:
    void RenderTargetSelector();
    void RenderImageInfo();

    /**
     * 文件大小 / 按参数算出的图像大小，以及两者"对不上"时的提示
     */
    void RenderFileInfo();
    void RenderFormatSelector();
    void RenderFormatPresetPopup();
    void RenderResolutionEditor();

    /**
     * 分辨率输入框右侧的候选下拉框
     *
     * 文件名解析结果与"由文件大小反推"的结果在这里合成同一个列表 —— 它们不是
     * 互相竞争的两套机制，谁也不覆盖谁，由用户点一下决定。
     */
    void RenderResolutionGuess();

    /**
     * 按 (格式, 文件大小, 文件路径) 缓存候选
     *
     * ImGui 是立即模式，每帧都会走到渲染函数；因数分解虽然只有毫秒级，
     * 也没有理由每帧算一遍。
     */
    void RefreshCandidates();

    /**
     * 应用一条候选：写入宽高并把 stride 复位成紧凑值，然后触发一次重新加载
     */
    void ApplyCandidate(int32_t Width, int32_t Height);

    /**
     * 统一应用用户选择的格式，并同步位深、stride 与存储布局默认值。
     */
    void ApplyFormatSelection(EImageFormat NewFormat);

    void RenderStrideEditor();
    void RenderBayerPatternSelector();
    void RenderBitsPerPixelSelector();
    void RenderDisplaySettings();

    /**
     * 三原色 / 传输函数 / 色调映射 / 曝光 / 超范围高亮
     *
     * 与"色彩标准"分开成一节：那个只管 YCbCr 矩阵，这里的三项才是 HDR 素材真正要填的。
     */
    void RenderTransferSettings();

    /// HDR 呈现层的当前状态（只读），含未启用时的原因
    void RenderHdrOutputStatus();

    void RenderStorageLayoutSelectors();
    void RenderByteOrderSelector();
    void RenderSampleAlignmentSelector();

    /**
     * 按当前格式与位深约束存储布局，固定/不适用属性回到格式默认值。
     */
    void ConstrainStorageLayout();

    void NotifyDisplaySettingsChanged();

    /**
     * 指定格式与宽度下紧凑排列的每行字节数
     */
    int32_t GetCompactStride(EImageFormat Format, int32_t Width) const;

    /**
     * 格式或宽度改变后重算 stride
     *
     * 只在用户没手填过 padding（当前值仍等于旧格式/旧宽度的紧凑值）时才覆盖，
     * 否则保留用户填的值。
     */
    void RefreshStrideForGeometry(EImageFormat OldFormat, int32_t OldWidth);

    /**
     * 无加减按钮的整数输入框，失去焦点时提交
     * @return 值发生变化时返回 true
     */
    bool DrawIntInput(const char* Label, int32_t& InOutValue, int32_t MinValue, int32_t MaxValue);

    const FImageData* CurrentImageData;

    EImageFormat SelectedFormat;
    int32_t SelectedWidth;
    int32_t SelectedHeight;
    int32_t SelectedStride;   ///< 第 0 平面每行字节数，回填时总是具体值（默认 = 紧凑排列的字节数）
    int32_t SelectedBitsPerPixel;
    EBayerPattern SelectedBayerPattern;
    EByteOrder SelectedByteOrder;
    ESampleAlignment SelectedSampleAlignment;

    FDisplaySettings SelectedDisplay;

    EPropertyTarget Target;
    bool bTargetSelectionAvailable;

    /// 自带文件头的格式为 false，此时格式/分辨率/stride/位深等控件置灰
    bool bParamsEditable;

    std::string LoadError;

    uint64_t FileSize;
    uint64_t ImageSize;

    std::string SourceFile;

    // --- 自定义图像格式预设 ---

    /// 最近一次显式选择或保存的预设；当前参数不再匹配时仅回退显示内建格式名。
    std::string SelectedFormatPresetName;

    /// 弹窗打开时拍下完整参数，保证名称编辑期间预设内容保持不变。
    FImageLoadParams FormatPresetDraftParams;

    bool bRequestFormatPresetPopup;
    bool bFocusFormatPresetNameInput;
    char FormatPresetNameInput[256];
    std::string FormatPresetSaveError;

    // --- 分辨率候选（缓存，key 见 RefreshCandidates） ---

    EImageFormat CachedFormat;
    uint64_t CachedFileSize;
    std::string CachedSourceFile;

    /// 首项在文件名解析出分辨率时恒为该条（bFromFilename = true），其余按可信度排序
    std::vector<FResolutionCandidate> Candidates;

    /// 文件名里的分辨率，0 表示没解析到
    int32_t FilenameWidth;
    int32_t FilenameHeight;

    /// 文件名里的分辨率在当前格式下能整除文件大小时为 true。没解析到分辨率时无意义
    bool bFilenameExact;

    /// 文件名里的分辨率对不上时，在这些格式下反而能整除 —— 多半是格式选错了
    std::vector<EImageFormat> AltFormats;

    std::function<void(EImageFormat)> OnFormatChanged;
    std::function<void(int32_t, int32_t)> OnResolutionChanged;
    std::function<void(int32_t)> OnBitsPerPixelChanged;
    std::function<void(int32_t)> OnStrideChanged;
    std::function<void(EBayerPattern)> OnBayerPatternChanged;
    std::function<void(EByteOrder)> OnByteOrderChanged;
    std::function<void(ESampleAlignment)> OnSampleAlignmentChanged;
    std::function<void()> OnFormatPresetApplied;
    std::function<void(EPropertyTarget)> OnTargetChanged;
    std::function<void(const FDisplaySettings&)> OnDisplaySettingsChanged;
};
