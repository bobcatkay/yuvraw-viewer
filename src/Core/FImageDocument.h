#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Image/FDisplaySettings.h"
#include "Image/FImageData.h"
#include "Image/FImageLoadParams.h"
#include "gl/FTextureData.h"

struct FImageLoadResult;

/**
 * 当前打开的图像"文档"
 *
 * 把 文件路径 + 加载参数 + 解码结果 + GPU 纹理 收拢成一个对象。
 *
 * 引入它之前，"取参数 -> 加载 -> 回填面板"这段逻辑在
 * FApplication::HandleFileDrop 和 FMainDockSpace::HandleFileSelection 里
 * 各写了一遍，且拖放路径漏掉了文件名解析，导致同一个文件"拖进来"和"点进来"行为不一致。
 *
 * 现在 UI 只做两件事：读文档的状态、提交参数变更。
 */
class FImageDocument
{
public:
    FImageDocument();
    ~FImageDocument();

    /**
     * 自动识别并打开文件。先清空上一张图的加载属性，再尝试从文件名解析分辨率与
     * 格式（如 xxx_1920x1080_nv21.yuv）并加载。曾经打开过的文件，以及经上层
     * 文件大小校验后可复用同目录属性的文件，通过 OpenWithParams 打开。
     * @return 是否加载成功
     */
    bool Open(const std::string& FilePath);

    /**
     * 用给定参数打开文件，**跳过文件名解析**。
     * 供命令行调用等"调用方已经知道确切参数"的场景使用。
     */
    bool OpenWithParams(const std::string& FilePath, const FImageLoadParams& InParams);

    /**
     * 用当前参数重新加载当前文件。参数变更后调用。
     */
    bool Reload();

    /**
     * 原子提交后台准备好的文件、参数、CPU 像素与 GPU 纹理。
     *
     * 成功时四者在同一主线程调用中一起替换并只触发一次 OnChanged；失败时保留上一幅
     * 可显示资源，但提交本次尝试的路径、参数与错误，允许用户继续在属性面板修正。
     */
    bool CommitLoadResult(FImageLoadResult&& Result);

    /**
     * 直接塞入一份现成的图像数据（不来自文件）。
     * 差值图这类计算产物走这条路径。
     */
    void SetImageData(std::unique_ptr<FImageData> InImageData, const std::string& InLabel);

    /**
     * 关闭当前文档并释放 GPU 资源
     */
    void Clear();

    /**
     * 接管另一固定槽位的完整内容并清空来源，不重新解码或上传纹理。
     * 两个槽位的 OnChanged 保持不变且不触发；调用方完成视图迁移后统一同步面板。
     */
    void TakeContentFrom(FImageDocument& Source);

    /**
     * 更新加载参数。若参数确实发生变化且已打开文件，会自动重新加载。
     * @return 是否触发了重新加载
     */
    bool SetParams(const FImageLoadParams& InParams);

    const FImageLoadParams& GetParams() const { return Params; }

    /**
     * 显示设置（色彩标准/范围/通道隔离）。只影响着色器 uniform，
     * 因此改动不会触发重新加载。每个文档一份，并排对比时两侧可以各自设置。
     */
    const FDisplaySettings& GetDisplaySettings() const { return Display; }
    void SetDisplaySettings(const FDisplaySettings& InDisplay) { Display = InDisplay; }

    /**
     * 当前文件是否自带文件头（PNG/JPEG/BMP/WebP…）
     *
     * 这类文件的格式与尺寸由文件本身描述，UI 应当把加载参数置为只读。
     */
    bool IsSelfDescribing() const;

    /**
     * 当前文件的字节数。未打开时为 0
     */
    uint64_t GetFileSize() const { return FileSize; }

    /**
     * 按当前参数算出的图像字节数。参数无效、或文件自带文件头时为 0
     *
     * 一个文件就是一幅图，所以这个值理应与文件大小相等 —— 不相等就是参数填错了，
     * 属性面板据此给出提示。
     *
     * 自带文件头的格式返回 0 是有意的：解码后的像素字节数和压缩文件大小没有可比性，
     * 拿去和文件大小对账只会给出误导性的"参数不对"提示。
     */
    uint64_t GetImageSize() const;

    /**
     * 上一次加载失败的原因（可直接显示给用户），成功时为空串
     *
     * 加载失败**也会**触发 OnChanged —— 否则用户在属性面板改了分辨率却什么都没发生，
     * 只会以为"改了不生效"。
     */
    const std::string& GetLastError() const { return LastError; }

    const FImageData* GetImageData() const { return ImageData.get(); }

    FTextureData* GetTextureData() const { return TextureData.get(); }

    const std::string& GetFilePath() const { return FilePath; }

    bool IsValid() const;

    /**
     * 内容变化（重新加载成功、参数变更）时的回调，供 UI 同步显示
     */
    void SetOnChanged(std::function<void()> Callback) { OnChanged = std::move(Callback); }

private:
    /**
     * 按当前参数把 ImageData 上传到 GPU。格式/尺寸不变时走 UpdateData，否则重建。
     */
    bool UpdateTexture();

    std::string FilePath;
    FImageLoadParams Params;
    FDisplaySettings Display;

    std::unique_ptr<FImageData> ImageData;
    std::unique_ptr<FTextureData> TextureData;

    uint64_t FileSize;

    std::string LastError;

    std::function<void()> OnChanged;
};
