# 面板、主题、图标与 DPI

在新增面板、调整 Dock/模态弹窗、主题、字体、矢量图标或 UI 缩放时读取。

本页内容：

- [新增 UI 面板](#新增-ui-面板)
- [主题调色板](#主题调色板)
- [图标与字体](#图标与字体)

## 新增 UI 面板

1. 在 `src/UI/` 建 `F<名字>Panel.h/.cpp`，提供 `Render()`；中英文文本登记在 `Core/FUiText.inl`，窗口用 `FLocalization::WindowTitle(EUiText::...)`
2. `FMainDockSpace` 持有 `unique_ptr` 并在 `Render()` 中调用
3. 在 `SetupDockSpace()` 中用同一 `FLocalization::WindowTitle(EUiText::...)` 调用 `DockBuilderDockWindow`
4. **递增 `kDockSpaceId` 末尾的版本号**，否则老用户的 `imgui.ini` 里没有新面板的 DockId，面板会以浮动窗口出现在屏幕中央
5. 加进 `.vcxproj`

同一个 dock 节点里的标签页有两件事互相独立，别搞混：

- **排列顺序** = 各窗口首次 `Begin()` 的顺序，由 `FMainDockSpace::Render()` 里的调用次序决定
- **默认选中哪个** = 由 `bPendingSelectPropertyPanel` 在本帧所有面板渲染后排队选择 Dock 标签。
  布局重建以及“没有对比图时打开新主图”都会置位。这里不能调用 `SetWindowFocus()`：它会把
  键盘导航焦点从文件浏览器抢到属性面板，导致点击文件后无法继续用上下键切图；只更新对应
  Dock 节点与 TabBar 的选中项，保留原 `NavWindow`。

右侧“属性面板 / 对比”两个标签的持久选中色读 `FUiTheme`，默认为 `#0076A1`。这组颜色要在各自
`ImGui::Begin()` 前局部 Push，让 ImGui 存入窗口 `DockStyle`；禁止修改 `FRenderer` 的全局 Tab 颜色，
否则文件浏览器、直方图等其它面板也会一起变色。

一次性动作（导出、设置对话框）做成**模态弹窗**而不是停靠面板：不占布局，
也不必递增 `kDockSpaceId`。参考 `FExportPanel` —— `OpenPopup` 与 `BeginPopupModal`
必须在同一个 ID 栈里，所以外部只置位 `bRequestOpen`，真正打开留到 `Render()` 里做。

帮助菜单的“使用说明”由 `FMainDockSpace::RenderUsageGuidePopup()` 显示，正文按中英文资源分节滚动，
底部关闭按钮固定，支持标题栏关闭与 Esc；内容只保留快捷键及不明显的操作方法，不放 YUV/RAW 参数、
常规菜单介绍、格式限制或排查说明。文档中的弹窗介绍与实际内容保持一致。
“关于”中的“最新版本”按钮放在版本号右侧，两者同高并作为一组居中；版本号使用主题的输入控件背景、
边框与文字色。按钮通过 `FFileDialog::OpenProjectReleases()` 将固定项目 Releases 地址交给默认浏览器，
Shell 调用失败时记录日志并显示本地化提示。

帮助菜单的“反馈”由 `FMainDockSpace::RenderFeedbackPopup()` 显示，包含当前版本、上传日志提示、
“打开日志目录”与“反馈”按钮；正文独立滚动，按钮整体右对齐并固定在弹窗右下角，
窄窗口中纵向排列并保持右对齐，支持标题栏关闭与 Esc。
`FFileDialog::OpenLogDirectory()` 从 `FLogger::GetLogDirectory()` 获取实际目录并交给资源管理器，
`OpenProjectIssue()` 用默认浏览器打开固定项目的新建 Issue 页面；失败时记录日志并显示本地化提示。
日志每条记录从 `FAppVersion::String` 获取版本，方便从滚动文件或截取片段定位构建。

面板 Begin 返回 false 时应配对 End 并跳过内容计算；属性面板仍须在原 ID 栈内维护格式预设模态弹窗。文件浏览器刷新后用已排序的原生路径二分查找选中项，每个选中路径只转换一次，保留多选导出顺序；枚举失败时清空不完整列表。

## 主题调色板

设置页只暴露六个真正需要用户选择的基础色：**高亮色、界面背景、文字、边框、输入控件背景、
按钮与选择色**。角色由 `EThemeColorRole` 稳定编号，默认值集中在 `kDefaultThemePalette`；
`FUserSettings` 沿用旧版完整调色板中对应基础角色的配置键，旧版悬停/按下等细项在读取时忽略，
下次保存时自然移除。新增或调整基础色时必须同步更新角色枚举、默认调色板、配置键表、设置页标签与测试。

`FUiTheme` 从六个基础色自动推导弹窗/菜单背景、次要文字、悬停、按下、选择项、标签页、勾选与滑块等
状态色。状态变化应朝文字色增加对比，浮层变化则依据背景亮度自动选择明暗方向；这些派生色不能重新
暴露成独立设置，否则会再次产生大量互相冲突的选项。

`FThemeColorPicker` 按设置页截图样式自绘：上方是圆角饱和度/明度方块，下方是水平彩虹色相条，
底部提供 `#RRGGBB` 输入和当前色块。右侧编辑区不重复显示当前项标题，上下内边距使用设置页的五分之一；
`CalculateFittingSize()` 按可用宽高等比计算整个取色器的尺寸并居中放置，色相条、输入框、字体、间距和标记
跟随同一比例，绘制结束后恢复局部字体与样式。
任何受支持的窗口尺寸与 DPI 下都不应依赖滚动才能操作完整。设置页左侧逐项列出六个基础色，每项都必须
有独立“重置”按钮；六行统一以色块高度作为控件行高，标签、色块与按钮垂直对齐，“重置”文字按
实际字形边界在按钮背景中居中。重置只恢复该基础色的默认值，派生状态会随之实时更新。
标题选中及悬停背景使用标题列的明确宽度，并禁用 Selectable 的半间距扩展；不绘制整行底色，
避免背景越过标题列侵入后方色块，各标题背景保持相同宽高。

拖动取色器或单项重置时只调用 `FUiTheme::SetPalette()` 做进程内实时预览，不进行磁盘 I/O；点“应用”
后由 `FUserSettings::SetThemePalette()` 一次性写入 `settings.ini`。“取消”、Esc 或弹窗关闭必须从持久设置
重载；“清除全部数据”则恢复整套默认调色板。`FRenderer` 在 DPI 缩放重建基准样式之后必须再次应用
`FUiTheme`，避免缩放变化覆盖用户颜色。

高亮色角色默认 `#0076A1`，用于右侧“属性面板 / 对比”标签、单图切换的槽位身份前缀、平铺模式的当前身份标签，
以及链条、水平并联、垂直并联的选中底色。其它角色按语义映射到全局 ImGui 样式及文件选择、忙碌指示等自绘控件；
文件浏览器中对比图的橙色、警告/错误色、直方图通道色等业务含义颜色保持固定，不能随主题改变。

## 图标与字体

### 中英文界面

`FLocalization` 用 `EUiText` 索引 `Core/FUiText.inl` 的中英文资源，普通文本调用 `Text()`，
窗口/模态标题调用 `WindowTitle()`；必须保留两种翻译的 printf 参数类型与顺序。
`###` 后的固定标识与显示语言无关，窗口 Begin、DockBuilder 与标签选中查询必须使用同一入口。
`FUiLayout` 在布局加载后、首帧前迁移旧中文窗口记录，保留位置、大小、DockId、顺序与选中标签，
已有新记录优先；语言切换不递增 DockSpace 版本。底层格式名和持久 HDR 状态在 UI 边界用 `Translate()` 解析。

设置弹窗提供“简体中文 / English”，打开时读取草稿，点击“应用”并成功保存后切换，无需重启。
取消或关闭不修改语言；仅清图片缓存保留语言，清除全部数据恢复英文。
新用户默认英文，已保存的中文选择继续生效。
主界面启动时加载语言；后台任务也可读取语言，因此运行期语言值为原子变量。
设置正文使用可滚动子区域，底部应用/取消固定，避免小屏幕上按钮被遮挡。

字体优先使用 Windows 已知字体目录内的 `msyh.ttc`（由 `SHGetKnownFolderPath(FOLDERID_Fonts)`
取得目录，禁止写死系统盘）；没有微软雅黑时使用 EXE 相对路径
`resources/fonts/NotoSansCJKsc-Regular.otf`。后者是未裁剪的 Noto Sans CJK SC Regular 2.004，
按 OFL-1.1 可随 ZIP 和源码包分发；固定来源、hash 和许可在 `resources/fonts/README.md`。
启动目录、Windows 盘符或系统是否安装中文字体都不能改变资源定位。

运行时由 `FUiFont::BuildGlyphRanges()` 使用 `GetGlyphRangesChineseSimplifiedCommon()`、Latin 默认范围和全部中英文资源字符，
避免启动时同步栅格化两万多个完整中文字形；新增 UI 文本统一登记到 `FUiText.inl`，其字形自动加入图集。
语言切换不重建图集，中文文件名仍可显示。这份图集并不保证显示所有罕用文件名汉字，即使字体文件完整。
字体**不含 emoji**（📁 在 BMP 之外，ImGui 默认的
`ImWchar` 也只有 16 位）——直接写 emoji 会画成问号。图标一律用 `FUiIcons` 里的 ImDrawList 矢量绘制。
需要单个符号时可以用 U+00FF 以内的字符（比如关闭按钮用 `×` U+00D7），那段范围字体里有。

### DPI / UI 缩放

Windows 下 GLFW 已让进程进入 DPI 感知模式，但 ImGui 不会自动放大字体与样式。
`FRenderer` 因此在每帧 `NewFrame` **之前**查询主窗口的 `glfwGetWindowContentScale()`：

- 启动时按 `18px × 内容缩放`直接栅格化字体；主窗口跨到另一 DPI 显示器时销毁并重建字体图集
- 样式每次都从未缩放的 `BaseImGuiStyle` 重新 `ScaleAllSizes()`，禁止在当前样式上连续乘比例，
  否则窗口往返 100% / 150% 显示器会积累浮点和取整误差
- 字体图集只能在主 GL Context 当前且 ImGui 帧尚未开始时重建；不能放进逐窗口 Render、
  GLFW 回调或 `NewFrame` / `EndFrame` 之间
- ImGui 自带的 padding / spacing / rounding 已由样式缩放；项目自己写的固定宽高、留白、线宽和
  ImDrawList 几何则必须通过 `FUiScale::Apply()`。比例、颜色、秒数、图片坐标和业务数值不能缩放

ImGui 的字体图集和样式是全局资源，所以当前应用级比例跟随**主窗口**所在显示器；副视口共享它。
图像的 1:1 模式仍单独按 `FramebufferScale` 做像素映射，不能把 UI 比例混进图像缩放。

`ImGuiConfigFlags_NavEnableGamepad` **不要开启**：工具没有手柄交互，而 GLFW 会在首帧同步
初始化 Windows 游戏控制器后端。机器上存在失效或休眠 HID 设备时，这次查询可能阻塞 UI 线程
十几秒，表现为主窗口已经出现但内容全空、点击后“未响应”。键盘导航保持开启即可。

**查看器工具栏图标不依赖位图图集。** 应用窗口图标仍由 `resources/AppIcon.ico` 和 RC 资源提供。
曾经把查看器图标做成内嵌 PNG（WIC 解码 + GL 纹理），
缩到工具栏的 19px 后 mipmap 把细节糊成灰点，还带来了 COM/WIC/GL 生命周期这一串负担。
现在 `DrawViewerGlyph()` 按当前像素密度现画，任意尺寸都锐利，也不需要在 GL 上下文销毁前释放任何东西。

改这几个图标时守住三条：

- **线宽统一**（各图标的 stroke ratio，另有像素下限）。同族图标只有形状不同，线宽一变，
  工具栏上一排按钮的光学重量就参差不齐
- **一份几何两种朝向**。水平/垂直镜像只换一组坐标基，顺/逆时针只镜像 X，
  水平/垂直并联只互换宽高 —— 各写一份必然慢慢长歪
- **细节按尺寸分层**。并联图框里的图片符号只在内腔够宽时才画（`kTileMotifMinInteriorPixels`）；
  19px 下框内只剩 4px，太阳和山峰糊成一团，反而看不出是两幅并排的图。
  同理两框之间只留缝、不画分隔线：缝隙里再塞一条线就成了三条并排的边

验证只能靠看：改完跑起来截屏，把按钮区域放大 6-10 倍核对。
临时往前景绘制列表上按 19/32/64px 画一排图标是最快的办法（核对完记得删掉）。

`tests/run_ui_resource_tests.ps1` 独立验证完整中文字体的字形覆盖及图集构建、中文资源路径、
两套实际字体的中英文 UI 字形覆盖、格式化参数一致性、窗口 ID 稳定性与旧布局迁移、
非 C 盘资源路径组合、与工作目录无关的用户布局、清除后退出不重新创建布局；它不需要 OpenGL。
这不替代真实英文 Windows 或非 C 系统盘的整机验收。

## 按需关联

- 涉及旧品牌设置、Dock ID 兼容、清除数据或窗口边界持久化时，读 [设置与缓存](settings-and-cache.md)。
- 涉及文档编辑目标、对比模式或键盘导航时，读 [查看器与输入](viewer-and-input.md)。
- 新增 .cpp/.h 的工程登记和验证方式见 [构建与验证](build-and-test.md)。
