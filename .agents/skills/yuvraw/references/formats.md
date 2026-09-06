# 格式描述、扩展与采样布局

在新增图像格式、调整格式表、平面几何、位深、字节序或有效位对齐时读取。

本页内容：

- [格式描述表：本项目的核心抽象](#格式描述表本项目的核心抽象)
- [位深与采样布局](#位深与采样布局)
- [平面尺寸边界](#平面尺寸边界)

## 格式描述表：本项目的核心抽象

`Image/FImageFormatDesc.h` 的 `FFormatDesc` + `FPlaneDesc` 一张表驱动**五处**：

1. 加载器的帧大小计算与平面偏移（`FRawImageLoader`）
2. 建纹理（`FTextureData` —— 循环平面，没有任何 `switch (format)`）
3. 着色器变体选择（`FShaders::GetFragmentShaderForFormat`）
4. 属性面板下拉框（由 `GetDisplayOrder()` 生成，显示顺序不改变枚举持久化值）
5. 像素探针与直方图的分量解析（`FImageSampler`）

`FPlaneDesc` 用移位描述降采样：平面宽 = `ceil(Width >> WidthShift)`。`StrideDivisor` 描述该平面每行字节数相对基准 stride 的比例（半平面 UV 与 Y 等宽 → 1；平面格式 U/V 减半 → 2）。

有效位深、字节序与有效位对齐同样由描述表声明：`EffectiveBitDepth`、`StorageLayout` 分别给出
`Fixed / Configurable / NotApplicable` 策略，属性面板、预设校验和加载器都必须通过
`ResolveBitDepth()` / `ResolveByteOrder()` / `ResolveSampleAlignment()` 消费，禁止再按具体格式写 UI 特判。
`YUV420SP16` 是通用 16bit 半平面 4:2:0 容器，允许配置 8–16bit、小/大端和低/高位对齐；
标准 `P016` 仅作为兼容输入别名映射到该容器，未指定参数时使用 16bit、小端默认值。
`P010` 仍严格保持标准的小端、10bit 高位对齐语义。

**表的顺序必须与 `EImageFormat` 枚举严格一致** —— `Get()` 直接按下标取，`tests/TestFormatDesc.cpp` 会校验这一点。

面向用户的格式下拉顺序走 `GetDisplayOrder()`：`Unknown` 在首项，RGB 格式按位深集中展示，
其余格式保持描述表相对顺序。新增格式仍然只追加枚举和描述表，禁止为了 UI 排序重排二者。

### 新增图像格式

绝大多数情况**只需要两步**：

1. 在 `EImageFormat`（`Image/FImageFormat.h`）**末尾**追加枚举值（绝不插在中间）
2. 在 `FImageFormatDesc.cpp` 的表里对应位置加一行 `Add(...)`

然后跑 `tests/run_tests.ps1`。只有当这个格式需要新的着色器族（现有的有：半平面 YUV / 平面 YUV / packed YUV / Bayer / 灰度 / RGB）时，才需要在 `FShaders.h` 加着色器并在 `GetFragmentShaderForFormat` 里分支。

## 位深与采样布局

相机 RAW 极常见的情况：10/12/14bit 数据装在 16bit 容器里。只知道容器宽度不足以正确显示。
`FImageData` 因此有两个可覆盖的字段（`SetSampleLayout`）：

- `SourceBitDepth` —— 采样值真实位深
- `SampleShift` —— 采样值在容器内左移的位数

`GetSampleScale()` 据此算出着色器要乘的系数 `containerMax / (sourceMax << SampleShift)`：

| 情况 | SourceBitDepth | SampleShift | 系数 |
|---|---|---|---|
| P010（10bit 在 16bit **高位**） | 10 | 6 | 65535/65472 |
| YUV420SP16（10bit 在 16bit **低位**） | 10 | 0 | 65535/1023 |
| YUV420SP16（10bit 在 16bit **高位**） | 10 | 6 | 65535/65472 |
| MIPI RAW10 解包后（低位对齐） | 10 | 0 | 65535/1023 |
| 真 16bit | 16 | 0 | 1 |

漏掉这个，10bit 低位对齐的数据按 16bit 归一化会几乎全黑（1023/65535 ≈ 1.6%）。

## 平面尺寸边界

- **色度平面尺寸用 `(w+1)/2` 向上取整**，别用 `w/2`（奇数宽会少一列）。

## 按需关联

- 涉及文件分发、RAW 读取或分辨率推断时，读 [文件加载与参数推断](loading.md)。
- 需要新的着色器族或改动纹理上传时，读 [纹理与渲染](rendering.md)；涉及色彩解释时补读 [色彩管线与 HDR](color-and-hdr.md)。
- 格式变更的必跑回归与 GPU 格式验证见 [构建与验证](build-and-test.md)。
