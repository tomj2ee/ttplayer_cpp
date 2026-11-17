# TTPlayer C++

基于 **C++20 + FFmpeg + SDL3 + SDL3_ttf** 的原生音频播放器，与仓库原有 Java 版本独立共存。不是 Java 启动器，也不需要 JVM。使用同一套播放和界面代码，面向 Windows x64 与 macOS（Apple Silicon / Intel，分别构建）。Windows 已在本机编译并运行回归；macOS 已加入平台适配、`.app` 打包和 CI 入口，尚待 Mac 构建与真机验收，不视为已验证发布版本。

## 直接运行

面向对象重构的职责划分、线程与生命周期约定、源码入口及 CMake/CTest 使用方法见 `ARCHITECTURE.md`。重构验收版单独输出到 `out/bin-refactor/`，不覆盖正在运行的默认版本。

启动时先在隐藏状态完成皮肤、尺寸、位置和首帧绘制，再成组显示需要的窗口；迷你启动不会先闪出主播放器，已隐藏的附属窗口不会临时弹出。

本次单文件部署验证版位于 `cpp/out/bin-single/ttplayer.exe`。只复制这一个 EXE 到其他目录或电脑即可运行，不需要安装 SDL、FFmpeg、curl 或 MinGW 运行库，也不需要携带 `assets/`。默认构建现已启用静态依赖和内嵌资源；不更改输出参数时仍输出到 `out/bin-complete/`。

Windows 版是纯图形界面程序：在资源管理器中双击 `cpp/out/bin-complete/ttplayer.exe` 即可启动，不需要打开终端或通过 `.bat` 启动。默认构建及 CMake preset 统一输出到此目录；打包会拒绝控制台子系统的旧 EXE，避免附带黑色控制台窗口。仓库根目录的 `ttplayer.bat` 是旧 Java 版入口，不是 C++ 播放器入口。

CMake 同时将原版 `TTPlayer.ico` 的多尺寸图像编入 Windows EXE，用于文件本身的图标，不只设置程序启动后的窗口/托盘图标；macOS 应用包继续使用原版 `TTPlayer.icns`。

此前 `out/bin-crossplatform-ui/` 是旧部署布局，不能将其 EXE 当作本次单文件版本。新版本仍使用统一 SDL3 菜单与 libcurl。

```powershell
.\cpp\out\bin-complete\ttplayer.exe
```

可带音乐文件、目录、M3U/M3U8 或 Java 播放列表 XML 参数：

```powershell
.\cpp\out\bin-complete\ttplayer.exe 'D:\Music\歌曲.flac'
.\cpp\out\bin-complete\ttplayer.exe 'D:\Music'
```

70 个皮肤、图标、五套语言和第三方许可文本已压缩嵌入 EXE。首次运行自动释放到配置目录的 `bundled-resources/<资源版本哈希>/`，后续校验并修复缺失或损坏的文件，不重复写入正常文件。单文件指部署只需 EXE，并不表示运行时完全不写磁盘；配置、歌词和资源缓存仍需可写目录。Windows 系统 DLL 和系统字体仍使用操作系统提供的版本。

### 拖放播放与换肤

- 把音频文件拖到主窗、播放列表、歌词、均衡器或迷你窗口，加入当前列表并播放；一次拖入多个文件时按拖入顺序添加并播放首个音频。
- 支持拖入文件夹（递归扫描音频）、M3U/M3U8/PLS 和 Java 播放列表 XML。已有歌曲不重复添加；连续拖入时会排队导入，不丢弃后续批次。
- 把原版格式的 `.skn` 皮肤文件拖到播放器窗口即可换肤，不停止当前音乐。外部皮肤加入本次运行的皮肤选择列表，并记住当前皮肤；请保留原 `.skn` 文件，程序只引用它，不复制或移动源文件。
- 音频和皮肤可以一起拖入；损坏的皮肤保留原外观，不阻止同批音频导入。单独拖入 `.lrc` 可加载歌词。扩展名大小写及中文路径均可处理。
- 启动参数同样接受音频、目录、播放列表和 `.skn`。支持无关联窗口的 SDL 文件拖放事件，供 macOS 应用级文件打开使用；Mac 原生行为仍待真机验收。

### 原版界面修正

