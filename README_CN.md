 [English](README.md)  | 简体中文
# QRAC — 量化随机存取编码

把任意文件无损编码为图像，再还原回来。  
本质是一个带损伤检测的可视化数据容器。

## ✨ 功能

- 📁 **文件 → 图像** — 把任何文件编码为 PNG 或 BMP
- 🔍 **图像 → 文件** — 从 QRAC 图像还原原始文件
- 🛡️ **损伤检测** — 验证像素可检测压缩噪声或物理损坏
- 🔧 **校正模式** — 轻微受损的图像可通过重新锚定修复
- 📦 **批量编码** — 一键处理整个文件夹
- 🖼️ **自描述格式** — 编码参数嵌入图像 header，无需额外文件
- 🌐 **完全离线** — 无网络、无遥测、一切本地

## 🚀 快速开始

### 下载（Windows）

从 [Releases](https://github.com/sans666VIP/QRAC/releases) 下载 `qrac.exe`。

> ⚠️ QRAC 可能被杀毒软件误报。这是已知问题，详见下方[杀毒软件误报说明](#杀毒软件误报说明)章节。

### 从源码编译

QRAC 是单文件程序（`QRAC.cpp`），依赖三个已包含在仓库中的 stb 头文件。

**Windows — Visual Studio**

1. 打开 `QRAC.vcxproj`
2. 确认链接器依赖中已添加：`comdlg32.lib` `shell32.lib` `ole32.lib`
3. 使用 `x64 Release` 配置编译

**Windows — g++（MinGW / MSYS2）**

```bash
g++ -std=c++17 -O2 QRAC.cpp -o qrac.exe -lcomdlg32 -lshell32 -lole32 -static
```

**Linux — g++**

```bash
g++ -std=c++17 -O2 QRAC.cpp -o qrac
# 文件对话框需要 zenity（多数发行版已预装）
```

### 使用方法

启动 `qrac`，从菜单选择操作：

```
1. 编码   — 文件 → QRAC 图像
2. 解码   — QRAC 图像 → 原始文件
3. 校正   — 修复受损的 QRAC 图像
4. 批量   — 将整个文件夹编码
5. 设置   — 查看 / 修改配置参数
```

所有文件选择使用系统原生对话框，无需手动输入路径。

## 📐 图像格式

每张 QRAC 图像包含三个区域：

```
┌──────────────────────────────┐
│  像素 0–3    │  Header       │  数据长度、L、填充阈值、校验和、版本号
│  像素 4–N-6  │  数据区       │  量化符号 → 区间中点
│  末尾 5 像素 │  验证区       │  固定颜色，用于损伤检测
└──────────────────────────────┘
```

数据末尾追加 CRC32 完整性校验。

## 🔙 向后兼容

v5.3 可解码旧版本（v5.0–5.1）编码的图像。检测到旧格式时会使用默认参数，并提示输入原始文件大小。

## 杀毒软件误报说明

QRAC 的行为是"读取文件 → 变换字节 → 写出新文件"。这一行为链与以下恶意软件模式重叠：

- **勒索软件启发式** — "读文件 → 加密 → 写出"的行为链高度相似。
- **隐写工具检测** — 把数据嵌入图像是已知的数据泄露手段，部分安全产品会对所有执行此操作的工具标记。
- **MinGW 编译偏见** — 大量恶意软件使用 MinGW 编译，导致杀毒引擎对 MinGW 产物整体提高检测阈值。
- **无数字签名** — 未签名的可执行文件会触发更严格的启发式扫描。

**QRAC 不含任何恶意代码。** 全程无网络通信、无注册表修改、无进程注入。源码完全公开可供审查。

### 如何应对

- **自行编译。** 自己编译的 exe 具有不同哈希值，不易命中杀毒软件的特征库。
- 提交到 [VirusTotal](https://www.virustotal.com)，向各厂商逐一申诉误报。
- 在杀毒软件中为 QRAC 所在文件夹添加排除项。
- Windows Defender 操作路径：*设置 → 隐私和安全性 → 病毒和威胁防护 → 管理设置 → 排除项 → 添加或删除排除项*。

这个问题与 [No!! MeiryoUI](https://github.com/Tatsu-syo/noMeiryoUI) 等合法工具面临的情况完全一致——它修改系统字体设置，同样触发误报。

## 📦 第三方代码（均已包含在仓库中）

| 库 | 许可证 |
|----|--------|
| [stb_image.h](https://github.com/nothings/stb) | Public Domain / MIT |
| [stb_image_write.h](https://github.com/nothings/stb) | Public Domain / MIT |
| [stb_image_resize.h](https://github.com/nothings/stb) | Public Domain / MIT |
| [LodePNG](https://github.com/lvandeve/lodepng) | zlib |

## 📄 许可证

MIT — 详见 [LICENSE](LICENSE)。

## 👤 作者

**陈学浩 (Xuehaoyu Chen)**  
GitHub: [@sans666VIP](https://github.com/sans666VIP)
