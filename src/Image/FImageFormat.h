#pragma once

#include <cstdint>

/**
 * 图像像素格式枚举
 *
 * 重要：新增格式一律**追加在末尾**。
 * 描述表和持久化数据按枚举值索引；属性面板顺序由 GetDisplayOrder() 单独控制。
 */
enum class EImageFormat
{
    Unknown,
    RGB8,
    RGBA8,
    RGB16,
    RGBA16,
    YUV420P,        // I420: Y 平面 + U 平面 + V 平面，色度 1/2 宽 1/2 高
    YUV422P,        // Y + U + V，色度 1/2 宽 全高
    YUV444P,        // Y + U + V，色度全尺寸
    NV12,           // 半平面 4:2:0，色度交织顺序 U,V
    NV21,           // 半平面 4:2:0，色度交织顺序 V,U
    P010,           // 半平面 4:2:0 10bit，采样值存在 16bit 的**高 10 位**
    Grayscale8,
    Grayscale16,
    Raw,

    // --- 以下为 Phase 2 追加 ---
    YV12,           // 4:2:0 平面，V 平面在 U 之前
    NV16,           // 半平面 4:2:2
    P210,           // 半平面 4:2:2 10bit
    YUY2,           // packed 4:2:2，字节排列 Y0 U Y1 V
    UYVY,           // packed 4:2:2，字节排列 U Y0 V Y1
    Bayer8,         // 8bit CFA，未插值
    Bayer16,        // 16bit CFA，低位对齐
    BayerPacked10,  // MIPI RAW10，4 像素 5 字节，加载时解包为 Bayer16
    BayerPacked12,  // MIPI RAW12，2 像素 3 字节，加载时解包为 Bayer16

    // --- Android Bayer RAW 扩展：只能追加，不能改变前面枚举的持久化值 ---
    Bayer10,        // 10bit CFA，存放在 16bit 容器的低 10 位
    Bayer12,        // 12bit CFA，存放在 16bit 容器的低 12 位
    Bayer14,        // 14bit CFA，存放在 16bit 容器的低 14 位
    BayerPacked14,  // Android RAW14，4 像素 7 字节，加载时解包为 Bayer16

    // --- packed RGB 扩展：只能追加，不能改变前面枚举的持久化值 ---
    RGB10A2,        // 32bit 小端：R[9:0] G[19:10] B[29:20] A[31:30]

    // --- 可配置半平面 YUV 扩展：只能追加，不能改变前面枚举的持久化值 ---
    YUV420SP16,     // 半平面 4:2:0，16bit 容器；有效位深、字节序与有效位对齐可配置
};

/**
 * 色彩模型。决定用哪一族着色器，以及是否需要 YUV 转换 uniform
 */
enum class EColorModel
{
    RGB,
    YUV,
    Gray,
    Bayer
};

/**
 * 多字节采样在文件中的字节序。
 *
 * 这里只描述单个数值采样内部的字节顺序，不描述 NV12/NV21 这类通道排列。
 */
enum class EByteOrder
{
    LittleEndian,
    BigEndian,
};

/**
 * 有效位在采样容器中的对齐方式。
 *
 * 例如 10bit 放进 16bit 容器时，低位对齐的 shift 为 0，高位对齐的 shift 为 6。
 */
enum class ESampleAlignment
{
    LeastSignificantBits,
    MostSignificantBits,
};

/**
 * 格式属性在属性面板中的行为。
 *
 * NotApplicable 表示该概念对格式没有意义；Fixed 显示格式规定值但不可修改；
 * Configurable 表示裸数据本身无法携带该信息，需要用户选择。
 */
enum class EFormatPropertyMode
{
    NotApplicable,
    Fixed,
    Configurable,
};