默认不再使用此前的大尺寸深色界面。首次迁移读取 Java 的 `%USERPROFILE%/.ttplayer/windows.properties`；没有该文件时，开发目录读取仓库 `config/windows.properties`，否则使用 `default.skn`。按皮肤原生尺寸绘制，例如经典皮肤主窗为 327×157，Media Player 10 为 270×168，而不是整体放大两倍。

旧 C++ 配置迁移前保存为 `player.before-classic-ui.json`，歌曲列表和音乐文件保持不变。若仍显示旧布局，可运行：

```powershell
.\cpp\out\bin-complete\ttplayer.exe --restore-java-ui
```

本次补齐 `Playlist.xml` / `Lyric.xml` / `Visual.xml` 配色、原生按钮与滑块帧、ICO 图标、LED 补零及剩余时间切换；列表恢复左右分栏、可拖分隔条、皮肤滚动条、Ctrl/Shift 多选与批量删除。右键和工具条使用统一 SDL3 弹出菜单，可直接选择五种播放模式、70 个皮肤、13 类 Java 均衡器预设；迷你模式和托盘还原保留子窗口的显示状态。图标也已内嵌，无须另带 `assets/ico/`。

## 功能

- FFmpeg 后台解码、48 kHz 双声道 Float32 输出、SDL3 音频设备播放、暂停/恢复、停止、跳转和音量控制。
- 接入原 Java 中/英/日/韩/德五套语言资源、字体回退、HiDPI 坐标转换、文件选择和文件/目录拖放。
- 主窗口、播放列表、10 段均衡器、歌词、迷你播放器、透明桌面歌词，以及扩展工具、独立对话框和加载进度，共九个 SDL 窗口。
- 原版左右分栏、Ctrl/Shift 多选、拖动重排、查找/下一处/定位、完整列表管理、排序、批量导出、异步扫描进度和取消；五种播放模式、播完暂停和标题模板。
- 原 Java `playlists.xml`、M3U/M3U8/PLS 导入、M3U8 导出、HTTP/HTTPS 音频地址、独立 JSON 配置与原子替换保存。
- 原版 `.skn` ZIP/XML/BMP 皮肤读取、控件精灵帧、按钮、滑块、数字时钟、透明色和九宫格背景；设置中可选择仓库中的 70 个皮肤。
- 60 / 170 / 310 / 600 / 1K / 3K / 6K / 12K / 14K / 16K Hz 双二阶均衡器、±12 dB、预放大、声道平衡、立体声扩展和预设切换。
- 按 Java 模式顺序绘制频谱、镜像、折线、面积、LED、PCM 波形、星空七种效果；原皮肤渐变、细峰线、残影、3D 星点和流星。
- 本地 LRC、UTF-8/UTF-16/GBK、同一行多个时间标签、offset、增强逐字时间标签、同步高亮、点歌词跳转及每曲偏移保存。
- 在线歌词搜索/下载，使用 Java 版的相同 HTTPS 服务；后台请求、超时和响应大小限制，不上传音频。
- 原版皮肤选择、歌词搜索、选项、格式、输入与信息对话框；剪贴板、UTF-8 光标编辑与 IME 预编辑绘制。
- 桌面歌词原版单/双行、24px 白/绿文字、圆角半透明背景，置顶/拖动/穿透锁定；托盘、窗口成组移动/吸附、迷你信息轮播和重启恢复。

## 窗口管理与吸附

