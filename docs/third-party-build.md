# 对应源码与第三方库重建

便携发行版同时提供 `ImagiDock-0.1.0-third-party-sources.zip`。它包含版本锁定的完整官方源码归档、校验值、Qt 官方 FFmpeg 构建脚本、Qt SPDX 清单及实际 DLL 的构建配置。源码包应与应用 ZIP 一起上传到同一 GitHub Release，下载无需申请。`docs/license-sources.lock.json` 可单独复核每个上游下载地址与 SHA-256。

这里的构建命令用于修改、重建和替换第三方库；发行包使用官方 Qt / Filament 预编译库，没有修改其库代码。源码归档保留上游已有的源码、许可证、构建文件与必要资源。来源均为明确列出的第三方归档，不读取或包含用户素材库。

## Qt 6.8.3（动态链接，LGPL-3.0）

对应模块为 qtbase、qtmultimedia、qtimageformats、qtsvg、qttranslations。Qt Core/Gui/Widgets/Network/Concurrent 在 qtbase 内。每个源码模块已包含自身所需的内嵌第三方源码，例如 PNG、JPEG、FreeType、HarfBuzz、PCRE2、TIFF、WebP 和 zlib。`licenses/qtbase` 等目录保留原许可证与 `qt_attribution.json`，`build-information/*.spdx*` 是官方二进制包提供的清单。

解包五个模块。使用 Windows x64 MSVC 开发者命令环境、CMake、Ninja、Python 3；官方包的编译器为 MSVC 19.39.33520，具体功能见 `qt-qconfig.pri` 与 SPDX。先构建以下 FFmpeg，再构建 Qt；将下面 `QT_PREFIX`、`FFMPEG_PREFIX` 和源码位置替换为自己的目录。不要启用静态 Qt：

```text
cmake -S qtbase-everywhere-src-6.8.3 -B qtbase-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=QT_PREFIX -DBUILD_SHARED_LIBS=ON -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
cmake --build qtbase-build
cmake --install qtbase-build
QT_PREFIX/bin/qt-configure-module qtmultimedia-everywhere-src-6.8.3 -- -DFFMPEG_DIR=FFMPEG_PREFIX -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
cmake --build .
cmake --install .
```

`qt-configure-module` 应分别在空的模块构建目录运行；同样构建 qtimageformats、qtsvg 和 qttranslations。构建选项需与 `qt-qconfig.pri` 的共享库 ABI 和所需插件相容；上述命令是可修改库的构建入口，不声称逐字节重现官方构建环境。官方文档：https://doc.qt.io/qt-6/build-sources.html 。

应用直接加载同目录的 Qt6 DLL 及 `platforms/`、`imageformats/`、`multimedia/` 等插件。关闭应用和库服务，保留备份后替换 ABI 兼容的 DLL/插件即可使用修改后的库。运行媒体预览需要 `platforms/qoffscreen.dll`，主窗口使用 `qwindows.dll`。无需签名许可或在线激活。允许为调试对 LGPL 库的修改而进行必要的逆向工程；应用自有代码的权利声明不限制这些 LGPL 权利。

## FFmpeg n7.1（动态链接，LGPL-2.1-or-later）

已实际调用发行版 `av_version_info()`、每个库的 `*_license()` 和 `*_configuration()`，得到版本 `7.1`、`LGPL version 2.1 or later`。原始结果和每个 DLL 的 SHA-256 保存在 `ffmpeg-binaries.json`。其配置没有 `--enable-gpl` 或 `--enable-nonfree`。

Qt 6.8.3 的官方 `coin/provisioning/common/windows/install-ffmpeg.ps1` 指定 **n7.1**，对应源码归档 SHA-1 为 `f008a93710a7577e3f85a90f4b632cc615164712`。所附 `FFmpeg-n7.1.tar.gz` 与之完全一致，另外固定 SHA-256。MSVC 路径没有源码补丁。保留的 `qt-install-ffmpeg.ps1`、`qt-ffmpeg_config_options.txt`、`qt-windows-helpers.ps1` 是官方构建上下文；其 Android 等其他平台分支不是此 Windows 包配置。

