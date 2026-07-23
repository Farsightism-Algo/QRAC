English | [简体中文](README_CN.md) 
# QRAC - Quantitative Random Access Codes

Convert any file into a lossless image, and back.  
Think of it as a visual data container with damage detection.

Although this was not the original design intention, it has proved remarkably effective in circumventing regulation and content censorship.

## ✨ Features

- 📁 **File → Image** — encode any file into a PNG or BMP
- 🔍 **Image → File** — decode a QRAC image back to the original file
- 🛡️ **Damage Detection** — verification pixels detect compression artifacts
- 🔧 **Correction Mode** — repair damaged images via re-anchoring
- 📦 **Batch Encode** — process an entire folder at once
- 📤 **Batch Decode** — decode all QRAC images in a folder at once
- 📁➡️🗜️ **Large File Mode** — split + compact BMP + ZIP archive for huge files (100GB+)
- 🖼️ **Self-Describing** — encoding parameters are embedded in the image header
- 🌐 **Offline** — no network, no telemetry, everything is local

## 🚀 Quick Start

### Download (Windows)

Get `qrac.exe` from [Releases](https://github.com/sans666VIP/QRAC/releases).

> [!WARNING]
> QRAC may trigger antivirus false positives.  
> This is a known issue — see [Antivirus False Positives](#antivirus-false-positives) for details.

### Build from Source

QRAC is a single `.cpp` file. Place all dependencies in the same directory.

**Windows — Visual Studio**

1. Open `QRAC.vcxproj`
2. Ensure the following files are present in the project (they are by default):  
   `QRAC.cpp` `miniz.c` `miniz.h` `stb_image.h` `stb_image_write.h` `stb_image_resize.h`
3. Linker dependencies are already configured: `comdlg32.lib` `shell32.lib` `ole32.lib`
4. Build → `x64 Release`

**Windows — g++ (MinGW / MSYS2)**

```bash
g++ -std=c++17 -O2 QRAC.cpp miniz.c -o qrac.exe -lcomdlg32 -lshell32 -lole32 -static
```

**Linux — g++**

```bash
g++ -std=c++17 -O2 QRAC.cpp miniz.c -o qrac
# File dialogs require zenity (pre-installed on most distros)
```

### Usage

Launch `qrac` and pick an operation:

```
1. Encode         — file → QRAC image
2. Decode         — QRAC image → original file
3. Correct        — repair a damaged QRAC image
4. Batch Encode   — encode all files in a folder
5. Batch Decode   — decode all QRAC images in a folder
6. Large Encode   — large file → split + ZIP archive
7. Large Decode   — ZIP archive → restored file
8. Settings       — view / edit configuration
```

All file selection uses native OS dialogs — no paths to type.

### Large File Mode

For files too large to encode as a single image (e.g. 100GB ISO files), QRAC v0.6 introduces **Large File Mode**:

- The file is split into chunks of configurable size (default 50MB per chunk)
- Each chunk is encoded in **compact mode**: L=1, no filler, no verification pixels, 1 byte per pixel channel — zero overhead
- All BMP chunks are packed into a single ZIP archive along with a `manifest.txt`
- The ZIP layer prevents platforms from silently compressing the BMP images inside
- Decoding reads the ZIP, extracts all parts by order, verifies CRC per chunk, and reassembles the original file

Compact mode images are not damage-resistant (no quantization tolerance), but the ZIP container provides integrity protection — ZIP CRC32 guards each entry, and the QRAC CRC32 per chunk provides secondary verification.

## 📐 Image Format

Every QRAC image has three zones:

```
┌──────────────────────────────┐
│  Pixels 0–3   │  Header      │  data size, L, filler, checksum, version
│  Pixels 4–N-6 │  Data        │  quantized symbols → interval midpoints
│  Last 5 px    │  Verification│  fixed colors for damage detection
└──────────────────────────────┘
```

CRC32 is appended to the data for integrity verification.  
In compact mode, pixels directly store raw bytes (no quantization, no verification pixels).

## 🔙 Backward Compatibility

v0.6 can decode images made by older versions (v0.5, v0.1).  
When a legacy image is detected, default parameters are used and you'll be asked for the original file size.

## Antivirus False Positives

QRAC reads files, transforms their bytes, and writes new files.  
This behavior pattern overlaps with several malware heuristics:

- **Ransomware detection** — "read → encrypt → write" looks identical to ransomware at the file-system level.
- **Steganography detection** — embedding data into images is a known data exfiltration technique, and some security products flag any tool that performs this operation.
- **MinGW bias** — many malware samples are compiled with MinGW, causing some engines to treat all MinGW-built binaries with elevated suspicion regardless of what they actually do.
- **Unsigned executable** — without a code signing certificate, antivirus engines apply stricter heuristic thresholds.

**QRAC is not malware.** It performs no network activity, no registry modification, and no process injection. The full source code is available for inspection.

### How to reduce false positives

- **Build from source.** Your compiled binary will have a unique hash, making it unlikely to match any pre-existing signature in antivirus databases.
- **Submit to [VirusTotal](https://www.virustotal.com)** and report false positives to individual vendors through their respective portals.
- **Add an exclusion** in your antivirus software for the folder where QRAC resides.
- In Windows Defender: *Settings → Privacy & Security → Virus & threat protection → Manage settings → Exclusions → Add or remove exclusions*.

This is the same situation faced by many legitimate system tools such as [No!! MeiryoUI](https://github.com/Tatsu-syo/noMeiryoUI), which modifies system font settings and triggers identical false positives.

## 📦 Dependencies (all bundled)

| Library | License |
|---------|---------|
| [stb_image.h](https://github.com/nothings/stb) | Public Domain / MIT |
| [stb_image_write.h](https://github.com/nothings/stb) | Public Domain / MIT |
| [stb_image_resize.h](https://github.com/nothings/stb) | Public Domain / MIT |
| [miniz](https://github.com/richgel999/miniz) (miniz.h + miniz.c) | MIT |
| [LodePNG](https://github.com/lvandeve/lodepng) | zlib |

## 📄 License

MIT — see [LICENSE](LICENSE).
### Acknowledgments

If you integrate or reference QRAC code in your project, you are welcome to let the author know through either of the following ways:
- Send an email to vip10338848@Gamil.com <sub>(The author does not check this email address very often.)</sub>
- Submit an issue in the GitHub repository

This request is not part of the MIT License, does not constitute a legal obligation, and is made purely out of personal preference.

## 👤 Author

**Xuehaoyu Chen**  
GitHub: [@sans666VIP](https://github.com/sans666VIP)
