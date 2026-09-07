# 用户设置与图片配置缓存

在修改用户设置、图片缓存、目录属性继承、自定义预设、迁移/清除数据或历史偏好时读取。

本页内容：

- [用户数据迁移](#用户数据迁移)
- [图片配置缓存](#图片配置缓存)
- [自定义图像格式预设](#自定义图像格式预设)
- [主窗口位置与大小](#主窗口位置与大小)
- [直方图显示偏好](#直方图显示偏好)
- [最近打开](#最近打开)

## 用户数据迁移

纹理策略保存为 `SparseTexturesEnabled=1/0` 和 `SparseTextureDimensionThreshold=<像素>`，
缺失默认启用、16384；非法值忽略，正数阈值限制在 1–65535。设置弹窗只编辑草稿，应用时
`SetTextureLoadOptions()` 原子保存两项，失败回滚内存值；取消不生效，清除全部数据恢复默认。
应用影响下一次打开或重新加载，已提交请求与现有图像不切换后端。后台不能访问设置存储，
`FAsyncImageLoader::Submit` / 同步解码入口在主线程快照，主线程上传回退使用结果里的同一快照。
具体回退顺序见 [纹理与渲染](rendering.md#大图预览与稀疏页面)。

界面语言用 `Language=zh-CN` 或 `Language=en-US` 保存在 `settings.ini`。
缺失或未知值回退英文，已有 zh-CN 选择不受默认值变化影响；`SetLanguage()` 保存失败时回滚内存值并返回 false。
设置弹窗仅在应用成功后调用 `FLocalization::SetLanguage()`，取消不改变语言；
“清除全部数据”同时重置持久偏好、语言草稿与当前 UI 语言。语言跨进程恢复、非法值、
锁定设置文件导致保存失败和清除行为由 `TestUserSettings` 覆盖。

品牌名为 `YUVRaw`，仓库名为 `yuvraw-viewer`。`FUserSettings` 首次启动时把旧
`ImageDevTool` 设置及缓存复制到新目录，已有新文件优先；迁移标记
`legacy-migration.complete` 在清除全部数据时必须保留，防止再次导入旧数据。
`kDockSpaceId` 保留旧内部 ID 以兼容 `imgui.ini`，不是遗漏的品牌名。
这部分行为由 `tests/TestUserDataMigration.cpp` 跨进程回归验证。

ImGui 布局独立保存在 `%LOCALAPPDATA%\YUVRaw\imgui.ini`，由 `FUiResources` 定位，
`FRenderer` 在首个 `NewFrame` 之前绑定 `io.IniFilename`。字符串由 Renderer 持有至
`DestroyContext` 后；禁止把局部字符串指针交给 ImGui。无有效绝对 `LOCALAPPDATA` 时
改查 Windows 已知本机用户目录，不回退到当前工作目录或 EXE 目录；目录创建失败时记录警告并
禁用布局持久化。“清除全部数据”删除该文件后须把 `io.IniFilename` 置空，防止退出自动重建。
旧启动目录内的个人 `imgui.ini` 不会自动导入。

## 图片配置缓存

`FImageDocument` 的主图/对比图对象会被反复用于打开不同文件，所以只按文档指针保存状态会在切图后泄漏。
`FMainDockSpace` 在覆盖路径或清空文档前，把 `FImageLoadParams`、`FDisplaySettings` 和
`FImageViewer::GetViewSettings()` 的结果合成 `FImageConfiguration`，写入 `FImageConfigCache`。

- key 是 `weakly_canonical` 后的 UTF-8 文件路径；计算产物（如 `<差值图>`）不缓存
- 缓存是 `list + unordered_map` 的有界 LRU，查找/写入均摊 O(1)，默认 100 张
- 缓存只在打开/切换/清空图片时访问，**禁止在逐帧 Render 路径里按路径查表**
- 主图打开顺序是：当前文件的按路径缓存 > 本次运行中该目录同后缀上一张主图的属性 > 自动识别。
  目录继承只复制加载参数与显示设置，缩放/平移/朝向仍从默认视图开始；继承前必须用
  `FResolutionGuess::Matches()` 确认按上一图参数计算出的帧大小与目标文件大小完全一致。
  大小不匹配、该目录此后缀第一次打开、同后缀上一张图/目标图是自描述格式，或继承后仍解码失败时，
  一律回到 `FImageDocument::Open()` 的现有自动识别逻辑。目录历史只在本次运行有效，
  与持久缓存容量无关；记录按规范化目录与 ASCII 小写文件后缀分桶，`.raw` / `.RAW` 共享记录，
  `.raw` / `.yuv` 互不覆盖。文件浏览器右键“添加为对比图”和 `--compare` 仍沿用主图加载参数，
  方便比较同批 dump。
  对比面板的文件选择器若选中当前浏览目录之外的文件，则优先按所选文件的文件头/文件名
  推断参数，无法推断时才回退主图参数；同目录选择继续沿用主图参数
- 容量由 `FUserSettings` 跨会话保存，0 表示禁用；缓存内容写入
  `%LOCALAPPDATA%\YUVRaw\image-properties.cache`
- 缓存文件有 magic/version/字段校验，并通过同目录临时文件原子替换；损坏或版本不兼容时忽略，
  不能用坏数据覆盖仍有效的内存缓存
- 只有当前路径最近一次加载成功且参数有效时才允许归档；失败请求虽然会把尝试参数回填到文档，
  但不得写入按文件缓存、覆盖或清除目录历史。已有文件缓存也只是第一候选，实际解码失败后必须在
  同一个异步请求内继续尝试兼容的目录历史和自动识别
- 启动只读一次，正常退出前归档当前主图/对比图后写一次；清除缓存、修改容量会立即写盘。
  **切图和逐帧 Render 都不做磁盘 I/O**
- “设置”模态弹窗可修改容量；“清除数据”默认只清图片属性缓存，取消勾选后同时清除
  `settings.ini`、图片属性缓存、运行期目录继承记录、最近文件与 `imgui.ini` 布局数据，
  并恢复默认偏好。缩容必须立即淘汰最久未使用项

## 自定义图像格式预设

属性面板“图像格式”右侧的“保存”按钮把当前完整 `FImageLoadParams`（基础格式、宽高、stride、
位深、Bayer 排布、字节序与有效位对齐）拍成一份用户命名的预设。它只是现有格式与加载参数的快捷方式，
**不是新的 `EImageFormat` / `FFormatDesc`**；纹理、着色器与平面布局仍由预设中的基础格式驱动。

- 预设由 `FUserSettings` 写入 `%APPDATA%\YUVRaw\settings.ini`，名称按 UTF-8 安全编码；
  保存是低频动作，可以立即落盘，逐帧只读内存列表
- 同名保存覆盖旧内容并移到最前；下拉框固定按 **Unknown → 自定义预设（新保存优先）→ 内建格式**
  排列，不能为了插入预设修改格式枚举或 `GetDisplayOrder()`。下拉框最多显示 16 行（ImGui 默认
  约 8 行的两倍），高度按当前字体、间距与窗口内边距计算，不能写死像素值
- 选择预设时先一次性 `SetLoadParams()`，再走单独的预设应用回调触发一次
  `FMainDockSpace::SubmitPanelToDocument()`；禁止逐字段触发回调，否则会连续排队多次异步重载
- 预设不保存 `FDisplaySettings`：色彩矩阵、原色、传输函数、范围等仍是当前文档独立的显示解释
- 弹窗遵循其它模态的主视口居中、DPI 缩放、工作区边距、圆角与右下操作区约定；它不新增 Dock 面板，
  因此无需递增 `kDockSpaceId`
- “仅清除图片属性缓存”保留预设；“清除全部数据”删除 `settings.ini`，因此同时清空预设

## 主窗口位置与大小

主窗口最后一次普通状态下的位置、大小和退出时的最大化状态由 `FUserSettings` 写入
`settings.ini`。`FWindow` 通过 GLFW 的位置/尺寸/最大化回调持续保留普通窗口边界：最小化或最大化
不能用系统临时矩形覆盖它；恢复时把边界约束到当前仍存在的显示器工作区，避免拔掉副屏后窗口留在屏外。
GLFW 的窗口位置与尺寸描述的是**客户区**，不是含标题栏的整个窗口；隐藏窗口移到目标显示器后必须用
`glfwGetWindowFrameSize()` 取得该显示器 DPI 下的四边 frame，再由 `FWindowPlacement` 从工作区扣除
标题栏和边框。首次启动的默认尺寸也走同一套约束，否则 1920×1080 客户区会在 1080p 设备上把
原生标题栏和右上角窗口按钮推到屏幕外。屏幕工作区、客户区位置与 frame 已使用同一套 GLFW
screen coordinates，不能再乘 `FUiScale`。
“清除全部数据”后的同一次退出必须跳过窗口边界保存，否则会立即重建刚删除的设置文件。

## 直方图显示偏好

“叠加显示”和“对数刻度”是全局用户习惯，不随图片切换，分别由
`FUserSettings::Get/SetHistogramOverlayEnabled()` 与 `Get/SetHistogramLogScaleEnabled()`
写入 `settings.ini`。`FHistogramPanel` 构造时恢复，复选框实际变化时立即保存；禁止逐帧写盘。
默认值为叠加关闭、对数关闭；已经写入配置的用户选择优先于默认值。
非叠加模式的固定顺序是 **亮度、R、G、B**，让综合亮度分布始终位于最上方。

## 最近打开

`FMainDockSpace` 只在主图异步加载成功提交后调用 `AddRecentFile()`；失败请求、目录切换和对比图
都不能进入历史。列表按最近优先去重，容量统一使用
`FUserSettings::kMaximumRecentFileCount`（当前 15 条）。

- 启动时从 `%APPDATA%\YUVRaw\settings.ini` 恢复重复的 `RecentFile=` 项
- 成功打开主图或点击“清空列表”后立即写盘；逐帧 `Render()` 不做磁盘 I/O
- 持久层会再次过滤空路径、重复项和超过上限的旧项，避免手工编辑或旧版本数据破坏约束

## 按需关联

- 主题的持久化与预览/取消约束见 [UI 与主题](ui.md)；修改调色板时读取。
- 对比模式与提示次数的持久化、文档编辑目标见 [查看器与输入](viewer-and-input.md)；修改相关设置时读取。
- 修改候选参数的实际解码规则时，读 [文件加载与参数推断](loading.md)；修改退出归档时序时，读 [异步任务与生命周期](async-lifecycle.md)。
