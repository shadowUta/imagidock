# 创想坞 · ImagiDock

面向 2D、3D 和音视频创作素材的 Windows 桌面资产管理器。主程序采用 C++20、Qt 6 Widgets、Filament PBR、SQLite 和 Zstandard。

**当前为 v0.1.0-rc.1 发布候选，不是完整首版交付。** 圆角无边框窗口、统一玻璃弹窗及四主题已通过功能和视觉检查；独立干净系统、Windows 10 及更广泛源文件兼容性尚未完成验证。完整范围与性能数据见 [验收记录](docs/VALIDATION.md)，版本变化见 [发布说明](docs/RELEASE_NOTES.md)。

本地 UI 修订 `0.1.0-rc.1+linear.20261020` 使用 UI UX Pro Max Skill，参考 [Linear 官方面板结构](https://linear.app/now/behind-the-latest-design-refresh) 调整紧凑页签、侧栏、位置栏、视图栏与信息检查器。保留原有四套主题颜色、晕染玻璃背景、小圆角和顶部拖动。验证范围见 [UI 记录](docs/UI_REFINEMENT.md)，已发布 RC ZIP 保留原版。

## 开始使用

将便携包解压到可写目录，运行 `ImagiDock.exe`。默认素材库位于程序旁的 `data/library`；“文件 → 打开 / 新建素材库”或 `--library <目录>` 可选择其他库。包内带有运行所需的 Qt、Python 和 Node 组件；Blender、Aseprite 属于可选的用户工具。

本工程的本地更新使用根目录 `启动创想坞.cmd`，优先运行同目录依赖下的 `ImagiDock-linear.exe`，保留既有默认素材库。完成构建后可用 `scripts/update-local-ui.ps1 -SkipBuild` 更新该入口的程序。

- 递归导入文件夹或拖入文件，保持库内目录结构。默认只索引来源，可单独或批量后台备份。
- 网格卡片单击查看右侧信息、双击进入详情；图片完整显示并支持缩放，模型实时旋转、材质覆盖和动画，音视频播放与定位。
- 支持 PSD/PSB 保存合成图、SAI 内嵌预览及 SAI2 实验合成解码。上述图像也可绑定模型材质；源格式能力和降级规则见 [使用说明](docs/USER_GUIDE.md)。
- 管理关联资产、多个预览图、封面与可扩展材质槽；Esc 沿关联历史返回，主页清空当前页签的历史。
- 页签可复制、拆成独立窗口、拖回同库窗口；共享库数据与后台任务。
- 压缩备份、依赖恢复、单项/批量/目录取用、独立库包导入导出、回收站与错误中心。
- 四种主题：暖白、石墨黑、深海蓝、冰青。保留半透明玻璃面板和柔和晕染背景；透明贴图直接显示在预览背景上，不加棋盘格。

专用 [MCP 服务](mcp/README.md) 提供 36 个工具，项目内 [imagidock-assets Skill](.agents/skills/imagidock-assets/SKILL.md) 描述检索、整理、贴图、备份及任务跟踪流程。不修改全局客户端配置。

## 验证状态

已有真实 C++ 库服务、Filament 渲染、格式解码、MCP 与多窗口测试。核心 QtTest 10 个结果、格式测试 10 项、源格式材质测试 3 项、MCP SDK 测试 2 项及真实 MCP 服务 12 项断言通过。最新 Linear 布局在两档缩放下各通过 89 项功能断言；四项桌面模型画面比对因当前桌面捕获无新帧而待验收，不计为通过。已有本机隔离开发环境的便携检查；尚未在独立无开发环境的 Windows 系统上验收。

一万条合成记录的三个查询场景热缓存 P95 为 16.893–23.200 ms；最终 1080p 石墨黑网格的首屏 28 张缓存缩略图为 119.380 ms。十万条记录标签并集 P95 为 131.530 ms。测量口径、四主题基线、绘制耗时及未覆盖范围见 [性能与限制](docs/VALIDATION.md#性能记录)。

## 构建与发布

开发依赖、下载、缓存、测试副本和产物全部保留在本工程内。需要 Windows x64、MSVC C++ 工具、CMake/Ninja、Python 3.11 和 Node.js。

1. `python scripts/bootstrap.py` 下载并校验锁定的原生依赖。
2. 运行 `scripts/bootstrap-qt.ps1` 准备 Qt 6.8.3（MSVC x64），使用 Python 3.11 运行 `scripts/bootstrap-adapters.py` 准备锁定的格式助手、Python/Node 与 SAI 依赖。
3. `powershell -ExecutionPolicy Bypass -File scripts/build.ps1` 编译 Release。
4. `scripts/package.ps1` 组装便携目录；`scripts/package-licenses.py` 收集原始声明和对应源码；`scripts/finalize-package.py` 生成应用 ZIP 与 SHA-256 清单。

发布时须同时提供应用 ZIP、校验文件与独立第三方源码 ZIP。源码重建及 LGPL 库替换说明见 [第三方构建文档](docs/third-party-build.md)。

公开内容排除真实归档美术、测试截图、个人路径、本地库、备份和缓存。归档仅作为只读测试输入，破坏性故障测试使用工程内副本。

## 许可

自有代码暂不授予开源许可，见 [LICENSE.txt](LICENSE.txt)。第三方组件分别遵循原许可，见 [第三方声明](THIRD_PARTY_NOTICES.md)；自有代码声明不限制 Qt、FFmpeg 等组件赋予的 LGPL 权利。
