#include "FImageDocument.h"

#include "FAsyncImageLoader.h"
#include "FLocalization.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageLoader.h"
#include "gl/FTexture.h"
#include "FUserSettings.h"
#include "Util.h"

#include <filesystem>
#include <exception>

namespace
{
    bool IsRegularFile(const std::string& FilePath)
    {
        std::error_code ec;
        return !FilePath.empty() &&
            std::filesystem::is_regular_file(std::filesystem::u8path(FilePath), ec);
    }

    FImageLoadAttempt MakeAutomaticAttempt(const char* Source)
    {
        FImageLoadAttempt attempt;
        attempt.Mode = EImageLoadMode::Automatic;
        attempt.DiagnosticSource = Source ? Source : "automatic";
        return attempt;
    }

    FImageLoadAttempt MakeExplicitAttempt(
        const FImageLoadParams& Params,
        const char* Source)
    {
        FImageLoadAttempt attempt;
        attempt.Mode = EImageLoadMode::Explicit;
        attempt.Params = Params;
        attempt.DiagnosticSource = Source ? Source : "explicit";
        return attempt;
    }
}

FImageDocument::FImageDocument()
    : FileSize(0)
{
}

FImageDocument::~FImageDocument()
{
    Clear();
}

bool FImageDocument::Open(const std::string& InFilePath)
{
    if (!IsRegularFile(InFilePath))
    {
        LOGE("Open", "Not a regular file: %s", InFilePath.c_str());
        return false;
    }

    FImageLoadRequest request;
    request.Target = EImageLoadTarget::Main;
    request.FilePath = InFilePath;
    request.Attempts.push_back(MakeAutomaticAttempt("document automatic"));

    FImageLoadResult result =
        FAsyncImageLoader::DecodeOnCallingThread(std::move(request));

    if (result.HasDecodedImage())
    {
        FAsyncImageLoader::UploadTextureOnCallingThread(result);
    }

    return CommitLoadResult(std::move(result));
}

bool FImageDocument::OpenWithParams(const std::string& InFilePath, const FImageLoadParams& InParams)
{
    if (!IsRegularFile(InFilePath))
    {
        LOGE("OpenWithParams", "Not a regular file: %s", InFilePath.c_str());
        return false;
    }

    FImageLoadRequest request;
    request.Target = EImageLoadTarget::Main;
    request.FilePath = InFilePath;
    request.Attempts.push_back(
        MakeExplicitAttempt(InParams, "document explicit"));

    FImageLoadResult result =
        FAsyncImageLoader::DecodeOnCallingThread(std::move(request));

    if (result.HasDecodedImage())
    {
        FAsyncImageLoader::UploadTextureOnCallingThread(result);
    }

    return CommitLoadResult(std::move(result));
}

bool FImageDocument::Reload()
{
    if (FilePath.empty())
    {
        return false;
    }

    return OpenWithParams(FilePath, Params);
}

bool FImageDocument::CommitLoadResult(FImageLoadResult&& Result)
{
    FilePath = std::move(Result.FilePath);
    Params = Result.Params;
    FileSize = Result.FileSize;
    LastError = std::move(Result.LastError);

    const bool bPrepared =
        Result.HasDecodedImage() &&
        Result.TextureData &&
        Result.TextureData->IsValid();

    if (bPrepared)
    {
        // CPU 与 GPU 两份资源在主线程同一临界点替换；渲染回调在本帧稍后看到的
        // 要么全是旧资源，要么全是新资源，不会出现路径/像素/纹理跨代混搭。
        ImageData = std::move(Result.ImageData);
        TextureData = std::move(Result.TextureData);
        LastError.clear();
    }
    else
    {
        if (LastError.empty())
        {
            LastError = FLocalization::Text(EUiText::TextureCreationFailed);
        }

        LOGE("CommitLoadResult", "Failed to prepare image (%s)", LastError.c_str());
    }

    // 成功与失败都只通知一次。失败仍需把实际采用的参数与可读错误回填到属性面板。
    if (OnChanged)
    {
        OnChanged();
    }

    return bPrepared;
}