- 只有主播放器窗口作为普通应用窗口显示在 Windows 任务栏/窗口切换列表；播放列表、歌词、均衡器、迷你、桌面歌词、设置、对话框与进度窗口使用工具窗口标记，不单独占任务栏入口。
- 激活主播放器时，已经显示的附属窗口一起提升到其他普通窗口前面，不抢走主窗口输入焦点；已隐藏的窗口保持隐藏，独立最小化的附属窗口不强制还原。桌面歌词维持自身的置顶/穿透策略，对话框保持最上层和原有模态焦点。托盘恢复与迷你模式返回也同步窗口组的前后层级。
- Windows 系统托盘图标左键单击显示并激活主播放器（迷你模式返回主窗），右键弹出操作菜单；键盘激活托盘图标同样显示主窗。使用原版内嵌 EXE 图标，退出时清理，Explorer 重启后尝试重新注册；macOS 保留 SDL 的系统状态栏菜单。
- 应用内菜单统一使用 SDL3 弹出窗口，不再分别实现 Win32、AppKit 和窗口内裁剪菜单。七个播放列表工具栏菜单首次点击即可打开；支持多级子菜单、勾选、分隔线、长菜单滚动、方向键、Home/End、PageUp/PageDown、Enter 和 Esc。菜单可以超出小尺寸皮肤窗口，位置按 SDL 渲染坐标转换。
- 拖动主窗口时，已贴边连接的播放列表、歌词、均衡器等核心窗口一起移动，支持链式连接；拖动子窗口只移动自身，可直接拖离窗口组。每次拖动开始时固定成员，不会沿途把经过的窗口误带走。
- Windows 鼠标拖动按本次会话的原始窗口位置和鼠标累计位移计算，不把吸附后的坐标作为下一次输入；播放列表、歌词、均衡器慢速拖动也能脱离主窗，无需按 Alt。拖离后靠近边缘仍可重新吸附。
- 靠近屏幕工作区或其他核心窗口的边缘 10 像素内吸附，支持上下左右贴边及同边对齐；选择最近候选，吸附后超过 16 像素释放，减少边界抖动。按住 **Alt** 可临时关闭吸附，但不解除已有窗口组。
- 窗口组用整体外框吸附屏幕边缘；多个显示器、负坐标和任务栏工作区纳入计算。桌面歌词只吸附屏幕，不加入播放器窗口组；设置、对话框和进度窗口不参与互相吸附。
- 皮肤声明 `resize_rect` 的窗口支持四边及四角八方向缩放、边缘吸附和最小尺寸约束；固定皮肤窗口不允许拉伸。缩放只改变当前窗口，不带动其他窗口。
- Windows 使用原生移动/缩放消息实时更新，不再依赖延迟的 SDL 移动事件或“鼠标左键按下”猜测；程序设置位置不会触发连锁移动。系统键盘移动同样支持窗口组，Esc 取消会将组内窗口一起还原。
- 原生移动/缩放的系统循环中仍持续重绘频谱、歌词和窗口内容，不必松开鼠标才刷新；缩放吸附同样使用 10/16 像素磁滞。保存的是正常窗口尺寸，不把最大化后的屏幕大小写成下次启动尺寸。
- 主窗或迷你窗最小化时保留子窗显隐快照，还原时恢复；桌面歌词保持独立。启动、换皮肤及显示器/工作区变更时，恢复到屏幕内可拖动的位置，避免断开显示器后窗口找不到。
- 托盘隐藏/还原保留子窗口的最大化状态；隐藏期间的显隐切换更新恢复快照，已结束的进度窗不会重新弹出。模态窗口最后恢复焦点；切换迷你/完整模式会迁移对话框归属，文件选择及确认窗口不再挂到隐藏的主窗。
- 主菜单、托盘的“恢复窗口布局”或 **Ctrl+Shift+R** 可恢复当前皮肤的默认尺寸/布局并移回屏幕，保留播放列表、皮肤、迷你模式和子窗显隐状态。应用内对话框以当前播放器为中心，并限制在其显示器工作区内。

本机已验证 Windows 原生消息及实际系统键盘移动循环；多显示器负坐标、不同工作区采用确定性几何测试，尚未穷尽真实多屏和混合 DPI 硬件组合。

### 格式范围

当前本地 FFmpeg 精简配置包含 MP3、FLAC、WAV、OGG/Vorbis/Opus、AAC、M4A/MP4（AAC/ALAC）、APE、WavPack、AIFF、ASF/WMA 和常见 PCM。启用 HTTP/HTTPS/TCP/TLS，Windows TLS 使用 Schannel 并验证证书；网络打开/读取有超时及取消处理。**不是 FFmpeg 全格式构建，不含视频播放或任意流媒体协议。** WMA1/2/Pro/Lossless 解码器已编入，但不能据此推断所有编码变种和损坏文件均可播放。

## 离线构建

`src/main.cpp` 已加入 `ttplayer` 编译目标，皮肤、图标和语言文件默认使用本仓库的 `src/main/resources/`，无需配置资源路径，也不依赖旁边的 Java 工程。CLion 打开本目录后直接构建 `ttplayer` 即可；旧缓存中的默认外部资源路径会自动切换到仓库内资源，自定义资源目录仍可通过 `TTPLAYER_RESOURCE_ROOT` 指定。

