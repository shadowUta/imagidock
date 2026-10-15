# 创想坞 · ImagiDock

Windows 原生桌面素材库。C++20、Qt 6 Widgets、Filament PBR 预览、SQLite 和 Zstandard。

本仓库正在完成首版实现和验收。验收记录见 [VALIDATION.md](docs/VALIDATION.md)；尚未验证的内容不视为完整首版交付。

## 使用

便携包解压到可写目录后运行 `ImagiDock.exe`。首次启动在程序旁的 `data/library` 建立素材库。可通过 `--library <目录>` 打开另一个库。

- 导入文件或文件夹，默认仅索引；勾选备份后在后台压缩保存原文件及模型依赖。
- 单击看右栏，双击进入详情。Esc 返回上一层关联；主页清空当前页签的详情历史。
- 图片缩放平移、GLB/glTF/OBJ/FBX 实时 3D、音视频播放，以及 PSD/PSB、SAI/SAI2 合成或内嵌预览。
- 详情管理关联、预览图、材质贴图和封面。材质覆盖只影响库内预览。
- 页签可复制、拆成独立窗口并拖回；窗口共享同一个库服务。
- 错误中心处理来源缺失、过期备份、预览失败和材质槽变化；删除仅进入库内回收站。

详情见 [使用说明](docs/USER_GUIDE.md)。MCP 和专用 Skill 分别位于 `mcp/` 和 `.agents/skills/imagidock-assets/`；不自动安装任何全局配置。

## 构建

开发依赖与缓存必须保留在工程目录内。需要 Windows x64、MSVC C++ 工具、CMake/Ninja、Python 3.11+ 和 Node.js。

1. `python scripts/bootstrap.py` 下载并校验已锁定的原生依赖。
2. 通过 `scripts/bootstrap-qt.ps1` 安装 Qt 6.8.3（MSVC x64），通过适配器文档准备 Python/Node 依赖。
3. `powershell -ExecutionPolicy Bypass -File scripts/build.ps1` 编译 Release。
4. `scripts/package.ps1` 组装便携目录和 ZIP。

开发源文件不包含测试艺术资产、素材库、备份、缓存和个人路径。测试仅使用归档的只读输入或工程内测试副本。

## 许可

自有代码暂不授予开源许可，见 [LICENSE.txt](LICENSE.txt)。依赖遵循各自许可，见 [第三方声明](THIRD_PARTY_NOTICES.md)。
