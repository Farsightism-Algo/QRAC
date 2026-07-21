English | [简体中文](README_CN.md) 
# QRAC - Quantitative Random Access Codes

Convert any file into a lossless image, and back.  
Think of it as a visual data container with damage detection.

## ✨ Features

- 📁 **File → Image** — encode any file into a PNG or BMP
- 🔍 **Image → File** — decode a QRAC image back to the original file
- 🛡️ **Damage Detection** — verification pixels detect compression artifacts
- 🔧 **Correction Mode** — repair damaged images via re-anchoring
- 📦 **Batch Encode** — process an entire folder at once
- 🖼️ **Self-Describing** — encoding parameters are embedded in the image header
- 🌐 **Offline** — no network, no telemetry, everything is local

## 🚀 Quick Start

### Download (Windows)

Get `qrac.exe` from [Releases](https://github.com/sans666VIP/QRAC/releases).

> ⚠️ QRAC may trigger antivirus false positives.  
> This is a known issue — see [below](#-antivirus-false-positives) for details.

### Build from Source

QRAC is a single `.cpp` file + three stb headers.

**Windows — Visual Studio**