直接使用 `thirdparty/` 已有源码，不使用 vcpkg、不下载依赖：

```text
thirdparty/sdl3-src/                       SDL3
thirdparty/ffmpeg-src/                     FFmpeg
thirdparty/sdl_ttf-src/                    SDL3_ttf
thirdparty/sdl_ttf-src/external/freetype/   FreeType
thirdparty/miniz*.c,h                      SKN ZIP
thirdparty/cJSON.c,h                       JSON 配置及歌词响应
```

在线歌词在所有平台共用 libcurl 请求实现，开启 HTTPS 证书与主机名验证、连接/总超时和 4 MiB 响应限制，禁止重定向。Windows 从本地 `thirdparty/curl-src` 静态构建 libcurl，使用 Schannel，无需额外 curl/OpenSSL DLL；macOS 使用系统 libcurl 和 iconv，不依赖 Homebrew 运行库。Windows 的 GBK/UTF-16 仍使用系统编码转换。FFmpeg 的 HTTPS 分别使用 Windows Schannel / macOS Secure Transport。SDL_ttf 使用本地 FreeType，关闭未需要的 HarfBuzz/PlutoSVG，避免额外下载。

### Windows

前提：MSYS2 的 MinGW64 GCC/G++、MSYS GNU Make，以及 Windows 原生 CMake 3.24+、Ninja。默认 MSYS2 位于 `C:\msys64`，脚本也会查找 Visual Studio 自带的 CMake/Ninja。

从仓库根目录执行：

```powershell
.\cpp\build.ps1 -RuntimeDirectory out\bin-complete
```

自定义工具或输出目录：

```powershell
.\cpp\build.ps1 -MsysRoot D:\msys64 -RuntimeDirectory out\my-player
```

不传 `-RuntimeDirectory` 时默认输出至 `cpp/out/bin-complete/`。如果该 EXE 正在运行，先关闭，或改用其他输出目录。不要把 MinGW 编译的 FFmpeg 静态库与 MSVC 对象混用。

所有构建产物都在 `out/`，不向第三方源码目录写入构建文件。首次需要完整编译 SDL3、FreeType、FFmpeg，之后为增量编译。

生成与本次验收相同的单文件版本：

```powershell
.\cpp\build.ps1 -RuntimeDirectory out/bin-single
```

CMake 的 `TTPLAYER_STATIC_DEPS=ON` 和 `TTPLAYER_EMBED_ASSETS=ON` 默认开启：静态链接 SDL3、SDL3_ttf、FreeType、FFmpeg、libcurl 和 MinGW 运行库，并内嵌资源。静态依赖模式要求使用本地 bundled 依赖。仅需旧式可替换 DLL/资源目录布局时，使用 `-SharedDependencies -ExternalAssets`；对应 CMake 选项都设为 `OFF`。建议使用独立输出目录，构建脚本不会擅自删除旧目录中遗留的 DLL 或资源。

### macOS

构建目标最低 macOS 12；准备 Xcode Command Line Tools（`xcode-select --install`）和 CMake 3.24+。依赖源码已在 `thirdparty/`，无需安装 SDL/FFmpeg/curl/iconv 的 Homebrew 版本。安装工具本身可能需要联网。

从仓库根目录运行，选择对应处理器架构：

```sh
bash cpp/build-macos.sh arm64       # Apple Silicon
bash cpp/build-macos.sh x86_64      # Intel
bash cpp/package-macos.sh arm64    # 按实际构建架构打包
```

默认不传架构时使用当前 Mac 的架构。输出为 `cpp/out/macos-arm64/TTPlayer.app` 或 `cpp/out/macos-x86_64/TTPlayer.app`，可在 Finder 中双击，不需要终端启动。两种架构各自打包，并非 Universal Binary；构建另一架构不等于已在该架构实测。不要尝试在 Windows 直接生成可用的 macOS 发布包。