在已加载 x64 MSVC 编译环境的 MSYS2 shell 中，准备上游 FFmpeg 构建所需的 make、shell 工具及 x86 汇编器，在解包源码的空 `build/msvc` 目录运行：

```sh
../../configure --prefix=installed --disable-programs --disable-doc --disable-debug --enable-network --disable-lzma --enable-pic --disable-vulkan --disable-v4l2-m2m --disable-decoder=truemotion1 --toolchain=msvc --enable-shared --disable-static
make install -j
```

将生成的 `avcodec-61.dll`、`avformat-61.dll`、`avutil-59.dll`、`swresample-5.dll`、`swscale-8.dll` 放入应用目录，可替换同 ABI 的发行库。Qt 构建时若需要 import library，按官方脚本将 `lib*.a` 改名为对应 `*.lib`。FFmpeg 全部源码和 LGPL 2.1/3 许可文本已附带；发行程序没有包含开发测试使用的 GPL 版 imageio-ffmpeg 可执行程序。

## Filament、Assimp 与其他原生库

Filament 1.78.0 使用官方 Windows x64 `/MD` SDK 静态库。对应完整源码包括各 `third_party/` 组件，`licenses/filament/` 同时保留 Apache-2.0 主许可、Draco、Basis Universal、meshoptimizer、stb、Abseil、smol-v、SPIR-V、Khronos 组件等原版权声明；MikkTSpace 等源码内许可另保留原头文件。完整源码还含可选构建模块，其许可附带不代表所有模块都链接进入程序。

按 Filament 源码内 `BUILDING.md` 使用其 Windows 工具链构建 Release 与 `/MD` 变体。ImagiDock 链接的名称是：`gltfio_core gltfio filament backend filabridge filaflat utils geometry shaders uberarchive uberzlib stb ktxreader basis_transcoder dracodec meshoptimizer mikktspace bluegl bluevk smol-v image filamat matdbg abseil zstd`。以项目 `CMakeLists.txt` 的 `FILAMENT_LIBS` 为实际链接依据；没有动态 Qt 静态化或改变其许可。

Assimp 5.4.3 从附带源码构建静态库：关闭测试、工具和默认全部导入/导出；开启 OBJ/FBX/glTF 导入及 glTF 导出，开启内嵌 zlib。对应开关在项目 `CMakeLists.txt`；主 BSD-3-Clause 许可及 contrib 的 zlib、unzip、rapidjson、pugixml、poly2tri 等声明均随包保存。

Zstandard 1.5.7 使用 BSD-3-Clause 选项，从 `lib/common`、`compress`、`decompress`、`dictBuilder` 构建静态库。SQLite 3.50.4 使用官方 amalgamation，Public Domain；编译开关 `SQLITE_ENABLE_FTS5 SQLITE_THREADSAFE=1 SQLITE_ENABLE_COLUMN_METADATA`。libsai 固定提交 `f07ee8878e184eeeded79a5a2ef808bc51a3a7f7`，MIT；SAI2 工具为官方 2.0.0 二进制及对应提交 `6671463ef4b5364f3db5ee9659a6015fd6c2ca0a`，MIT。

## 重新打包声明与源码

项目本地运行：

```powershell
.tools/adapters-env/Scripts/python.exe scripts/package-licenses.py dist/ImagiDock-0.1.0-win-x64
```

缺少锁定归档时添加 `--download`；只会下载清单指定的官方地址并核对哈希。输出为应用目录下 `licenses/` 与独立 `dist/third-party-sources/ImagiDock-0.1.0-third-party-sources.zip`。源码 ZIP 旁的 JSON 记录包 SHA-256。脚本在缺少关键许可、Qt attribution 所引文件、FFmpeg 版本/许可不符时失败；不能忽略这些错误后发布。Python/Node/轮子及 MCP 依赖的具体许可由 `package-adapters.py` 保存在 `licenses/adapters/` 和对应模块目录。