/**
 * YCbCr -> R'G'B' 的**矩阵系数**（Kr/Kb），对应 ffmpeg 的 `colorspace` 字段
 *
 * 注意这里只描述矩阵，不描述原色，也不描述传输函数 —— 三者是正交的三件事。
 * 例如 "BT.2020ncl + BT.2020 原色 + PQ" 的素材，这个字段只管其中的 "BT.2020ncl"。
 * 另外两轴见 EColorPrimaries / EColorTransfer。
 *
 * 枚举名保持历史称呼（面板下拉框按枚举顺序构建），语义以本注释为准。
 */
enum class EColorSpace
{
    BT601,
    BT709,
    BT2020
};

/**
 * 三原色与白点，对应 ffmpeg 的 `color_primaries` 字段
 *
 * 决定线性域里 RGB 三分量各自代表哪个实际颜色，必须在**线性域**乘一个 3x3 转换矩阵。
 *
 * 方向别搞反：BT.709 的原色落在 BT.2020 色域**内部**，所以同一组坐标在 709 屏上
 * 显示出来的颜色比原本要淡。把 BT.2020 素材原封不动送给 709 显示器的结果是**发闷、欠饱和**，
 * 而不是过饱和；正确转换会把坐标拉开（红色 0.5 -> 0.76 这种），甚至出现负分量，
 * 那正是为了在更窄的色域上还原同一个物理颜色。
 *
 * （反过来才是过饱和：把 BT.709 素材直接丢到广色域屏上不做转换。）
 */
enum class EColorPrimaries
{
    BT709,       ///< = sRGB 原色，D65
    BT2020,      ///< 广色域，D65
    DisplayP3,   ///< DCI-P3 原色 + D65 白点
    BT601_525,   ///< SMPTE 170M（NTSC）
    BT601_625,   ///< EBU 3213（PAL/SECAM）
};

/**
 * 传输函数（EOTF），对应 ffmpeg 的 `color_trc` 字段
 *
 * 决定码值与光强之间的曲线。**这是 HDR 素材最容易被漏掉的一轴** ——
 * PQ 编码的码值当成 sRGB 直接显示，画面会灰蒙蒙、低对比。
 *
 * SDR / BT1886 / Linear 是**相对**传输函数（1.0 就是"显示白"，没有绝对亮度含义）；
 * PQ / HLG 是**绝对**传输函数（解码结果直接是 cd/m^2）。
 * 两类在归一化时走不同分支，见 FColorTransform::IsAbsoluteTransfer()。
 */
enum class EColorTransfer
{
    SDR,      ///< 按 sRGB 曲线解读。与"不做任何处理"往返恒等，是默认值
    BT1886,   ///< 纯 2.4 次幂，广播视频的显示参考曲线
    PQ,       ///< SMPTE ST 2084，绝对亮度 0-10000 cd/m^2
    HLG,      ///< ARIB STD-B67 / BT.2100 HLG，含 OOTF
    Linear,   ///< 数据已是线性光
};

/**
 * HDR -> SDR 的色调映射算子
 *
 * 默认是 Clip：调试工具里"好看"没有意义，能对上像素探针的读数才有意义。
 * 另外两个算子是给"想看看这段素材大致该长什么样"的场合准备的。
 */
enum class EToneMapOperator
{
    Clip,       ///< 直接裁剪。超出部分由"超范围高亮"负责暴露，不做任何美化
    Reinhard,   ///< 扩展 Reinhard，把 uToneMapWhite 映射到 1.0
    ACES,       ///< Narkowicz 的 ACES 近似拟合曲线
};

/**
 * 数值范围。相机/视频 dump 出的 YUV 绝大多数是 Limited (Y 16-235, UV 16-240)
 */
enum class EColorRange
{
    Limited,
    Full
};

/**
 * Bayer 滤色阵列排布（以左上角 2x2 为准）
 * 不影响内存布局，只影响去马赛克，因此作为独立参数而不是独立格式
 */
enum class EBayerPattern
{
    RGGB,
    BGGR,
    GRBG,
    GBRG
};
