#pragma once

#include "FColorTransform.h"

/** Bayer 线性校正：CPU 与 GLSL 使用相同的黑电平、白平衡、矩阵和输出编码。 */
namespace FRawColorTransform
{
    struct FContext
    {
        bool bEnabled = false;
        bool bEncodeSrgb = false;
        std::array<float, FRawDisplaySettings::kCfaChannelCount> BlackLevel{};
        std::array<float, FRawDisplaySettings::kCfaChannelCount> Scale{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, FRawDisplaySettings::kRgbChannelCount> WhiteBalance{ 1.0f, 1.0f, 1.0f };
        // 始终展开为 OpenGL 所需的列主序，CPU 和 GPU 不再各自解释排列方式。
        std::array<float, FRawDisplaySettings::kMatrixElementCount> Matrix{
            1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    };

    inline FContext Build(const FRawDisplaySettings& Settings, int32_t MaxValue)
    {
        FContext ctx;
        if (!Settings.bEnabled || !Settings.IsValid()) { return ctx; }
        ctx.bEnabled = true;
        ctx.bEncodeSrgb = Settings.bEncodeSrgb;
        ctx.WhiteBalance = Settings.WhiteBalance;
        const float maximum = static_cast<float>(MaxValue);
        const float white = Settings.WhiteLevel > 0.0f ? Settings.WhiteLevel : maximum;
        for (int32_t i = 0; i < FRawDisplaySettings::kCfaChannelCount; ++i)
        {
            // 黑电平在传感器码值域扣除，再按实际白电平归一化；避免低位深时除零。
            const float black = std::min(Settings.BlackLevel[i], std::max(0.0f, white - 1.0f));
            ctx.BlackLevel[i] = black / maximum;
            ctx.Scale[i] = maximum / std::max(white - black, 1.0f);
        }
        if (Settings.bApplyCcm)
        {
            for (int32_t row = 0; row < FRawDisplaySettings::kRgbChannelCount; ++row)
            {
                for (int32_t col = 0; col < FRawDisplaySettings::kRgbChannelCount; ++col)
                {
                    const int32_t outputIndex = col * FRawDisplaySettings::kRgbChannelCount + row;
                    const int32_t inputIndex = Settings.CcmLayout == ERawCcmLayout::RowMajor
                        ? row * FRawDisplaySettings::kRgbChannelCount + col : outputIndex;
                    ctx.Matrix[outputIndex] = Settings.Ccm[inputIndex];
                }
            }
            if (Settings.CcmOutput == ERawCcmOutput::XyzD65)
            {
                // XYZ 的白点必须为 D65。其它白点需要厂商提供适配矩阵，不在这里猜测。
                float srgbToXyz[FRawDisplaySettings::kMatrixElementCount];
                float xyzToSrgb[FRawDisplaySettings::kMatrixElementCount];
                FColorTransform::BuildRgbToXyz(EColorPrimaries::BT709, srgbToXyz);
                FColorTransform::Invert3x3(srgbToXyz, xyzToSrgb);
                const auto sensorToXyz = ctx.Matrix;
                FColorTransform::Multiply3x3(xyzToSrgb, sensorToXyz.data(), ctx.Matrix.data());
            }
        }
        return ctx;
    }

    inline float Normalize(const FContext& Ctx, float Level, int32_t X, int32_t Y)
    {
        if (!Ctx.bEnabled) { return Level; }
        const int32_t index = (Y & 1) * 2 + (X & 1);
        return std::max(Level - Ctx.BlackLevel[index], 0.0f) * Ctx.Scale[index];
    }

    inline void ApplyMatrix(const FContext& Ctx, float Rgb[FRawDisplaySettings::kRgbChannelCount])
    {
        if (!Ctx.bEnabled) { return; }
        const std::array<float, FRawDisplaySettings::kRgbChannelCount> balanced{
            Rgb[0] * Ctx.WhiteBalance[0], Rgb[1] * Ctx.WhiteBalance[1], Rgb[2] * Ctx.WhiteBalance[2] };
        for (int32_t row = 0; row < FRawDisplaySettings::kRgbChannelCount; ++row)
        {
            Rgb[row] = 0.0f;
            for (int32_t col = 0; col < FRawDisplaySettings::kRgbChannelCount; ++col)
            {
                Rgb[row] += Ctx.Matrix[col * FRawDisplaySettings::kRgbChannelCount + row] * balanced[col];
            }
        }
    }

    inline float Encode(const FContext& Ctx, float Level)
    {
        const float clipped = std::clamp(Level, 0.0f, 1.0f);
        return Ctx.bEnabled && Ctx.bEncodeSrgb ? FColorTransform::SrgbEncode(clipped) : clipped;
    }
}