打包输出 ZIP/DMG 到 `cpp/out/release/`，默认将 SDL、FFmpeg 等可静态构建的依赖链接进应用，内嵌皮肤和语言；应用包保留 Finder 所需图标。macOS 继续使用系统 libcurl、iconv 和系统框架，不是完全静态的裸二进制，交付单位为一个 `.app`。打包检查外部动态库依赖、签名和 SHA-256 清单。默认临时签名仅用于本机测试，下载到其他 Mac 仍可能被 Gatekeeper 拦截。正式分发需使用自己的 Developer ID，并完成 Apple 公证；已提供以下环境变量入口：

```sh
CODESIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)' \
NOTARYTOOL_PROFILE='your-keychain-profile' \
bash cpp/package-macos.sh arm64
```

`NOTARYTOOL_PROFILE` 需提前通过 `xcrun notarytool store-credentials` 配置，不要把证书或凭据提交到仓库。`.github/workflows/native-player.yml` 提供 Windows 与 macOS 双架构构建/打包入口；只有对应主机架构才执行启动检查。CI 尚未在本次本地操作中运行，实际通过状态以 GitHub Actions 为准。

macOS 与 Windows 共用 SDL3 弹出菜单，并加入系统中日韩字体、桌面歌词鼠标穿透、无边框窗口拖动/八方向缩放及共用吸附逻辑。快捷键和列表多选支持 Command，保留 Ctrl 兼容。声音设备、输入法、Retina、多屏、状态栏托盘和桌面歌词的实际表现仍需 Mac 真机验收。

## 配置与 Java 兼容

Windows 默认配置在 `%APPDATA%\TTPlayer\CppPlayer\player.json`；macOS 默认在 `~/Library/Application Support/TTPlayer/CppPlayer/player.json`。实际路径可在设置 → 帮助中查看。

```powershell
.\ttplayer.exe --portable
.\ttplayer.exe --config-dir 'D:\MyPlayerSettings'
```

Windows 的 `--portable` 保存到 EXE 同目录 `config/`；macOS 保存到 `.app` 同级的 `TTPlayerData/`，不修改已签名的应用包，须先复制到可写目录，不能直接写入只读 DMG。`--config-dir` 可指定其他可写位置。开发目录首次运行时自动读取原 Java 播放列表和均衡器参数，**不写回 Java 的任何配置**。独立发布包不会打包个人播放列表，可以在设置中手动导入。

损坏的 JSON 会先备份为 `player.invalid-*.json`，再使用默认值。歌曲路径按原位置引用，不复制音乐文件；移除列表歌曲只修改播放列表，不删除磁盘文件。

本地歌词优先匹配歌曲旁同名 `.lrc`；在线下载缓存到 C++ 配置目录的 `lyrics/`。搜索窗口支持指定保存路径；勾选关联时只在歌曲旁没有同名 LRC 的情况下创建，不覆盖已有侧文件。可用“导出当前歌词”另存，增强逐字标签会保留。

选项中的“自动歌词”默认关闭：开启后，缺歌词时按 Java 版逻辑取搜索首项，但仅写入 C++ 缓存，不向音乐目录自动写文件。首项可能匹配错误，准确匹配请用 F3 手选。语言、播放选项、显示格式、歌词模式和迷你恢复状态均持久化；关闭“记住位置”后保留窗口显隐，但不恢复旧坐标和大小。

Windows 默认寻找系统 Microsoft YaHei/SimHei/SimSun；macOS 寻找 PingFang/Songti/Hiragino 等系统字体。单文件版本不分发系统字体；外部资源模式仍可在 `assets/` 下提供有权使用的 `font.ttf` 或 `font.ttc`。

## 快捷键

| 快捷键 | 功能 |
|---|---|
| Ctrl+O / Ctrl+Shift+O | 打开音频 / 添加目录 |
| Ctrl+I / Ctrl+E | 导入 / 导出播放列表 |
| Space / S / P / N | 播放暂停 / 停止 / 上一首 / 下一首 |
| ← / → | 前后跳转 5 秒 |
| ↑ / ↓ | 音量；播放列表中为选择歌曲 |
| Enter / Delete | 播放列表中播放选中 / 移除选中 |
| Ctrl+F | 过滤歌曲 |
| Ctrl+A / Ctrl 或 Shift+点击 | 全选 / 多选歌曲 |
| F1 / F2 / F3 / F4 | 帮助 / 皮肤 / 在线歌词 / 可视化 |
| F5 / F6 / F7 / F8 | 歌词 / 桌面歌词 / 均衡器 / 播放列表 |
| F9 / F10 | 迷你模式 / 设置 |
| Ctrl+L | 锁定/解锁桌面歌词，也可在托盘中操作 |
| + / - | 当前歌曲的歌词偏移增减 100 ms |
| Ctrl+Q | 退出 |

