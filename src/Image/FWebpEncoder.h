#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * 最小实现的无损 WebP（VP8L）编码器
 *
 * 为什么要自己写：Windows 的 WIC **只带 WebP 解码器，不带编码器**
 * （`Microsoft Webp Decoder` 在解码器列表里，编码器列表里没有对应项），
 * 而项目不引入第三方库，libwebp 也不在依赖里。
 *
 * 实现范围刻意收窄到"正确 + 够用"：
 *   - 只用 subtract-green 变换（红/蓝各减去绿，photo 上收益明显且实现只有两行）
 *   - 不做 LZ77 回溯引用，不用色彩缓存，不用预测器/元 Huffman
 *   - 四个通道各自做一次静态 Huffman 熵编码
 *
 * 因此压缩率不如 libwebp（大致介于 BMP 与 PNG 之间），但输出是**完全合规的
 * 无损 .webp**，系统照片查看器、浏览器、WIC 解码器都能打开。
 * 导出后可以用 FWicImageLoader 重新读回来做往返校验。
 */
namespace FWebpEncoder
{
    /// VP8L 的尺寸字段只有 14 位，边长上限 16384
    constexpr int32_t kMaxDimension = 16384;

    /**
     * 把紧凑排列的 RGB8（每像素 3 字节）编码成无损 WebP 文件的完整字节流
     *
     * @param Rgb      像素数据，长度必须 >= Width * Height * 3
     * @param OutFile  输出的完整文件内容（含 RIFF 头）
     * @param OutError 失败原因
     * @return 是否成功
     */
    bool EncodeLosslessRgb8(
        const uint8_t* Rgb,
        int32_t Width,
        int32_t Height,
        std::vector<uint8_t>& OutFile,
        std::string& OutError);
}
