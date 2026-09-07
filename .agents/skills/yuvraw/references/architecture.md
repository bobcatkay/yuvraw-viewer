# 架构与目录职责

在定位模块、梳理职责或调整模块边界时读取；无需把此目录表作为其它任务的固定前置。

## 目录与职责

```
Application.cpp            WinMain，把 lpCmdLine 交给 FApplication
src/
  Core/
    FApplication           窗口/渲染器/DockSpace 生命周期，主循环，拖放入口
    FWindow                GLFW 窗口封装 + drop callback（请求 3.3 core 上下文）
    FRenderer              ImGui 初始化、每帧 Begin/End、浅色主题、系统/随包中文字体
    FUiResources           Windows 已知字体目录、EXE 相对字体资源、本机用户 ImGui 布局路径
    FMainDockSpace         布局 + 三个文档的协调 + 快捷键 + 最近文件 + 拖放派发
    FImageConfigCache     **按规范化文件路径持久化加载/显示/视图配置**（版本化有界 LRU，默认 100 张）
    FDirectoryImagePropertyHistory **按目录与文件后缀记录本次运行中上一张主图的加载/显示属性**（不持久化）
    FImageDocument         **文件路径 + 加载参数 + 显示设置 + 解码结果 + GPU 纹理** 的聚合体
    FCommandLine           命令行解析
    FHdrDisplay            显示器 HDR 能力探测（DXGI + Win32 显示配置，与渲染 API 无关）
    FHdrPresenter          **HDR 呈现层**：fp16 FBO + WGL_NV_DX_interop2 + D3D11 交换链
    FFileDialog            Win32 IFileOpenDialog 封装（文件/目录）+ 在资源管理器中定位文件 + 用默认浏览器打开项目 Releases
    FAsyncJob              单个后台任务 + 忙碌状态（工作线程跑，主线程 Poll 收尾）
    FAsyncImageLoader      有界代际邮箱 + 后台解码 + 隐藏共享 Context 纹理上传 + fence/主线程回退
    FUserSettings          跨会话偏好（%APPDATA%\YUVRaw\settings.ini），界面语言 + 主窗口边界 + 精简主题调色板 + 上次浏览目录 + 最近打开 15 条 + 自定义图像格式预设 + 图片配置缓存容量 + 直方图显示方式 + 对比模式 + 单图对比提示次数
    FLocalization / FUiText.inl  中英文 UI 资源、运行期语言、窗口稳定标识与底层诊断文本的翻译入口
  Image/
    FImageFormat.h         EImageFormat / EColorModel / EColorSpace / EColorRange / EBayerPattern
    FDisplaySettings.h     EChannelView + FDisplaySettings（三轴色彩 + 色调映射/曝光，**按文档存**）
    FImageFormatDesc       **格式描述表** —— 整个项目的地基，详见 formats.md
    FResolutionGuess       由文件大小 + 格式反推可能的分辨率（详见 loading.md）
    FImageData             图像数据容器（尺寸/格式/stride/位深布局/像素）
    FImageLoadParams       无头格式的加载参数
    FImageLoader           加载器接口 + FImageLoaderFactory 注册表
    FRawImageLoader        **所有无头格式的通用加载器**（含 MIPI RAW10/12 解包）
    FWicImageLoader        PNG/JPEG/BMP/TIFF/GIF/HEIF（走 Windows WIC，无第三方依赖）
    FColorTransform        **整条色彩管线的唯一真值来源**：YUV 矩阵 + EOTF + 原色 + 色调映射
    FImageSampler          单像素采样（像素探针）+ 整图转 RGB8
    FImageCompare          差值图 + 最大差/平均差/差异占比/PSNR
    FImageExporter         导出：转 RGB8 -> 等比例重采样 -> 编码（PNG/JPEG/BMP 走 WIC）
    FWebpEncoder           **自研无损 WebP(VP8L) 编码器** —— Windows 只有解码器，详见 export.md
  UI/
    FMenuBar               文件/设置/帮助菜单（文件菜单留了 extras 插槽给"最近打开"）
    FFileExplorer          目录浏览 + 图像文件过滤 + Shift/Ctrl 多选 + 右键"添加为对比图/导出"
    FImageViewer           按文档槽位保存显示模式/缩放平移/镜像旋转、像素探针、并排对比、局部加载动画
    FPropertyPanel         编辑对象切换 + 格式/分辨率(含候选下拉)/stride/位深/Bayer + 色彩标准/范围/通道
    FHistogramPanel        RGB + 亮度直方图（降采样统计）
    FComparePanel          对比图选择、移除主图/对比图、差值计算、统计量、显示目标切换
    FExportPanel           导出设置的模态弹窗（格式/色彩矩阵/等比例缩放/保存目录）
    FUiIcons               ImDrawList 画的文件夹/文件/镜像/旋转/链条/并联图标 + 转圈动画 + 忙碌遮罩（字体没有 emoji，详见 ui.md）
    FUiScale               主窗口 DPI 对应的应用级 UI 比例；供自定义尺寸与 ImDrawList 绘制统一缩放
    FUiFont                用全部中英文 UI 资源与常用中文/Latin 范围构建字体图集
    FUiLayout              将旧中文窗口布局迁移到语言无关的固定 ID，保留 Dock 布局与选中标签
    FUiTheme               用六个基础色自动推导浮层、次要文字及控件状态色，并映射到 ImGui 样式
    FThemeColorPicker      设置页自绘取色器（明度/饱和度方块 + 水平色相条 + 十六进制输入 + 色块）
    FToast                 瞬时提示（"已复制到剪切板"这类回执），前景绘制列表，同时只留一条
  gl/
    FTexture               单个 GL 纹理封装（含 stride/对齐处理）
    FTextureData           按策略创建普通整图/稀疏/分块多平面纹理；预览、可见页预算和跨 Context 绘制同步
    FTextureLoadOptions    无 GL/UI 依赖的请求策略快照：稀疏开关、最长边阈值
    FSparseTexture         可选 ARB_sparse_texture 后端：虚拟页几何、提交/回收、页内 stride 重排
    FShader                着色器程序封装
    FShaderManager         按 EImageFormat 缓存着色器（单例）
    FShaders.h             全部 GLSL 源码
  Util.h/.cpp              日志宏 + 从文件名解析分辨率/格式
tests/
  TestFormatDesc.cpp       格式描述表自检
  TestColorPipeline.cpp    色彩管线自检（对着公开标准断言）
  TestWebpEncoder.cpp      无损 WebP 往返自检
  TestImageExporter.cpp    导出流水线自检
  TestAsyncImageLoadMailbox.cpp 异步请求代际/合并/取消自检
  TestUiResources.cpp       随包中文字体覆盖、中文路径、非 C 盘路径与用户布局自检
  run_ui_resource_tests.ps1 不依赖 OpenGL 的 UI 资源/布局验证
  run_tests.ps1            编译并运行
```

目录名磁盘上是小写 `gl/`，`.vcxproj` 里写的是 `src\GL\`，include 用 `"gl/FTexture.h"`（Windows 不区分大小写，别去"修正"）。

## 按需关联

- 修改格式描述表、加载参数推断时，分别读 [格式描述与扩展](formats.md)、[文件加载与参数推断](loading.md)。
- 修改编码器或图标时，分别读 [导出与编码](export.md)、[UI 与主题](ui.md)。
- 新增源码文件或核对工程收集方式时，读 [构建与验证](build-and-test.md)。
