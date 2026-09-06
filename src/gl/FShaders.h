#pragma once

#include "Image/FImageFormat.h"
#include "Image/FImageFormatDesc.h"
#include <string>

/**
 * 着色器代码定义
 * 集中管理所有格式的着色器代码，方便扩展
 *
 * 约定：
 *  - YUV 类格式共用同一份着色器，色彩标准/数值范围/位深/UV 顺序全部通过 uniform 传入，
 *    不为每种组合生成一份源码（否则 3 标准 x 2 范围 x N 格式会爆炸）
 *  - 着色器按 EImageFormat 缓存（FShaderManager），同一格式只编译一次
 *  - 片段着色器 = 版本声明 + 色彩管线前导块 + 各格式的取样代码，见 GetColorPipelineGLSL
 */
namespace FShaders
{
    /**
     * 通用顶点着色器（所有格式共享）
     */
    inline const char* GetDefaultVertexShader()
    {
        return R"(
            #version 330 core
            layout (location = 0) in vec2 aPos;
            layout (location = 1) in vec2 aTexCoord;

            uniform mat4 uProjection;
            uniform mat4 uTransform;

            out vec2 TexCoord;

            void main()
            {
                gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
                TexCoord = aTexCoord;
            }
        )";
    }

    /**
     * 色彩管线前导块（所有需要出彩色的片段着色器共享）
     *
     * **这段 GLSL 与 Image/FColorTransform.h 的 ApplyPipeline 逐步一一对应**，
     * 常数与分支顺序必须一致 —— 否则屏幕上看到的和导出/直方图/像素探针给出的会分叉，
     * 而这种分叉极难察觉。改任何一边都要同步改另一边。
     *
     * uTransfer 与 uToneMap 直接用 EColorTransfer / EToneMapOperator 的枚举值：
     *   uTransfer  0=SDR(sRGB) 1=BT1886 2=PQ 3=HLG 4=Linear
     *   uToneMap   0=Clip 1=Reinhard 2=ACES
     *
     * uOutputMode 为 1 时目标是 HDR 帧缓冲，其约定是
     * **扩展 sRGB 编码、1.0 = 显示器 SDR 白电平**（见 Core/FHdrPresenter.h）。
     * 此时不做 SDR 色调映射，只按显示器峰值**保色相**限幅（LimitToPeakPreserveHue），
     * 且**保留负分量** —— 那是广色域的表达方式。
     */
    inline const char* GetColorPipelineGLSL()
    {
        return R"(
            uniform int   uPipelineEnabled;   // 0 时整条链路恒等，只做裁剪
            uniform int   uTransfer;
            uniform int   uAbsoluteTransfer;  // PQ/HLG 解出来的是绝对 cd/m^2
            uniform int   uApplyPrimaries;
            uniform mat3  uPrimariesMatrix;   // 列主序，线性域
            uniform vec3  uLumaCoef;          // 源原色下的亮度系数，HLG OOTF 用
            uniform float uNormalizeNits;
            uniform float uHlgPeakNits;
            uniform float uExposureScale;
            uniform int   uToneMap;
            uniform float uToneMapWhite;
            uniform int   uOutputMode;        // 0=SDR 1=HDR(scRGB)
            uniform float uMaxOutputScale;
            uniform int   uShowOutOfRange;

            float SrgbEncode1(float L)
            {
                return (L <= 0.0031308) ? (12.92 * L) : (1.055 * pow(L, 1.0 / 2.4) - 0.055);
            }

            vec3 SrgbEncode(vec3 c)
            {
                return vec3(SrgbEncode1(c.r), SrgbEncode1(c.g), SrgbEncode1(c.b));
            }

            float SrgbDecode1(float V)
            {
                return (V <= 0.04045) ? (V / 12.92) : pow((V + 0.055) / 1.055, 2.4);
            }

            vec3 SrgbDecode(vec3 c)
            {
                return vec3(SrgbDecode1(c.r), SrgbDecode1(c.g), SrgbDecode1(c.b));
            }

            // 保号的扩展 sRGB 编码，可超出 [0,1]，也可为负
            vec3 SrgbEncodeExtended(vec3 c)
            {
                return sign(c) * SrgbEncode(abs(c));
            }

            // SMPTE ST 2084 (PQ) EOTF：码值 -> cd/m^2
            vec3 PqEotfNits(vec3 V)
            {
                const float m1 = 0.1593017578125;
                const float m2 = 78.84375;
                const float c1 = 0.8359375;
                const float c2 = 18.8515625;
                const float c3 = 18.6875;

                vec3 p   = pow(max(V, vec3(0.0)), vec3(1.0 / m2));
                vec3 num = max(p - c1, vec3(0.0));
                vec3 den = max(c2 - c3 * p, vec3(1e-6));

                return 10000.0 * pow(num / den, vec3(1.0 / m1));
            }

            // HLG OETF 的逆：码值 -> 场景线性
            vec3 HlgSceneLinear(vec3 V)
            {
                const float a = 0.17883277;
                const float b = 0.28466892;
                const float c = 0.55991073;

                vec3 v  = max(V, vec3(0.0));
                vec3 lo = (v * v) / 3.0;
                vec3 hi = (exp((v - c) / a) + b) / 12.0;

                return mix(lo, hi, step(vec3(0.5), v));
            }

            // HLG OOTF：场景线性 -> 显示线性 cd/m^2，三分量共用一个缩放
            vec3 ApplyHlgOotf(vec3 scene)
            {
                float lw    = max(uHlgPeakNits, 1.0);
                float gamma = clamp(1.2 + 0.42 * log(lw / 1000.0) / log(10.0), 1.0, 2.0);

                // pow(0, 0) 在 GLSL 里未定义，ys 必须先抬离 0
                float ys = max(dot(uLumaCoef, scene), 1e-6);

                return scene * (lw * pow(ys, gamma - 1.0));
            }

            // 保色相地把线性值限制到上限。与 FColorTransform::LimitToPeakPreserveHue 对应：
            // 逐通道 min() 会让饱和高光偏色（(6,2,1) 在上限 1.86 下裁成 (1.86,2,1)，
            // 红反而低于绿），所以按最大分量整体缩放 —— 只压亮度，色度不变。
            // 负分量随之等比缩放而不被抬起，那是 scRGB 表达色域外颜色的方式
            vec3 LimitToPeakPreserveHue(vec3 c, float upperLimit)
            {
                float peak = max(max(c.r, c.g), c.b);

                // upperLimit >= 1，所以 peak > upperLimit 时必有 peak > 0，除法安全
                return (peak > upperLimit) ? (c * (upperLimit / peak)) : c;
            }

            float ToneMapChannel(float x)
            {
                if (uToneMap == 1)
                {
                    float w = max(uToneMapWhite, 1e-4);

                    return x * (1.0 + x / (w * w)) / (1.0 + x);
                }

                if (uToneMap == 2)
                {
                    const float a = 2.51;
                    const float b = 0.03;
                    const float c = 2.43;
                    const float d = 0.59;
                    const float e = 0.14;

                    return (x * (a * x + b)) / (x * (c * x + d) + e);
                }

                return x;
            }

            vec3 ApplyColorPipeline(vec3 rgb)
            {
                if (uPipelineEnabled == 0)
                {
                    // 直通：非线性 R'G'B' 本来就是显示编码值。
                    // 此时"超范围"就是超出编码值域 [0,1] —— limited range 的超白/超黑
                    // 经矩阵拉伸后正落在这里，判定必须在裁剪之前
                    if (uShowOutOfRange != 0)
                    {
                        if (any(greaterThan(rgb, vec3(1.0 + 1e-4)))) { return vec3(1.0, 0.0, 0.0); }
                        if (any(lessThan(rgb, vec3(-1e-4))))         { return vec3(0.0, 0.4, 1.0); }
                    }

                    return clamp(rgb, 0.0, 1.0);
                }

                vec3 lin;

                if (uTransfer == 2)      { lin = PqEotfNits(rgb); }
                else if (uTransfer == 3) { lin = ApplyHlgOotf(HlgSceneLinear(rgb)); }
                else if (uTransfer == 1) { lin = pow(max(rgb, vec3(0.0)), vec3(2.4)); }
                else if (uTransfer == 4) { lin = rgb; }
                else                     { lin = SrgbDecode(rgb); }

                if (uAbsoluteTransfer != 0)
                {
                    lin /= uNormalizeNits;
                }

                if (uApplyPrimaries != 0)
                {
                    lin = uPrimariesMatrix * lin;
                }

                lin *= uExposureScale;

                float upperLimit = (uOutputMode != 0) ? uMaxOutputScale : 1.0;

                if (uShowOutOfRange != 0)
                {
                    // 超出显示上限画红，落在目标色域外（负分量）画蓝
                    if (any(greaterThan(lin, vec3(upperLimit + 1e-4)))) { return vec3(1.0, 0.0, 0.0); }
                    if (any(lessThan(lin, vec3(-1e-4))))                { return vec3(0.0, 0.4, 1.0); }
                }

                if (uOutputMode != 0)
                {
                    return SrgbEncodeExtended(LimitToPeakPreserveHue(lin, upperLimit));
                }

                vec3 mapped = vec3(ToneMapChannel(max(lin.r, 0.0)),
                                   ToneMapChannel(max(lin.g, 0.0)),
                                   ToneMapChannel(max(lin.b, 0.0)));

                return SrgbEncode(clamp(mapped, 0.0, 1.0));
            }
        )";
    }

    /**
     * 半平面 YUV（NV12 / NV21 / P010 / YUV420SP16）：Y 一张纹理，UV 交织在第二张纹理
     */
    inline const char* GetSemiPlanarYUVBody()
    {
        return R"(
            out vec4 FragColor;

            in vec2 TexCoord;

            uniform sampler2D uTextureY;
            uniform sampler2D uTextureUV;

            uniform mat3  uYuvToRgb;     // YUV -> RGB 矩阵（含 limited range 拉伸）
            uniform vec3  uYuvOffset;    // 减去的偏移：limited 8bit 为 (16/255, 128/255, 128/255)
            uniform float uSampleScale;  // 有效位未占满 16bit 容器时补偿归一化范围
            uniform int   uSwapUV;       // NV21 存储顺序是 V,U，此时为 1
            uniform int   uChannelMode;  // 0=彩色 1=仅Y 2=仅U 3=仅V

            void main()
            {
                float y  = texture(uTextureY,  TexCoord).r  * uSampleScale;
                vec2  uvSample = texture(uTextureUV, TexCoord).rg * uSampleScale;

                float u = (uSwapUV != 0) ? uvSample.y : uvSample.x;
                float v = (uSwapUV != 0) ? uvSample.x : uvSample.y;

                // 通道隔离看的是**原始码值**，刻意不走色彩管线：
                // 这条路径回答的是"这个平面里到底存了什么"，套上 EOTF 反而看不出来了
                if (uChannelMode == 1)
                {
                    // 仅看 Y：按 limited range 拉伸后以灰度呈现
                    float luma = (y - uYuvOffset.x) * uYuvToRgb[0][0];
                    FragColor = vec4(vec3(clamp(luma, 0.0, 1.0)), 1.0);
                    return;
                }
                else if (uChannelMode == 2)
                {
                    FragColor = vec4(vec3(clamp(u, 0.0, 1.0)), 1.0);
                    return;
                }
                else if (uChannelMode == 3)
                {
                    FragColor = vec4(vec3(clamp(v, 0.0, 1.0)), 1.0);
                    return;
                }

                vec3 rgb = uYuvToRgb * (vec3(y, u, v) - uYuvOffset);
                FragColor = vec4(ApplyColorPipeline(rgb), 1.0);
            }
        )";
    }

    /**
     * 平面 YUV（I420 / YV12 / YUV422P / YUV444P）：Y/U/V 各一张纹理
     */
    inline const char* GetPlanarYUVBody()
    {
        return R"(
            out vec4 FragColor;

            in vec2 TexCoord;

            uniform sampler2D uTextureY;
            uniform sampler2D uTextureU;
            uniform sampler2D uTextureV;

            uniform mat3  uYuvToRgb;
            uniform vec3  uYuvOffset;
            uniform float uSampleScale;
            uniform int   uChannelMode;

            void main()
            {
                float y = texture(uTextureY, TexCoord).r * uSampleScale;
                float u = texture(uTextureU, TexCoord).r * uSampleScale;
                float v = texture(uTextureV, TexCoord).r * uSampleScale;

                if (uChannelMode == 1)
                {
                    float luma = (y - uYuvOffset.x) * uYuvToRgb[0][0];
                    FragColor = vec4(vec3(clamp(luma, 0.0, 1.0)), 1.0);
                    return;
                }
                else if (uChannelMode == 2)
                {
                    FragColor = vec4(vec3(clamp(u, 0.0, 1.0)), 1.0);
                    return;
                }
                else if (uChannelMode == 3)
                {
                    FragColor = vec4(vec3(clamp(v, 0.0, 1.0)), 1.0);
                    return;
                }

                vec3 rgb = uYuvToRgb * (vec3(y, u, v) - uYuvOffset);
                FragColor = vec4(ApplyColorPipeline(rgb), 1.0);
            }
        )";
    }

    /**
     * packed 4:2:2（YUY2 / UYVY）
     *
     * 一个纹素打包 2 个像素共 4 字节，因此纹理宽度是图像宽度的一半、RGBA8 格式。
     * 必须用 texelFetch 按整数坐标取样 —— 线性过滤会把 Y0/U/Y1/V 混在一起。
     */
    inline const char* GetPackedYUVBody()
    {
        return R"(
            out vec4 FragColor;

            in vec2 TexCoord;

            uniform sampler2D uTexture;

            uniform mat3  uYuvToRgb;
            uniform vec3  uYuvOffset;
            uniform vec2  uImageSize;
            uniform int   uSwapUV;       // UYVY 为 1
            uniform int   uChannelMode;

            void main()
            {
                ivec2 pixel = ivec2(TexCoord * uImageSize);
                pixel = clamp(pixel, ivec2(0), ivec2(uImageSize) - ivec2(1));

                // 两个相邻像素共用一个纹素
                vec4 texel = texelFetch(uTexture, ivec2(pixel.x >> 1, pixel.y), 0);

                bool bOddPixel = (pixel.x & 1) == 1;

                float y, u, v;

                if (uSwapUV != 0)
                {
                    // UYVY: [U Y0 V Y1]
                    u = texel.r;
                    v = texel.b;
                    y = bOddPixel ? texel.a : texel.g;
                }
                else
                {
                    // YUY2: [Y0 U Y1 V]
                    u = texel.g;
                    v = texel.a;
                    y = bOddPixel ? texel.b : texel.r;
                }

                if (uChannelMode == 1)
                {
                    float luma = (y - uYuvOffset.x) * uYuvToRgb[0][0];
                    FragColor = vec4(vec3(clamp(luma, 0.0, 1.0)), 1.0);
                    return;
                }
                else if (uChannelMode == 2)
                {
                    FragColor = vec4(vec3(clamp(u, 0.0, 1.0)), 1.0);
                    return;
                }
                else if (uChannelMode == 3)
                {
                    FragColor = vec4(vec3(clamp(v, 0.0, 1.0)), 1.0);
                    return;
                }

                vec3 rgb = uYuvToRgb * (vec3(y, u, v) - uYuvOffset);
                FragColor = vec4(ApplyColorPipeline(rgb), 1.0);
            }
        )";
    }

    /**
     * Bayer CFA 去马赛克（双线性）
     *
     * 工具场景下正确性优先于画质：双线性会在高频边缘产生拉链纹，
     * 但能如实反映 CFA 排布是否选对 —— 选错 pattern 时颜色会明显翻车，这正是我们想看到的。
     *
     * **刻意不接色彩管线**：CFA 是传感器出来的原始读数，本来就在线性域，
     * 上面既没有 gamma 也没有 PQ，套一层 EOTF 只会得到一个没有物理含义的结果。
     * 想看 RAW 的亮度关系请用曝光/直方图，不要用传输函数。
     *
     * uBayerPattern: 0=RGGB 1=BGGR 2=GRBG 3=GBRG
     */
    inline const char* GetBayerShader()
    {
        return R"(
            #version 330 core
            out vec4 FragColor;

            in vec2 TexCoord;

            uniform sampler2D uTexture;
            uniform vec2  uImageSize;
            uniform float uSampleScale;   // 16bit 容器里装 10/12bit 时把值拉回满量程
            uniform int   uBayerPattern;
            uniform int   uChannelMode;   // 0=彩色 1/2/3=R/G/B

            // 带边界钳制的整数取样
            float Fetch(ivec2 coord)
            {
                ivec2 maxCoord = ivec2(uImageSize) - ivec2(1);
                coord = clamp(coord, ivec2(0), maxCoord);
                return texelFetch(uTexture, coord, 0).r * uSampleScale;
            }

            void main()
            {
                ivec2 p = ivec2(TexCoord * uImageSize);
                p = clamp(p, ivec2(0), ivec2(uImageSize) - ivec2(1));

                // 各排布下红色滤片在 2x2 中的位置
                ivec2 redOrigin;

                if      (uBayerPattern == 0) { redOrigin = ivec2(0, 0); }  // RGGB
                else if (uBayerPattern == 1) { redOrigin = ivec2(1, 1); }  // BGGR
                else if (uBayerPattern == 2) { redOrigin = ivec2(1, 0); }  // GRBG
                else                         { redOrigin = ivec2(0, 1); }  // GBRG

                int dx = (p.x - redOrigin.x) & 1;
                int dy = (p.y - redOrigin.y) & 1;

                float center = Fetch(p);

                // 四邻域与四对角
                float left  = Fetch(p + ivec2(-1,  0));
                float right = Fetch(p + ivec2( 1,  0));
                float up    = Fetch(p + ivec2( 0, -1));
                float down  = Fetch(p + ivec2( 0,  1));

                float horizontal = (left + right) * 0.5;
                float vertical   = (up + down) * 0.5;
                float cross4     = (left + right + up + down) * 0.25;
                float diagonal4  = (Fetch(p + ivec2(-1, -1)) + Fetch(p + ivec2(1, -1)) +
                                    Fetch(p + ivec2(-1,  1)) + Fetch(p + ivec2(1,  1))) * 0.25;

                vec3 rgb;

                if (dx == 0 && dy == 0)
                {
                    // 红点：绿取四邻域，蓝取四对角
                    rgb = vec3(center, cross4, diagonal4);
                }
                else if (dx == 1 && dy == 1)
                {
                    // 蓝点：绿取四邻域，红取四对角
                    rgb = vec3(diagonal4, cross4, center);
                }
                else if (dy == 0)
                {
                    // 绿点，位于红行：水平邻居是红，垂直邻居是蓝
                    rgb = vec3(horizontal, center, vertical);
                }
                else
                {
                    // 绿点，位于蓝行：水平邻居是蓝，垂直邻居是红
                    rgb = vec3(vertical, center, horizontal);
                }

                rgb = clamp(rgb, 0.0, 1.0);

                if      (uChannelMode == 1) { FragColor = vec4(vec3(rgb.r), 1.0); }
                else if (uChannelMode == 2) { FragColor = vec4(vec3(rgb.g), 1.0); }
                else if (uChannelMode == 3) { FragColor = vec4(vec3(rgb.b), 1.0); }
                else                        { FragColor = vec4(rgb, 1.0); }
            }
        )";
    }

    /**
     * RGB / RGBA（含由纹理上传阶段解包并归一化的 RGB10_A2）
     */
    inline const char* GetRGBBody()
    {
        return R"(
            out vec4 FragColor;

            in vec2 TexCoord;

            uniform sampler2D uTexture;
            uniform int uChannelMode;  // 0=彩色 1=R 2=G 3=B 4=A

            void main()
            {
                vec4 c = texture(uTexture, TexCoord);

                if      (uChannelMode == 1) { FragColor = vec4(vec3(c.r), 1.0); }
                else if (uChannelMode == 2) { FragColor = vec4(vec3(c.g), 1.0); }
                else if (uChannelMode == 3) { FragColor = vec4(vec3(c.b), 1.0); }
                else if (uChannelMode == 4) { FragColor = vec4(vec3(c.a), 1.0); }
                else                        { FragColor = vec4(ApplyColorPipeline(c.rgb), 1.0); }
            }
        )";
    }

    /**
     * 灰度：单通道纹理需要把 .r 复制到 rgb，否则会显示成纯红
     *
     * 也接色彩管线 —— PQ 编码的单平面 luma dump 是存在的，
     * 而且此时"这个码值到底是多少 nit"恰恰是最想知道的事情。
     */
    inline const char* GetGrayscaleBody()
    {
        return R"(
            out vec4 FragColor;

            in vec2 TexCoord;

            uniform sampler2D uTexture;
            uniform float uSampleScale;   // 16bit 容器里装 10/12bit 时把值拉回满量程

            void main()
            {
                float g = clamp(texture(uTexture, TexCoord).r * uSampleScale, 0.0, 1.0);
                FragColor = vec4(ApplyColorPipeline(vec3(g)), 1.0);
            }
        )";
    }

    /**
     * 根据图像格式获取片段着色器代码
     *
     * 返回值是拼好的完整源码（版本声明 + 色彩管线前导块 + 格式相关的取样代码）。
     * Bayer 自带完整源码、不拼前导块 —— 理由见 GetBayerShader 的注释。
     *
     * @param Format 图像格式
     * @return 片段着色器源码
     */
    inline std::string GetFragmentShaderForFormat(EImageFormat Format)
    {
        const char* body = nullptr;

        switch (Format)
        {
        case EImageFormat::NV12:
        case EImageFormat::NV21:
        case EImageFormat::P010:
        case EImageFormat::YUV420SP16:
        case EImageFormat::NV16:
        case EImageFormat::P210:
            body = GetSemiPlanarYUVBody();
            break;

        case EImageFormat::YUV420P:
        case EImageFormat::YUV422P:
        case EImageFormat::YUV444P:
        case EImageFormat::YV12:
            body = GetPlanarYUVBody();
            break;

        case EImageFormat::YUY2:
        case EImageFormat::UYVY:
            body = GetPackedYUVBody();
            break;

        case EImageFormat::Bayer8:
        case EImageFormat::Bayer10:
        case EImageFormat::Bayer12:
        case EImageFormat::Bayer14:
        case EImageFormat::Bayer16:
        case EImageFormat::BayerPacked10:
        case EImageFormat::BayerPacked12:
        case EImageFormat::BayerPacked14:
            return GetBayerShader();

        case EImageFormat::Grayscale8:
        case EImageFormat::Grayscale16:
        case EImageFormat::Raw:
            body = GetGrayscaleBody();
            break;

        case EImageFormat::RGB8:
        case EImageFormat::RGBA8:
        case EImageFormat::RGB16:
        case EImageFormat::RGBA16:
        case EImageFormat::RGB10A2:
        default:
            body = GetRGBBody();
            break;
        }

        return std::string("#version 330 core\n") + GetColorPipelineGLSL() + body;
    }

    /**
     * 获取顶点着色器代码
     * 当前所有格式共享同一个顶点着色器
     */
    inline const char* GetVertexShader()
    {
        return GetDefaultVertexShader();
    }

    /**
     * 该格式是否为 YUV 家族（决定是否需要绑定色彩转换相关 uniform）
     */
    inline bool IsYUVFormat(EImageFormat Format)
    {
        return FImageFormatDesc::IsYUV(Format);
    }

    /**
     * 该格式需要几张纹理。直接问格式描述表，不再维护第二份分支。
     */
    inline int GetRequiredTextureCount(EImageFormat Format)
    {
        return FImageFormatDesc::Get(Format).PlaneCount;
    }
}
