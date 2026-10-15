# 第三方组件

ImagiDock 自有代码声明不覆盖第三方组件。下列组件分别保留原版权与许可。便携包中的 `licenses/` 提供许可文本及 Qt SPDX 清单；下载来源和校验值见 `docs/*lock.json`。

| 组件 | 版本 | 许可 / 来源 |
|---|---|---|
| Qt Core / Gui / Widgets / Network / Concurrent / Multimedia / MultimediaWidgets / ImageFormats | 6.8.3 | LGPL-3.0，动态链接；[Qt 源码](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/) |
| FFmpeg（Qt 多媒体后端） | 以所附 Qt SPDX 与二进制配置为准 | LGPL-2.1-or-later；[Qt Multimedia 源码](https://code.qt.io/cgit/qt/qtmultimedia.git/tree/?h=v6.8.3) |
| Filament / gltfio | 1.78.0 | Apache-2.0；[源代码](https://github.com/google/filament/tree/v1.78.0)，包含第三方子组件各自许可 |
| Assimp | 5.4.3 | BSD-3-Clause；[源代码](https://github.com/assimp/assimp/tree/v5.4.3)，含 zlib 等依赖声明 |
| SQLite | 3.50.4 | Public domain；[来源](https://sqlite.org/2025/sqlite-amalgamation-3500400.zip) |
| Zstandard | 1.5.7 | BSD-3-Clause；[源代码](https://github.com/facebook/zstd/tree/v1.5.7) |
| libsai | f07ee8878e184eeeded79a5a2ef808bc51a3a7f7 | MIT，见 helpers/licenses |
| sai2-parser | 2.0.0 | MIT，见 helpers/licenses |
| Python / Pillow / psd-tools / numpy / psutil | 锁定清单 | 各自 PSF / HPND / MIT / BSD 许可，见适配器声明 |
| Node.js / 官方 MCP TypeScript SDK | 锁定清单 | MIT 及捆绑依赖许可 |

Qt 与 FFmpeg DLL 未作私有修改，可替换为 ABI 兼容版本以调试库的修改。应用的自有许可不限制相关 LGPL 权利。发布包不包含开发测试使用的 imageio-ffmpeg GPL 命令行程序。

Microsoft Visual C++ 运行库依 Microsoft Visual Studio 可再分发条款提供。

独立格式助手的详细信息见 [helpers/ADAPTER_NOTICES.md](helpers/ADAPTER_NOTICES.md)。
