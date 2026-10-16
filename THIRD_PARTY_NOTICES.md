# 第三方组件

ImagiDock 自有代码声明不覆盖第三方组件。下列组件分别保留原版权与许可。便携包中的 `licenses/` 提供原始许可、版权声明、Qt SPDX 清单及实际 DLL 构建配置；下载来源和校验值见 `docs/license-sources.lock.json` 与其他依赖锁。`native-notice-inventory.json` 逐项记录原生声明文件及哈希。

发行页与应用 ZIP 同时提供 **ImagiDock-0.1.0-third-party-sources.zip**，包含以下原生库对应的完整官方源码归档及构建信息。下载来源为 [ImagiDock Releases](https://github.com/shadowUta/imagidock/releases)。打包者必须将源码 ZIP 和旁边的 SHA-256 JSON 一并发布，不以仅链接上游代码仓库替代对应源码。具体重建与 DLL 替换方法见 `licenses/build-information/BUILD.md`（仓库内为 `docs/third-party-build.md`）。

| 组件 | 版本 | 许可 / 来源 |
|---|---|---|
| Qt Core / Gui / Widgets / Network / Concurrent / Multimedia / MultimediaWidgets / ImageFormats / SVG / Translations | 6.8.3 | LGPL-3.0，动态链接；[Qt 源码](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/)；许可证在 licenses/qtbase 等目录 |
| FFmpeg（Qt 多媒体后端） | 7.1，Qt 官方 n7.1 构建 | LGPL-2.1-or-later，动态链接；[对应源码](https://github.com/FFmpeg/FFmpeg/tree/n7.1)；licenses/ffmpeg 与 build-information/ffmpeg-binaries.json |
| Filament / gltfio | 1.78.0 | Apache-2.0；[源代码](https://github.com/google/filament/tree/v1.78.0)，包含第三方子组件各自许可 |
| Assimp | 5.4.3 | BSD-3-Clause；[源代码](https://github.com/assimp/assimp/tree/v5.4.3)，含 zlib 等依赖声明 |
| SQLite | 3.50.4 | Public domain；[来源](https://sqlite.org/2025/sqlite-amalgamation-3500400.zip) |
| Zstandard | 1.5.7 | BSD-3-Clause；[源代码](https://github.com/facebook/zstd/tree/v1.5.7) |
| libsai | f07ee8878e184eeeded79a5a2ef808bc51a3a7f7 | MIT，见 helpers/licenses |
| sai2-parser | 2.0.0 | MIT，见 helpers/licenses |
| Python / Pillow / psd-tools / numpy / psutil | 锁定清单 | 各自 PSF / HPND / MIT / BSD 许可，见适配器声明 |
| Node.js | 24.19.0 | MIT 及其捆绑依赖许可，原 LICENSE 完整保留 |
| 官方 MCP TypeScript SDK | 2.3.1 | Apache-2.0；其他 npm 组件各自许可，随部署模块原文保留 |

Qt 图像与基础模块含 libpng 1.6.47、libjpeg-turbo 3.1.0、FreeType 2.13.3（选择 FTL 许可）、HarfBuzz 10.4.0、PCRE2 10.45、zlib 1.3.1、libtiff 4.7.0、libwebp 1.5.0 等。各组件的原版权、许可全文和 Qt 实际使用说明位于模块 `src/3rdparty/`、`LICENSES/` 和 `qt_attribution.json`。本软件部分基于 Independent JPEG Group 的工作（This software is based in part on the work of the Independent JPEG Group）；部分使用 FreeType 项目，版权见相应声明。全部 Qt 子组件清单以随附 SPDX 为准。

Filament 静态依赖保留各自许可，包括 Draco、Basis Universal、meshoptimizer、stb、MikkTSpace、Abseil、cgltf、smol-v、SPIR-V、Khronos 注册表、zstd 等；不能将其全部视为 Filament 主 Apache-2.0 许可。Assimp 同样保留其 contrib 的独立许可。声明目录包含上游的可选模块声明，因此目录中文件的存在不代表该模块全部进入可执行文件。

Qt 与 FFmpeg DLL 未作私有修改，可替换为 ABI 兼容版本以调试库的修改。允许为调试对 LGPL 库的修改而进行必要的逆向工程。应用的自有许可不限制相关 LGPL 权利。FFmpeg DLL 自报配置未启用 GPL/nonfree，准确命令及各 DLL 哈希已保存。发布包不包含开发测试使用的 imageio-ffmpeg GPL 命令行程序。

Microsoft Visual C++ 运行库依 Microsoft Visual Studio 可再分发条款提供。

独立格式助手的详细信息见 [helpers/ADAPTER_NOTICES.md](helpers/ADAPTER_NOTICES.md)。

开发仓库另包含官方 `ui-ux-pro-max-cli@2.15.0` 安装的 UI UX Pro Max 与配套设计技能，未打入桌面程序运行包。其上游 MIT 许可（Next Level Builder）和来源记录保留在 `.agents/skills/UI_UX_PRO_MAX_LICENSE.txt`、`.agents/skills/UI_UX_PRO_MAX_PROVENANCE.md`；`ui-styling` 附带的 Apache-2.0 原始许可也单独保留。这些第三方开发工具不受本仓库自有代码的保留权利声明覆盖。