void FImageDocument::SetImageData(std::unique_ptr<FImageData> InImageData, const std::string& InLabel)
{
    if (!InImageData || !InImageData->IsValid())
    {
        return;
    }

    // 计算产物没有源文件，用标签占位；Reload() 对它没有意义
    FilePath = InLabel;
    FileSize = 0;
    LastError.clear();

    Params.SetDetectedFormat(InImageData->GetFormat());
    Params.Width = InImageData->GetWidth();
    Params.Height = InImageData->GetHeight();
    Params.Stride = 0;

    ImageData = std::move(InImageData);

    if (!UpdateTexture() && LastError.empty())
    {
        LastError = FLocalization::Text(EUiText::TextureCreationFailed);
    }

    if (OnChanged)
    {
        OnChanged();
    }
}

void FImageDocument::Clear()
{
    ImageData.reset();

    if (TextureData)
    {
        TextureData->Destroy();
        TextureData.reset();
    }

    FilePath.clear();
    FileSize = 0;
    LastError.clear();
}

void FImageDocument::TakeContentFrom(FImageDocument& Source)
{
    if (this == &Source)
    {
        return;
    }

    Clear();
    FilePath = std::move(Source.FilePath);
    Params = Source.Params;
    Display = Source.Display;
    ImageData = std::move(Source.ImageData);
    TextureData = std::move(Source.TextureData);
    FileSize = Source.FileSize;
    LastError = std::move(Source.LastError);
    Source.Clear();

    // 只转移内容，保留槽位对象及回调，避免面板借用指针与异步加载目标错位。
    LOGD("TakeDocumentContent", "%s", "Transferred image content between document slots");
}

bool FImageDocument::IsSelfDescribing() const
{
    return !FilePath.empty() && FImageLoaderFactory::IsSelfDescribingFile(FilePath);
}

uint64_t FImageDocument::GetImageSize() const
{
    if (IsSelfDescribing() || !Params.IsValid())
    {
        return 0;
    }

    return FImageFormatDesc::CalculateFrameSize(Params.Format, Params.Width, Params.Height, Params.Stride);
}

bool FImageDocument::SetParams(const FImageLoadParams& InParams)
{
    FImageLoadParams normalized = InParams;
    normalized.ConstrainStorageLayout();

    const bool bChanged =
        normalized.Format != Params.Format ||
        normalized.Width != Params.Width ||
        normalized.Height != Params.Height ||
        normalized.Stride != Params.Stride ||
        normalized.BitsPerPixel != Params.BitsPerPixel ||
        normalized.BayerPattern != Params.BayerPattern ||
        normalized.ByteOrder != Params.ByteOrder ||
        normalized.SampleAlignment != Params.SampleAlignment;

    Params = normalized;

    if (!bChanged || FilePath.empty())
    {
        return false;
    }

    return Reload();
}

bool FImageDocument::IsValid() const
{
    return ImageData && ImageData->IsValid() && TextureData && TextureData->IsValid();
}

bool FImageDocument::UpdateTexture()
{
    if (!ImageData || !ImageData->IsValid())
    {
        return false;
    }

    // 格式、尺寸与行跨距都没变时走 glTexSubImage2D，省一次纹理分配。
    // stride 必须一起比：它决定纹理上传时的 GL_UNPACK_ROW_LENGTH，而那是 Create() 时定死的
    const bool bCanReuse =
        TextureData &&
        TextureData->GetFormat() == ImageData->GetFormat() &&
        TextureData->GetWidth() == ImageData->GetWidth() &&
        TextureData->GetHeight() == ImageData->GetHeight() &&
        TextureData->GetStride() == ImageData->GetStride();

    if (bCanReuse && TextureData->UpdateFromImageData(ImageData.get()))
    {
        return true;
    }

    TextureData = std::make_unique<FTextureData>();

    FTextureCreateError textureError;
    if (!TextureData->CreateFromImageData(ImageData.get(), &textureError, FUserSettings::GetTextureLoadOptions()))
    {
        LastError = textureError.GetText();
        LOGE("UpdateTexture", "Failed to create texture, Format: %d, %dx%d",
             static_cast<int>(ImageData->GetFormat()), ImageData->GetWidth(), ImageData->GetHeight());

        TextureData.reset();
        return false;
    }

    return true;
}
