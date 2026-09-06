# 异步任务与退出生命周期

在修改异步加载、后台差值/导出、GL 资源所有权，或排查卡顿与退出问题时读取。

本页内容：

- [异步切图与共享 Context](#异步切图与共享-context)
- [退出生命周期](#退出生命周期)
- [后台任务与忙碌遮罩](#后台任务与忙碌遮罩)

## 异步切图与共享 Context

主图、对比图和属性面板触发的重新加载都走 `FAsyncImageLoader`，不要在 ImGui 回调里直接
调用 `FImageDocument::Open/Reload`。后两者只保留为同步兼容入口，并复用同一套解析规则。

固定流程与约束：

1. `FMainDockSpace` 在主线程拍下路径、候选加载参数、显示/视图配置和目标槽位；工作线程
   **不能访问文档或面板**。
2. 异步加载器最多保留一个执行中的请求和一个最新待处理请求；新请求覆盖尚未执行的待处理请求。新请求提升代际，旧请求即使已开始解码
   也不得上传或提交，避免连续按方向键时积压几十张大图。
3. 专用线程先将图像解码为内存中的像素数据；隐藏 GLFW 窗口与主窗口共享 OpenGL share group，成功时在该
   Context 上新建一套纹理。绝不能原地修改仍在渲染的旧纹理。
4. 本项目 GLAD 使用进程级函数表。隐藏 Context 必须逐项确认上传/同步函数地址与主 Context
   的 GLAD 表一致；不一致或隐藏 Context 创建失败时，安全降级为“后台解码 + 主线程上传”。
5. 后台上传后执行 `glFenceSync`，紧接 `glFlush`；主线程 `Poll` 用零超时
   `glClientWaitSync`，未就绪就留到下一帧，不能阻塞等待。fence 创建失败只允许在工作线程
   `glFinish`，fence 等待失败则丢弃后台纹理并在主 Context 重建。
6. CPU 像素和 GPU 纹理由 `FImageDocument::CommitLoadResult` 在主线程同一临界点换代，
   `OnChanged` 只触发一次。加载期间继续显示旧图，`FImageViewer` 只在自身画布叠加动画；
   文件浏览器和“继续打开”始终可交互。
7. 差值/导出的 `FAsyncJob` 会直接读取文档像素。它运行时允许图片线程继续准备，但主线程
   必须推迟提交，直到 `FAsyncJob::Poll` 完成。主图尚未准备时的对比图请求则保留一个最新
   延后意图，不能反过来取消主图（命令行 `主图 + --compare` 依赖此规则）。
8. 每次完成日志记录 queue / inspect / decode / worker upload / main fallback / commit / total；
   直方图只对超过一帧预算的刷新做限频日志。排查卡顿先看阶段数据，不要猜。
9. 隐藏上传 Context 不能在工作线程整个生命周期里保持 current。CPU 解码且确认请求仍有效后，
   才在覆盖纹理上传、fence 和失败/过期资源回收的 RAII 作用域内绑定，离开本轮任务前立即解绑；
   条件变量等待与下一轮解码期间不得占用它。实测 Intel UHD 770 在同一 share group 的两个
   Context 长期分别 current 于两个线程时，主线程可能卡死在 `wglMakeCurrent`。同理，ImGui
   副视口渲染后只有当前 Context 确实改变时才恢复，禁止每帧无条件重复绑定主 Context。

`FAsyncImageLoader::Shutdown()` 必须在 LoaderFactory、文档和主 Context 仍有效时执行：先提升
取消代际、唤醒并 join 工作线程，再在主 Context 删除待提交纹理/fence，最后销毁隐藏窗口。
任何共享 GL 资源都必须遵守这个所有权顺序。

## 退出生命周期

`FApplication`、`FImageLoaderFactory` 与 `FShaderManager` 都包含函数内静态对象，
**不能依赖进程退出时的静态逆序析构完成清理**。加载器和着色器通常晚于
`FApplication` 创建，因而会先于它析构；若这时才销毁 DockSpace，文档配置归档里的
`IsSelfDescribing()` 会访问已经析构的加载器容器，表现为窗口关闭后进程长期不退出。

`wWinMain` 必须在返回前显式调用 `FApplication::Shutdown()`，顺序固定为：

1. 销毁 `FMainDockSpace`（先停止 `FAsyncImageLoader`，再归档文档配置；此时加载器与 GL Context 仍有效）
2. 清空 `FShaderManager`（此时 OpenGL 上下文仍有效）
3. 清空 `FImageLoaderFactory`
4. 销毁 `FRenderer`（HDR / ImGui / GL 资源）
5. 销毁 `FWindow`（GLFW / OpenGL 上下文）
6. 写完退出日志后关闭 `FLogger`

关闭阶段日志会记录上述分段累计耗时。改动任何全局单例或 GL 资源所有权时，必须同步核对这条顺序。

FRenderer 的初始化失败路径也必须清理已创建的 ImGui Context 和后端。Shutdown 根据两个 BackendUserData 是否存在分别释放后端，不能仅检查最终的 bIsInitialized；io.IniFilename 借用的路径字符串必须等 Context 销毁后才能清空。

## 后台任务与忙碌遮罩

**计算差值和导出跑在工作线程上**，由 `FAsyncJob`（`Core/FAsyncJob.h`）承载：
`Start()` 起一个 `std::async`，主线程每帧 `Poll()`，完成时在主线程执行收尾。
同一时刻只允许一个任务 —— 发起时整个界面就被置灰了，不可能并发。

以前这两件事在主线程同步做，大图上界面卡死几秒；转圈动画也救不了，因为动画本身
也得靠主循环出帧。

`FMainDockSpace::Render()` 里的三件事必须一起看：

1. `AsyncJob.Poll()` 放在**出帧最前面**。收尾要建纹理（GL 上下文）、要改面板状态，
   只能在主线程做，工作线程里一律不碰 GL / ImGui / 任何 UI 对象
2. `ImGui::BeginDisabled(bBusy)` 罩住全部面板。置灰状态是 ImGui 的**全局**栈
   （`g.CurrentItemFlags`），`Begin()` 不会重置它，所以一次调用就够
3. 忙碌时跳过 `HandleShortcuts()`，`HandleDroppedPaths()` 也直接丢弃 ——
   这两条不走控件，置灰拦不住

**置灰不只是视觉反馈，也是一条安全边界**：工作线程直接读着文档里的像素
（`FExportJob::LoadedImage` 就是裸指针，不拷贝），此刻换帧 / 改 stride / 打开新文件
都会把那块内存换掉。要放开界面就得先改成拷贝一份再交给线程。

`FAsyncJob` 必须是 `FMainDockSpace` 的**最后一个成员** —— 成员按声明逆序析构，
它得先于几个文档销毁，因为它的析构函数会等工作线程结束。

遮罩由 `FUiIcons::DrawBusyOverlay()` 画在**主视口的前景绘制列表**上，不开窗口：
开窗口就要处理焦点、输入捕获和 DockSpace 的交互，而"挡输入"已经由 BeginDisabled 负责。
画在前景列表还有个好处 —— 不受那层置灰的透明度影响。

导出期间用户仍然能用 Esc 关掉模态弹窗（ImGui 内部处理，不受置灰影响），
所以 `FExportPanel::SetResult()` 发现弹窗已关就把它重新弹出来，否则结果和那几个
可点的文件名就没地方显示了。

## 按需关联

- 调整解码规则、加载候选时，读 [文件加载与参数推断](loading.md)；调整缓存归档与目录继承时，读 [设置与缓存](settings-and-cache.md)。
- 调整纹理上传或渲染回调时，读 [纹理与渲染](rendering.md)。
