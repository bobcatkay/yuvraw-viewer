---
name: yuvraw
description: YUVRaw 项目的架构、构建方式、代码约定与扩展流程。在本仓库中处理任何图像格式支持、OpenGL 渲染、ImGui 面板、加载器或 YUV/RAW/Bayer 解析相关的任务时使用；新增格式、新增 UI 面板、修改渲染管线前必读。
---

# YUVRaw 项目指南

Windows 平台的图像/YUV/RAW 查看与调试工具。OpenGL 3.3 core + Dear ImGui (docking) + GLFW + GLAD，C++17，MSVC v145。

## 按任务加载

本技能分三层：元数据用于选择技能；本文件提供共用约定与任务路由；`references/` 保存按需读取的技术细节。
先根据任务选择下表中的专题，修改相关模块前读对应文档。只有任务跨主题或需要核对关联约束时才补读其它专题；不要一次性读取整个 `references/`，也不要沿关联链接递归加载全部文档。

文档链接相对所在 Markdown 文件；构建命令在仓库根目录执行。源码路径中的 `Core/`、`Image/`、`UI/`、`gl/` 是 `src/` 下的简写，其余仓库路径从根目录定位。

| 当前任务 | 读取文档 | 主要内容 |
|---|---|---|
| 定位模块、梳理职责、调整架构 | [架构与目录](references/architecture.md) | Core / Image / UI / gl 职责、项目文件路径约定 |
| 构建、运行、发布、依赖恢复或选择回归验证 | [构建与验证](references/build-and-test.md) | Debug 方案构建、发布脚本、命令行、CPU/GPU 测试、真实素材 |
| 异步切图、后台差值/导出、卡顿或退出问题 | [异步任务与生命周期](references/async-lifecycle.md) | 请求代际、共享 Context、fence、忙碌状态、资源销毁顺序 |
| 新增格式、调整平面几何、位深或采样布局 | [格式描述与扩展](references/formats.md) | 描述表、枚举稳定性、平面尺寸、有效位与容器位深 |
| 加载器分发、打开/重载、中文路径、格式与分辨率推断 | [文件加载与参数推断](references/loading.md) | 自描述格式、失败回填、单帧语义、候选排序、UTF-8 路径 |
| 纹理上传、stride、GL 绘制回调或格式着色器 | [纹理与渲染](references/rendering.md) | 像素行长度、纹理复用、uniform、GL 状态与多视口坐标 |
| 色彩解释、传输函数、色调映射或 HDR 输出 | [色彩管线与 HDR](references/color-and-hdr.md) | CPU/GLSL 一致性、直通路径、scRGB 呈现、多视口 SDR 回退 |
| 对比模式、缩放/旋转/镜像、探针、拖放或键盘切图 | [查看器与输入](references/viewer-and-input.md) | 文档槽位、视图联动、编辑目标、坐标变换、输入派发 |
| 新增面板、Dock 布局、弹窗、主题、图标、字体或 DPI | [UI 与主题](references/ui.md) | Dock 版本与焦点、模态 ID、调色板、矢量图标、缩放 |
| 用户设置、图片缓存、预设、迁移、窗口位置或历史偏好 | [设置与缓存](references/settings-and-cache.md) | 路径 LRU、目录继承、持久化时机、清除数据与迁移标记 |
| 导出流程、重采样或 WebP 编码 | [导出与编码](references/export.md) | 内存图/批量文件入口、参数来源、输出路径、VP8L 约束 |

职责不清时先查架构文档；只验证或构建时直接读构建文档，无需先加载其它专题。

## 代码约定

- 类名 `F` 前缀，枚举 `E` 前缀；成员/函数首字母大写驼峰；bool 成员 `b` 前缀；出参 `Out` 前缀
- 头文件 `#pragma once`，资源用 `std::unique_ptr`
- 中文字符串字面量统一加 `u8` 前缀（项目开了 `/utf-8` 所以不加也能编过，但别依赖）
- ImGui 窗口标题是 DockBuilder 的 key，**改标题必须同步改 `SetupDockSpace()`**

## 验证与维护

- 根据改动选择 [构建与验证](references/build-and-test.md) 中适用的检查；其中格式描述表、色彩管线、编码器与导出流程的必跑要求仍有效。
- 代码结构、交互逻辑、构建方式或扩展流程变化时，更新对应专题；只有共用约定或任务路由变化才修改本入口。
- 新增/删除模块同步更新架构文档；格式表字段、加载器分发、渲染回调协议、DockSpace 版本与测试素材分别维护在对应专题。
- 新专题必须在上表写明读取条件。跨文件引用使用明确链接，避免依赖原文顺序的“见上节/下节”。