右键播放器或列表打开播放/列表/皮肤菜单，列表左栏右键管理列表，右键歌词打开歌词操作，右键均衡器打开启用、环绕和预设菜单。**与 Java 一致，主窗口关闭按钮隐藏到托盘，迷你窗口关闭返回完整模式；真正退出请用菜单/托盘“退出”或 Ctrl+Q。** 桌面歌词锁定后，可通过托盘菜单解锁。

“杜比环绕”按 Java 源码实现为左右声道 0.35 交叉混合，不是 Dolby 编解码或认证功能。立体声扩展是额外的 mid/side 控制。Java 中浏览器按钮未绑定实现，C++ 版将“音乐窗”按钮接入与“打开歌曲”相同的文件选择流程，支持多选，选中后导入播放列表并开始播放。移动同步只有皮肤定义，不应误列为已存在但未迁移的功能。

## 打包

```powershell
.\cpp\package.ps1 -BinaryDirectory out\bin-complete
```

在 `cpp/out/release/` 生成带时间戳的目录和 ZIP。打包脚本读取构建生成的 `deployment.json`；默认单文件构建只复制主 EXE，不再复制 DLL 和外部 `assets/`，另附说明、第三方声明和 SHA256 清单。不包含个人配置、音乐或系统字体。`deployment.json` 只供打包脚本识别布局，运行播放器不需要它。

本次验收版可用 `-BinaryDirectory out/bin-single -OutputDirectory out/release-single` 打包。使用时只需 EXE；公开分发仍应保留许可证和相应源码/二次链接材料，不能把单文件运行等同于免除第三方许可要求。

`-IncludeRelinkObjects` 还可附带对应构建目录中的应用对象文件、内嵌资源库和静态依赖，方便二次链接；这不是“已完成所有第三方分发义务”的保证，公开分发时仍需依据实际依赖、源码、修改和构建方式核对。FFmpeg 当前采用静态链接。

### 单文件部署检查

```powershell
.\cpp\tests\run-single-file-tests.ps1
.\cpp\tests\run-single-file-tests.ps1 -Native
```

默认检查 `out/bin-single/ttplayer.exe`，可用 `-Executable out/bin-complete/ttplayer.exe` 指定其他构建。检查 PE 仅依赖系统 DLL、GUI 子系统，再把唯一的 EXE 复制到仓库外的中文临时路径，限制 PATH，验证 70 皮肤、五语言、界面绘制、缓存修复及 FFmpeg 音频解码。`-Native` 使用真实 Windows 窗口，其余默认使用 SDL dummy；音频均使用 dummy，不代替真实音箱听测。

## 诊断

```powershell
.\ttplayer.exe --help
.\ttplayer.exe --version
.\ttplayer.exe --exit-on-complete 'D:\Music\song.wav'
```

`--exit-on-complete` 只初始化音频，播放结束返回 0，文件或解码错误返回 2。

无声/无窗口环境可检查 UI 渲染：

```powershell
$env:SDL_AUDIO_DRIVER = 'dummy'
$env:SDL_VIDEO_DRIVER = 'dummy'
$env:SDL_RENDER_DRIVER = 'software'
.\ttplayer.exe --smoke-ui 'D:\Temp\ttplayer-ui-check'
```

会输出多个窗口/皮肤/可视化模式的 BMP 并自动退出。`dummy` 不代表真实设备音质验证，检查完请移除这些环境变量再正常听歌。

## 实现结构与范围

`src/media/` 负责解码、重采样、输出、DSP 和频谱；`src/app/` 按数据解析、配置、网络、皮肤、绘制、事件和应用生命周期拆分；`src/main.cpp` 仅负责入口与诊断参数。

迁移状态、验证结果和与 Java 版的差异见 `MIGRATION.md`。当前已覆盖主要音频播放闭环，但**不宣称 Java 所有附属功能及全部皮肤细节逐像素一致**。
