#include "FWebpEncoder.h"

#include <algorithm>
#include <queue>
#include <vector>

namespace
{
    // --- VP8L 的字母表大小（无色彩缓存时）---
    constexpr int32_t kLiteralCodes = 256;
    constexpr int32_t kLengthCodes = 24;
    constexpr int32_t kGreenAlphabet = kLiteralCodes + kLengthCodes;  // 280
    constexpr int32_t kDistanceCodes = 40;

    /// 码长字母表：0-15 是字面码长，16 重复前一个非零码长，17/18 重复零
    constexpr int32_t kCodeLengthCodes = 19;

    /// 码长字母表在码流里的存放顺序（规范固定）
    constexpr uint8_t kStorageOrder[kCodeLengthCodes] = {
        17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

    /// 解码器读码长时 prev_code_len 的初值，编码端必须与它一致
    constexpr int32_t kDefaultCodeLength = 8;

    /// 主字母表的码长上限（规范允许 15），码长字母表自身上限 7（每个码长用 3 位存）
    constexpr int32_t kMaxCodeLength = 15;
    constexpr int32_t kMaxCodeLengthCodeLength = 7;

    /**
     * VP8L 的位写入器：**低位在前**打包进字节流
     *
     * Huffman 码本身在流里是"高位在前"的，所以写码字时先把它按码长反转，
     * 再交给这里按低位在前写出去，两次翻转正好抵消。
     */
    class FBitWriter
    {
    public:
        void PutBits(uint32_t Value, int32_t Count)
        {
            if (Count <= 0)
            {
                return;
            }

            const uint32_t mask = (Count >= 32) ? 0xFFFFFFFFu : ((1u << Count) - 1u);

            Accum |= static_cast<uint64_t>(Value & mask) << BitCount;
            BitCount += Count;

            while (BitCount >= 8)
            {
                Bytes.push_back(static_cast<uint8_t>(Accum & 0xFFu));
                Accum >>= 8;
                BitCount -= 8;
            }
        }

        void Flush()
        {
            if (BitCount > 0)
            {
                Bytes.push_back(static_cast<uint8_t>(Accum & 0xFFu));
                Accum = 0;
                BitCount = 0;
            }
        }

        std::vector<uint8_t> Bytes;

    private:
        uint64_t Accum = 0;
        int32_t BitCount = 0;
    };

    /**
     * 一个前缀码：每个符号的码长，以及**已按码长反转**、可直接写出的码字
     */
    struct FPrefixCode
    {
        std::vector<uint8_t> Lengths;
        std::vector<uint16_t> Codes;

        void Put(FBitWriter& Writer, int32_t Symbol) const
        {
            Writer.PutBits(Codes[Symbol], Lengths[Symbol]);
        }
    };

    uint16_t ReverseBits(uint32_t Code, int32_t Length)
    {
        uint16_t reversed = 0;

        for (int32_t i = 0; i < Length; ++i)
        {
            reversed = static_cast<uint16_t>((reversed << 1) | ((Code >> i) & 1u));
        }

        return reversed;
    }

    /**
     * 按频次算 Huffman 码长
     *
     * 超过码长上限时把所有频次减半后重算 —— 频次最小值是 1，(1+1)/2 仍是 1，
     * 反复几轮后所有符号趋于等频，深度收敛到 ceil(log2(N))，一定能落进上限内。
     */
    void BuildCodeLengths(const std::vector<uint32_t>& InFreqs, int32_t MaxLength, std::vector<uint8_t>& OutLengths)
    {
        const int32_t alphabetSize = static_cast<int32_t>(InFreqs.size());

        OutLengths.assign(alphabetSize, 0);

        std::vector<uint32_t> freqs = InFreqs;

        struct FNode
        {
            uint32_t Weight = 0;
            int32_t Left = -1;
            int32_t Right = -1;
            int32_t Symbol = -1;
        };

        while (true)
        {
            std::vector<FNode> nodes;
            nodes.reserve(static_cast<size_t>(alphabetSize) * 2);

            // 小顶堆按 (权重, 建立顺序) 排序，保证结果与平台无关
            using FEntry = std::pair<uint64_t, int32_t>;  // (权重<<32 | 序号, 节点下标)
            std::priority_queue<FEntry, std::vector<FEntry>, std::greater<FEntry>> heap;

            for (int32_t symbol = 0; symbol < alphabetSize; ++symbol)
            {
                if (freqs[symbol] == 0)
                {
                    continue;
                }

                FNode node;
                node.Weight = freqs[symbol];
                node.Symbol = symbol;

                const int32_t index = static_cast<int32_t>(nodes.size());
                nodes.push_back(node);
                heap.push({ (static_cast<uint64_t>(node.Weight) << 20) | static_cast<uint32_t>(index), index });
            }

            if (heap.size() < 2)
            {
                // 调用方负责保证至少两个符号，这里只做兜底
                if (heap.size() == 1)
                {
                    OutLengths[nodes[heap.top().second].Symbol] = 1;
                }

                return;
            }

            while (heap.size() > 1)
            {
                const int32_t a = heap.top().second;
                heap.pop();
                const int32_t b = heap.top().second;
                heap.pop();

                FNode parent;
                parent.Weight = nodes[a].Weight + nodes[b].Weight;
                parent.Left = a;
                parent.Right = b;

                const int32_t index = static_cast<int32_t>(nodes.size());
                nodes.push_back(parent);
                heap.push({ (static_cast<uint64_t>(parent.Weight) << 20) | static_cast<uint32_t>(index), index });
            }

            // 自顶向下算深度。符号数上限 280，递归深度也不会失控，但仍用显式栈避免深树爆栈
            std::fill(OutLengths.begin(), OutLengths.end(), static_cast<uint8_t>(0));

            int32_t maxDepth = 0;
            std::vector<std::pair<int32_t, int32_t>> stack;  // (节点下标, 深度)
            stack.push_back({ heap.top().second, 0 });

            while (!stack.empty())
            {
                const auto item = stack.back();
                stack.pop_back();

                const FNode& node = nodes[item.first];

                if (node.Symbol >= 0)
                {
                    OutLengths[node.Symbol] = static_cast<uint8_t>(item.second);
                    maxDepth = std::max(maxDepth, item.second);

                    continue;
                }

                stack.push_back({ node.Left, item.second + 1 });
                stack.push_back({ node.Right, item.second + 1 });
            }

            if (maxDepth <= MaxLength)
            {
                return;
            }

            for (int32_t symbol = 0; symbol < alphabetSize; ++symbol)
            {
                if (freqs[symbol] > 0)
                {
                    freqs[symbol] = (freqs[symbol] + 1) / 2;
                }
            }
        }
    }

    /**
     * 由码长生成规范 Huffman 码字（RFC 1951 的做法），并按码长反转好
     */
    void AssignCanonicalCodes(FPrefixCode& Code)
    {
        uint32_t lengthCount[kMaxCodeLength + 1] = { 0 };

        for (uint8_t length : Code.Lengths)
        {
            ++lengthCount[length];
        }

        lengthCount[0] = 0;

        uint32_t nextCode[kMaxCodeLength + 1] = { 0 };
        uint32_t code = 0;

        for (int32_t length = 1; length <= kMaxCodeLength; ++length)
        {
            code = (code + lengthCount[length - 1]) << 1;
            nextCode[length] = code;
        }

        Code.Codes.assign(Code.Lengths.size(), 0);

        for (size_t symbol = 0; symbol < Code.Lengths.size(); ++symbol)
        {
            const int32_t length = Code.Lengths[symbol];

            if (length > 0)
            {
                Code.Codes[symbol] = ReverseBits(nextCode[length]++, length);
            }
        }
    }

    /**
     * 建立前缀码
     *
     * **只有一个符号的前缀码在 VP8L 里是 0 位码**（解码器直接返回该符号，不消耗比特），
     * 与"写 1 位"的直觉冲突，极易写出对不上的码流。这里干脆保证至少两个符号：
     * 频次表只有一个非零项时补一个永远不会被用到的邻居符号，代价是每像素多 1 位。
     */
    FPrefixCode BuildPrefixCode(std::vector<uint32_t> Freqs, int32_t MaxLength)
    {
        int32_t usedCount = 0;
        int32_t firstUsed = 0;

        for (size_t symbol = 0; symbol < Freqs.size(); ++symbol)
        {
            if (Freqs[symbol] > 0)
            {
                if (usedCount == 0)
                {
                    firstUsed = static_cast<int32_t>(symbol);
                }

                ++usedCount;
            }
        }

        if (usedCount == 0)
        {
            Freqs[0] = 1;
            Freqs[1] = 1;
        }
        else if (usedCount == 1)
        {
            const size_t filler = (static_cast<size_t>(firstUsed) + 1) % Freqs.size();
            Freqs[filler] = 1;
        }

        FPrefixCode result;
        BuildCodeLengths(Freqs, MaxLength, result.Lengths);
        AssignCanonicalCodes(result);

        return result;
    }

    /// 码长流里的一个记号：符号本身 + 跟在后面的附加位
    struct FLengthToken
    {
        int32_t Symbol = 0;
        int32_t ExtraValue = 0;
        int32_t ExtraBits = 0;
    };

    /**
     * 把码长数组游程编码成码长字母表上的记号流
     *
     * 记号 16 重复"前一个非零码长"，17/18 重复零。解码端的 prev_code_len 初值是 8
     * 且只在读到非零字面码长时更新，这里必须完全按同样的规则推进。
     */
    void TokenizeCodeLengths(
        const std::vector<uint8_t>& Lengths,
        std::vector<FLengthToken>& OutTokens,
        std::vector<uint32_t>& OutFreqs)
    {
        OutTokens.clear();
        OutFreqs.assign(kCodeLengthCodes, 0);

        auto emit = [&OutTokens, &OutFreqs](int32_t Symbol, int32_t ExtraValue, int32_t ExtraBits)
        {
            FLengthToken token;
            token.Symbol = Symbol;
            token.ExtraValue = ExtraValue;
            token.ExtraBits = ExtraBits;

            OutTokens.push_back(token);
            ++OutFreqs[Symbol];
        };

        const int32_t count = static_cast<int32_t>(Lengths.size());
        int32_t prevNonZero = kDefaultCodeLength;
        int32_t i = 0;

        while (i < count)
        {
            const uint8_t value = Lengths[i];

            int32_t runEnd = i + 1;

            while (runEnd < count && Lengths[runEnd] == value)
            {
                ++runEnd;
            }

            int32_t remaining = runEnd - i;

            if (value == 0)
            {
                while (remaining > 0)
                {
                    if (remaining < 3)
                    {
                        emit(0, 0, 0);
                        --remaining;
                    }
                    else if (remaining <= 10)
                    {
                        emit(17, remaining - 3, 3);
                        remaining = 0;
                    }
                    else
                    {
                        const int32_t take = std::min(remaining, 138);
                        emit(18, take - 11, 7);
                        remaining -= take;
                    }
                }
            }
            else
            {
                // 记号 16 只能重复"前一个非零码长"，值对不上时得先写一次字面量
                if (value != prevNonZero)
                {
                    emit(value, 0, 0);
                    prevNonZero = value;
                    --remaining;
                }

                while (remaining >= 3)
                {
                    const int32_t take = std::min(remaining, 6);
                    emit(16, take - 3, 2);
                    remaining -= take;
                }

                while (remaining > 0)
                {
                    emit(value, 0, 0);
                    --remaining;
                }
            }

            i = runEnd;
        }
    }

    /**
     * 写出一个前缀码的定义
     */
    void StorePrefixCode(FBitWriter& Writer, const FPrefixCode& Code)
    {
        int32_t usedCount = 0;
        int32_t symbols[2] = { 0, 0 };

        for (size_t symbol = 0; symbol < Code.Lengths.size() && usedCount < 3; ++symbol)
        {
            if (Code.Lengths[symbol] != 0)
            {
                if (usedCount < 2)
                {
                    symbols[usedCount] = static_cast<int32_t>(symbol);
                }

                ++usedCount;
            }
        }

        // 简单码：1 或 2 个符号，各占 1 位码字
        if (usedCount <= 2 && symbols[0] < 256 && symbols[1] < 256)
        {
            Writer.PutBits(1, 1);
            Writer.PutBits(usedCount - 1, 1);

            if (symbols[0] <= 1)
            {
                Writer.PutBits(0, 1);
                Writer.PutBits(symbols[0], 1);
            }
            else
            {
                Writer.PutBits(1, 1);
                Writer.PutBits(symbols[0], 8);
            }

            if (usedCount == 2)
            {
                Writer.PutBits(symbols[1], 8);
            }

            return;
        }

        Writer.PutBits(0, 1);

        std::vector<FLengthToken> tokens;
        std::vector<uint32_t> tokenFreqs;
        TokenizeCodeLengths(Code.Lengths, tokens, tokenFreqs);

        const FPrefixCode lengthCode = BuildPrefixCode(tokenFreqs, kMaxCodeLengthCodeLength);

        // 尾部全是未用符号时可以少写几个 3 位字段
        int32_t storedCount = kCodeLengthCodes;

        while (storedCount > 4 && lengthCode.Lengths[kStorageOrder[storedCount - 1]] == 0)
        {
            --storedCount;
        }

        Writer.PutBits(storedCount - 4, 4);

        for (int32_t i = 0; i < storedCount; ++i)
        {
            Writer.PutBits(lengthCode.Lengths[kStorageOrder[i]], 3);
        }

        // 不使用 max_symbol 截断：记号流本来就覆盖了整个字母表
        Writer.PutBits(0, 1);

        for (const FLengthToken& token : tokens)
        {
            lengthCode.Put(Writer, token.Symbol);
            Writer.PutBits(token.ExtraValue, token.ExtraBits);
        }
    }

    /**
     * 距离码在本编码器里永远用不到（不做回溯引用），但码流里必须有一个定义。
     * 写成"两个 1 位符号"的简单码，避免单符号 0 位码那种容易对不上的形态。
     */
    void StoreUnusedDistanceCode(FBitWriter& Writer)
    {
        Writer.PutBits(1, 1);  // 简单码
        Writer.PutBits(1, 1);  // 符号数 - 1 = 1
        Writer.PutBits(0, 1);  // 第一个符号用 1 位表示
        Writer.PutBits(0, 1);  // 符号 0
        Writer.PutBits(1, 8);  // 符号 1
    }

    /**
     * 写出一幅"熵编码图像"：色彩缓存标志 + 五个前缀码 + 全字面量的像素流
     *
     * 主图像和预测器图像共用这段逻辑，区别只在于**元 Huffman 标志位**：
     * 解码器只在最外层（level 0）的图像里读它，变换携带的子图像里根本不读，
     * 多写一位就会整条码流错位。
     *
     * @param bWriteMetaPrefixBit 最外层图像传 true，变换携带的子图像传 false
     */
    void WriteEntropyCodedImage(FBitWriter& Writer, const uint32_t* Argb, size_t Count, bool bWriteMetaPrefixBit)
    {
        std::vector<uint32_t> greenFreqs(kGreenAlphabet, 0);
        std::vector<uint32_t> redFreqs(kLiteralCodes, 0);
        std::vector<uint32_t> blueFreqs(kLiteralCodes, 0);
        std::vector<uint32_t> alphaFreqs(kLiteralCodes, 0);

        for (size_t i = 0; i < Count; ++i)
        {
            const uint32_t argb = Argb[i];

            ++greenFreqs[(argb >> 8) & 0xFFu];
            ++redFreqs[(argb >> 16) & 0xFFu];
            ++blueFreqs[argb & 0xFFu];
            ++alphaFreqs[(argb >> 24) & 0xFFu];
        }

        const FPrefixCode greenCode = BuildPrefixCode(greenFreqs, kMaxCodeLength);
        const FPrefixCode redCode = BuildPrefixCode(redFreqs, kMaxCodeLength);
        const FPrefixCode blueCode = BuildPrefixCode(blueFreqs, kMaxCodeLength);
        const FPrefixCode alphaCode = BuildPrefixCode(alphaFreqs, kMaxCodeLength);

        Writer.PutBits(0, 1);  // 不用色彩缓存

        if (bWriteMetaPrefixBit)
        {
            Writer.PutBits(0, 1);  // 不用元 Huffman，整幅图共用一组前缀码
        }

        // 五个前缀码的顺序是规范固定的：绿(含长度码)、红、蓝、透明度、距离
        StorePrefixCode(Writer, greenCode);
        StorePrefixCode(Writer, redCode);
        StorePrefixCode(Writer, blueCode);
        StorePrefixCode(Writer, alphaCode);
        StoreUnusedDistanceCode(Writer);

        // 每个像素依次写绿、红、蓝、透明度，全部是字面量（不做回溯引用）
        for (size_t i = 0; i < Count; ++i)
        {
            const uint32_t argb = Argb[i];

            greenCode.Put(Writer, (argb >> 8) & 0xFFu);
            redCode.Put(Writer, (argb >> 16) & 0xFFu);
            blueCode.Put(Writer, argb & 0xFFu);
            alphaCode.Put(Writer, (argb >> 24) & 0xFFu);
        }
    }

    int32_t Clip255(int32_t Value)
    {
        return (Value < 0) ? 0 : ((Value > 255) ? 255 : Value);
    }

    /**
     * VP8L 的 12 号预测器：逐通道 clamp(L + T - TL)
     *
     * 选它作为全图唯一的预测模式：对连续色调的图像效果稳定，
     * 而且不需要右上邻居，省掉了行末 TR 越界那条容易踩的边界规则。
     */
    uint32_t PredictClampedAddSubtractFull(uint32_t Left, uint32_t Top, uint32_t TopLeft)
    {
        uint32_t result = 0;

        for (int32_t shift = 0; shift < 32; shift += 8)
        {
            const int32_t l = static_cast<int32_t>((Left >> shift) & 0xFFu);
            const int32_t t = static_cast<int32_t>((Top >> shift) & 0xFFu);
            const int32_t tl = static_cast<int32_t>((TopLeft >> shift) & 0xFFu);

            result |= static_cast<uint32_t>(Clip255(l + t - tl)) << shift;
        }

        return result;
    }

    /// 逐通道相减，每个通道各自模 256 回绕
    uint32_t SubtractPixels(uint32_t A, uint32_t B)
    {
        uint32_t result = 0;

        for (int32_t shift = 0; shift < 32; shift += 8)
        {
            const uint32_t diff = (((A >> shift) & 0xFFu) - ((B >> shift) & 0xFFu)) & 0xFFu;
            result |= diff << shift;
        }

        return result;
    }

    /// 变换子图像的边长：向上取整到 2^Bits 的块数
    int32_t SubSampleSize(int32_t Size, int32_t Bits)
    {
        return (Size + (1 << Bits) - 1) >> Bits;
    }

    void AppendFourCC(std::vector<uint8_t>& Out, const char* Tag)
    {
        Out.push_back(static_cast<uint8_t>(Tag[0]));
        Out.push_back(static_cast<uint8_t>(Tag[1]));
        Out.push_back(static_cast<uint8_t>(Tag[2]));
        Out.push_back(static_cast<uint8_t>(Tag[3]));
    }

    void AppendUInt32LE(std::vector<uint8_t>& Out, uint32_t Value)
    {
        Out.push_back(static_cast<uint8_t>(Value & 0xFFu));
        Out.push_back(static_cast<uint8_t>((Value >> 8) & 0xFFu));
        Out.push_back(static_cast<uint8_t>((Value >> 16) & 0xFFu));
        Out.push_back(static_cast<uint8_t>((Value >> 24) & 0xFFu));
    }
}

namespace FWebpEncoder
{
    bool EncodeLosslessRgb8(
        const uint8_t* Rgb,
        int32_t Width,
        int32_t Height,
        std::vector<uint8_t>& OutFile,
        std::string& OutError)
    {
        OutFile.clear();

        if (!Rgb || Width <= 0 || Height <= 0)
        {
            OutError = u8"图像数据为空";

            return false;
        }

        if (Width > kMaxDimension || Height > kMaxDimension)
        {
            OutError = u8"WebP 单边最大 16384 像素";

            return false;
        }

        const size_t pixelCount = static_cast<size_t>(Width) * static_cast<size_t>(Height);

        // 打包成 ARGB 并就地做 subtract-green：红/蓝各减去绿。
        // 解码端的逆变换是加回去，两者必须成对。对照片而言这一步能明显降低红蓝通道的熵。
        std::vector<uint32_t> argb(pixelCount);

        for (size_t i = 0; i < pixelCount; ++i)
        {
            const uint32_t r = Rgb[i * 3 + 0];
            const uint32_t g = Rgb[i * 3 + 1];
            const uint32_t b = Rgb[i * 3 + 2];

            argb[i] = 0xFF000000u
                    | (((r - g) & 0xFFu) << 16)
                    | (g << 8)
                    | ((b - g) & 0xFFu);
        }

        // 预测器变换：逐像素减去由左/上/左上邻居算出的预测值。
        // 没有它，连续色调的图像每个通道都要花满 8 位；有了它残差集中在 0 附近，
        // 熵编码才真正吃得上力（否则输出会比 BMP 还大）。
        //
        // 预测值必须用**重建后**的邻居算，无损编码下重建值就是原值，
        // 因此这里保留原始的前一行/当前行，残差写回 argb。
        {
            std::vector<uint32_t> prevRow(Width, 0);
            std::vector<uint32_t> curRow(Width, 0);

            for (int32_t y = 0; y < Height; ++y)
            {
                uint32_t* row = argb.data() + static_cast<size_t>(y) * Width;

                std::copy(row, row + Width, curRow.begin());

                for (int32_t x = 0; x < Width; ++x)
                {
                    uint32_t predicted;

                    if (y == 0)
                    {
                        // 首行：首像素用不透明黑，其余用左邻（规范硬性规定，与分块模式无关）
                        predicted = (x == 0) ? 0xFF000000u : curRow[x - 1];
                    }
                    else if (x == 0)
                    {
                        // 首列固定用上邻
                        predicted = prevRow[0];
                    }
                    else
                    {
                        predicted = PredictClampedAddSubtractFull(curRow[x - 1], prevRow[x], prevRow[x - 1]);
                    }

                    row[x] = SubtractPixels(curRow[x], predicted);
                }

                prevRow.swap(curRow);
            }
        }

        // 预测器图像：每块一个像素，模式号存在绿通道。全图只用一种模式，所以它是一幅纯色小图
        const int32_t predictorBits = 9;  // 512x512 一块，取最大值让这幅子图尽量小
        const int32_t predictorWidth = SubSampleSize(Width, predictorBits);
        const int32_t predictorHeight = SubSampleSize(Height, predictorBits);

        const std::vector<uint32_t> predictorImage(
            static_cast<size_t>(predictorWidth) * predictorHeight,
            0xFF000000u | (12u << 8));

        FBitWriter writer;

        // --- VP8L 头 ---
        writer.PutBits(0x2F, 8);              // 签名
        writer.PutBits(Width - 1, 14);
        writer.PutBits(Height - 1, 14);
        writer.PutBits(0, 1);                 // alpha_is_used：全不透明
        writer.PutBits(0, 3);                 // version

        // --- 变换链 ---
        // 解码器按**读到的相反顺序**做逆变换，所以这里的书写顺序就是编码时的施加顺序：
        // 先 subtract-green，再预测。
        writer.PutBits(1, 1);
        writer.PutBits(2, 2);                 // SUBTRACT_GREEN，无附加数据

        writer.PutBits(1, 1);
        writer.PutBits(0, 2);                 // PREDICTOR_TRANSFORM
        writer.PutBits(predictorBits - 2, 3);
        WriteEntropyCodedImage(writer, predictorImage.data(), predictorImage.size(), false);

        writer.PutBits(0, 1);                 // 变换链结束

        // --- 主图像 ---
        WriteEntropyCodedImage(writer, argb.data(), pixelCount, true);

        writer.Flush();

        // --- RIFF 容器 ---
        const uint32_t payloadSize = static_cast<uint32_t>(writer.Bytes.size());
        const uint32_t paddedSize = payloadSize + (payloadSize & 1u);

        OutFile.reserve(paddedSize + 20);

        AppendFourCC(OutFile, "RIFF");
        AppendUInt32LE(OutFile, 4 + 8 + paddedSize);
        AppendFourCC(OutFile, "WEBP");
        AppendFourCC(OutFile, "VP8L");
        AppendUInt32LE(OutFile, payloadSize);

        OutFile.insert(OutFile.end(), writer.Bytes.begin(), writer.Bytes.end());

        if (payloadSize & 1u)
        {
            OutFile.push_back(0);
        }

        return true;
    }
}
