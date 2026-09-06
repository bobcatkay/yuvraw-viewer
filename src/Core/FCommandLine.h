#pragma once

#include "Image/FDisplaySettings.h"
#include "Image/FImageLoadParams.h"

#include <string>

/**
 * 命令行参数
 *
 * 支持"用 YUVRaw 打开"以及脚本化调用：
 *
 *   YUVRaw.exe frame.yuv --format NV21 --width 1440 --height 1920 --stride 1472
 *   YUVRaw.exe photo.png
 *   YUVRaw.exe sensor.raw --format RAW_SENSOR --width 4032 --height 3024 --bits 12
 *   YUVRaw.exe hdr.yuv --format P010 --transfer pq --primaries bt2020 --matrix bt2020
 *
 * 格式名取自格式描述表的 Name 字段（NV21 / P010 / I420 / Bayer16 ...），大小写不敏感。
 * 未指定的参数会退回到从文件名解析的结果。
 *
 * 色彩相关的几项（--matrix / --range / --primaries / --transfer / --refwhite /
 * --tonemap / --exposure）**没有任何办法从文件里推断**：P010 既可能是 PQ 也可能是
 * 普通 SDR，格式本身不说明这一点。放到命令行上是为了能脚本化地验证同一幅图在
 * 不同解读下的样子，否则每次都只能在属性面板上手点。
 */
struct FCommandLineOptions
{
    /// 启动时要打开的文件或目录，空表示不打开
    std::string PathToOpen;

    /// --compare 指定的对比图。非空时启动后自动切到并排模式，
    /// 对比图沿用主图的加载参数（同一批 dump 通常格式分辨率一致）
    std::string ComparePath;

    /// 是否显式指定了各项参数（未指定的交给文件名解析）
    bool bHasFormat = false;
    bool bHasWidth = false;
    bool bHasHeight = false;
    bool bHasStride = false;
    bool bHasBitsPerPixel = false;

    /// 是否指定过任何色彩项。false 时完全不碰文档的显示设置
    bool bHasDisplay = false;

    /// --no-hdr：即使显示器支持也强制走 SDR 色调映射，用来 A/B 对比
    bool bDisableHdr = false;

    FDisplaySettings Display;

    FImageLoadParams Params;

    /**
     * 解析 WinMain 拿到的命令行字符串
     * @param CommandLine lpCmdLine（不含程序名）
     */
    static FCommandLineOptions Parse(const char* CommandLine);

    /**
     * 把显式指定的项覆盖到目标参数上，未指定的保持不变
     */
    void ApplyTo(FImageLoadParams& OutParams) const;
};
