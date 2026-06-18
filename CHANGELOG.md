# 更新日志

本文件记录项目的所有重要变更。

格式基于 [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)，并遵循 [Semantic Versioning](https://semver.org/spec/v2.0.0.html)。

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [v0.5.0] - 2026-06-18

### 新增

- 新增开机 DIJI-NES logo 粒子聚合动画，使用 1-bit bitmap 资源，黑底白色显示。
- 暂停菜单新增 5 格图形音量控制，每格代表 20% 音量，支持左右键调节。
- 新增 logo bitmap 资源生成后的嵌入式显示支持。

### Added

- Added a DIJI-NES logo particle boot animation using a 1-bit bitmap resource on a black background.
- Added a 5-block graphical volume control in the pause menu, with each block representing 20% volume and adjustable with LEFT/RIGHT.
- Added embedded display support for the generated logo bitmap resource.

### 变更

- 改善 APU 输出链路：加入直流偏置跟踪、软限幅和轻微平滑，以提高 8530 蜂鸣器上的可用音量并减少破音。

### Changed

- Improved the APU output path with DC offset tracking, soft limiting, and light smoothing to increase usable volume on the 8530 speaker while reducing clipping.

---

## [v0.4.0] - 2026-05-28

### 新增

- ROM 浏览菜单支持 UTF-8 中文文件名显示，中文 ROM 名称会使用中文字体并按像素宽度安全截断。
- PlatformIO 构建默认启用 ESP32-S3 USB CDC，便于通过板载 USB 口烧录程序和查看串口日志。

### Added

- ROM browser now supports UTF-8 Chinese filenames, using a Chinese-capable font and pixel-width-safe truncation.
- PlatformIO builds now enable ESP32-S3 USB CDC by default for flashing and serial logs through the board's native USB port.

### 修复

- 修复 Mapper 2 / UxROM 游戏《赤色要塞 / Jackal (USA)》启动后黑屏的问题。
- 修复帧级调度下 VBlank 起点立即触发 NMI，导致同时轮询 `$2002` 的游戏看不到 VBlank 标志的问题。
- 改善 Mapper 2 PRG bank 选择：支持按实际 PRG bank 数镜像，并模拟常规 UxROM bus conflict 行为。

### Fixed

- Fixed black screen on Mapper 2 / UxROM title Jackal (USA).
- Fixed a frame-scheduler VBlank/NMI race where games polling `$2002` could miss the VBlank flag when the NMI handler read `$2002` first.
- Improved Mapper 2 PRG bank selection with actual-bank mirroring and common UxROM bus-conflict behavior.

### 说明

- 引入中文字体后，app 固件体积约为 797 KB；静态 RAM 使用仍约 52 KB。
- 感谢 [k7212519](https://github.com/k7212519) 贡献中文 ROM 文件名显示支持。

### Notes

- With the Chinese font included, the app firmware is about 797 KB; static RAM usage remains about 52 KB.
- Thanks to [k7212519](https://github.com/k7212519) for contributing Chinese ROM filename display support.

---

## [v0.3.0] - 2026-04-26

### 修复

- 修复《超级马里奥兄弟》大马里奥碰到敌人后角色消失的问题。
- 修复多精灵场景下部分角色/敌人被错误丢弃的问题。
- 改善横向卷轴游戏左右边缘花屏/接缝问题。
- 修复 CNROM 小容量 CHR ROM bank 镜像问题，改善《影之传说》菜单花屏现象。
- 修复稳定 60 FPS 场景下 Display 任务可能占满 CPU0 并触发 task watchdog 重启的问题。
- 不支持的 Mapper、损坏/不完整 ROM 现在会显示错误提示并返回主菜单。

### Fixed

- Fixed Big Mario disappearing after touching enemies in Super Mario Bros.
- Fixed incorrect sprite dropping in object-heavy scenes.
- Improved visible edge artifacts/seams in horizontal scrolling games.
- Fixed CNROM CHR bank mirroring for smaller CHR ROMs, improving The Legend of Kage menu graphics.
- Fixed possible task watchdog resets when the Display task saturated CPU0 during stable 60 FPS scenes.
- Unsupported mappers and invalid/incomplete ROMs now show an error message and return to the main menu.

### 变更

- 将固定隔帧跳帧改为奇数周期自适应跳帧，避免与游戏内受伤/无敌闪烁锁相。
- 增加左右 4px overscan 裁边，实际显示区域为 248x240。
- 按 NES OAM 正序选择每条扫描线前 8 个精灵，并反向绘制以保持低索引精灵优先级。
- 增加游戏启动保护：启动后数秒内没有成功渲染帧时提示失败并返回主菜单。

### Changed

- Replaced fixed frame skipping with odd-cycle adaptive frame skipping to avoid locking onto in-game blinking effects.
- Added 4px horizontal overscan crop on each side; visible area is 248x240.
- Selects the first 8 sprites per scanline in NES OAM order, then renders in reverse to preserve low-index priority.
- Added startup guard: if no frame is rendered after a few seconds, show a failure message and return to menu.

### 性能

- 新增背景 tile 行 2bpp 解码查表，减少部分 PPU 背景渲染开销。
- 当前大部分游戏约 57-61 FPS；重精灵场景约 55-58 FPS。
- 相比最激进的固定隔帧跳帧方案，部分场景可能低约 1 FPS，但精灵显示兼容性更稳定。
- Display 任务在每帧 DMA 后主动让出时间片，降低看门狗风险；部分场景 DMA 统计值可能略高。

### Performance

- Added background tile row 2bpp decode lookup table to reduce part of PPU background rendering cost.
- Most games now run around 57-61 FPS; object-heavy scenes are around 55-58 FPS.
- Some scenes may be about 1 FPS slower than the most aggressive fixed frame-skip mode, but sprite compatibility is more stable.
- Display task now yields after each frame DMA to reduce watchdog risk; DMA timing may be slightly higher in some scenes.

---

## [v0.2.1] - 2026-03-28

### 打包与文档

- 新增仓库内预编译合并固件目录 `firmware/`，提供一键烧录文件 `DIJI-NES_v0.2.1.bin`。
- README 新增“方式二：乐鑫 Flash Download Tool”烧录说明（ESP32S3 + 地址 `0x0`）。
- README 补充项目实物图、电路图与烧录示意图，降低首次上手门槛。

### Packaging & Docs

- Added in-repo prebuilt merged firmware folder `firmware/` with one-click flash image `DIJI-NES_v0.2.1.bin`.
- Added README instructions for “Option 2: Espressif Flash Download Tool” (ESP32S3 + address `0x0`).
- Added project photo, circuit image, and flash-tool screenshot in README for easier onboarding.

### 说明

- 本版本以发布流程和使用体验优化为主，不涉及核心模拟器功能逻辑变更。

### Notes

- This version focuses on release workflow and usability improvements, with no core emulator logic changes.

---

## [v0.2.0] - 2026-03-24

### 性能优化 (48 FPS -> 60 FPS)

- **MMC3 4-bank PRG 缓存**：预计算 `prgBank0~3Offset`，消除 `cpuReadMapper4` 中的运行时分支和乘法。
- **CHR bank 指针缓存**：`chrBankPtrs[8]` 直接指向 8 个 1KB CHR 页，PPU 访问零开销。
- **Nametable 指针缓存**：`ntPtrs[4]` 直接指向 4 个 nametable，消除镜像计算。
- **CPU 时钟重构**：赤字追踪模式 (`cycles -= target; while(cycles<0) cycles += step()`)，每次 `clock(113)` 消除约 75 次空转循环。
- **OAM 预评估**：帧开始时构建 `spriteIndicesPerLine[240][8]`，每条扫描线不再遍历 64 个精灵。
- **无分支背景像素写入**：全透明 tile 快速路径 + 32 位写入。
- **指针内联**：`renderBackgroundLine`/`renderSpriteLine`/`checkSprite0HitFast` 将 `ntPtrs`/`chrBankPtrs` 缓存为局部变量，每条扫描线消除约 132 次函数调用。
- **IRAM_ATTR**：关键热路径函数放入 IRAM 加速执行。

### Performance Optimizations (48 FPS -> 60 FPS)

- **MMC3 4-bank PRG cache**: Pre-computed `prgBank0~3Offset`, eliminated runtime branching in `cpuReadMapper4`.
- **CHR bank pointer cache**: `chrBankPtrs[8]` direct pointers to 8 x 1KB CHR pages, zero-overhead PPU access.
- **Nametable pointer cache**: `ntPtrs[4]` direct pointers to 4 nametables, eliminated mirror calculation.
- **CPU clock refactor**: Deficit-tracking mode, eliminated about 75 idle loops per `clock(113)` call.
- **OAM pre-evaluation**: Built `spriteIndicesPerLine[240][8]` at frame start, no more scanning 64 sprites per scanline.
- **Branchless background pixel writes**: Transparent tile fast path + 32-bit writes.
- **Pointer inlining**: Cached `ntPtrs`/`chrBankPtrs` as local variables in `renderBackgroundLine`/`renderSpriteLine`/`checkSprite0HitFast`, eliminating about 132 function calls per scanline.
- **IRAM_ATTR**: Critical hot-path functions placed in IRAM for faster execution.

### 缺陷修复

- **[严重] PRG RAM ($6000-$7FFF) 总线路由缺失**：CPU 读写只转发 `addr >= 0x8000` 给卡带，$6000-$7FFF (SRAM) 读返回 0、写被丢弃。这是超级玛丽 3 黑屏的根本原因。
- **[严重] MMC3 IRQ 电平触发修复**：`acknowledgeIrq()` 在 `cpu.irq()` 检查 I-flag 之前清除 pending，当 CPU I-flag 置位时 IRQ 永久丢失。现在 pending 只由游戏写 $E000 清除。
- **[严重] IRQ 跟随实际渲染状态**：`ppu.renderEnabled` 硬编码为 `true`，即使游戏关闭渲染 ($2001) MMC3 IRQ 计数器仍在递减。导致 KOF97 菜单切换时 VRAM 更新被破坏。修复为检查实际 `ppuMask & 0x18`。
- **CPU 中断周期计数**：`irq()` 和 `nmi()` 使用 `cycles = 7` 绝对赋值，丢弃上一条指令剩余周期，改为 `cycles += 7`。
- **VBlank 周期数修正**：从 2501 修正为 2274（20 条扫描线 x 约 113.67 周期）。
- **脏 iNES 头检测**：盗版 ROM 的 header bytes 8-15 常有垃圾数据，导致 mapper 高位错误。现已自动检测并仅使用 `flags6` 低半字节。
- **ntPtrs 空指针崩溃**：`updateNtPtrs()` 在 `load()` 中 `setVramPointer()` 之前调用导致空指针。已加保护并在 `setVramPointer()` 中自动调用。

### Bug Fixes

- **[Critical] Missing PRG RAM ($6000-$7FFF) bus routing**: CPU read/write was only forwarded to cartridge for `addr >= 0x8000`. $6000-$7FFF returned 0 on read and writes were dropped. This was the root cause of the SMB3 black screen.
- **[Critical] MMC3 IRQ level-trigger fix**: `acknowledgeIrq()` cleared pending before `cpu.irq()` checked the I-flag, causing IRQs to be permanently lost when CPU I-flag was set. Pending is now only cleared by the game writing $E000.
- **[Critical] IRQ follows actual rendering state**: `ppu.renderEnabled` was hardcoded to `true`, causing MMC3 IRQ counting even when rendering was disabled via $2001. This corrupted VRAM updates in the KOF97 menu. Fixed by checking actual `ppuMask & 0x18`.
- **CPU interrupt cycle accounting**: `irq()`/`nmi()` used absolute `cycles = 7`, changed to additive `cycles += 7`.
- **VBlank cycle correction**: Corrected from 2501 to 2274 (20 scanlines x about 113.67 cycles).
- **Dirty iNES header detection**: Bootleg ROMs with garbage in header bytes 8-15 now auto-detected, using only the low mapper nibble from `flags6`.
- **ntPtrs null-pointer crash**: `updateNtPtrs()` could be called before `setVramPointer()` during `load()`. Fixed with null protection and automatic update in `setVramPointer()`.

### 新功能

- **无 SD 卡启动界面**：不插 SD 卡不再黑屏，显示菜单提示 "No SD card detected" 和 "Press A to retry"。

### New Features

- **No-SD-card startup screen**: Without an SD card inserted, the screen no longer goes black. A menu appears showing "No SD card detected" and "Press A to retry".

### 兼容性

- **超级玛丽 3 (Super Mario Bros. 3)**：现已完全可玩（之前黑屏）。
- **MMC3 游戏**：分屏滚动和扫描线 IRQ 时序正常工作。
- **脏头 ROM**：自动检测并正确处理。

### Compatibility

- **Super Mario Bros. 3**: Now fully playable (was black screen).
- **MMC3 games**: Split-screen scrolling and scanline IRQ timing working correctly.
- **Dirty-header ROMs**: Auto-detected and handled gracefully.

---

## [v0.1.0] - 2026-02-23

### 首次发布

- 6502 CPU 全指令集模拟（约 150 个操作码）。
- PPU：背景渲染、滚动、64 个精灵（8x8 和 8x16 模式）。
- APU：方波、三角波、噪声、DMC 通道，通过 I2S DAC 输出。
- 双核架构：Core 0（音频 + 显示），Core 1（模拟）。
- Mapper 支持：NROM (0), MMC1 (1), UxROM (2), CNROM (3), MMC3 (4，部分)。
- SD 卡 ROM 浏览菜单。
- 暂停菜单：存档/读档。
- 大部分游戏约 50 FPS。
- SPI ST7789 320x240 显示 (LovyanGFX)。

### Initial Release

- 6502 CPU full instruction set emulation (~150 opcodes).
- PPU: background rendering, scrolling, 64 sprites (8x8 and 8x16 modes).
- APU: square, triangle, noise, and DMC channels via I2S DAC.
- Dual-core architecture: Core 0 (audio + display), Core 1 (emulation).
- Mapper support: NROM (0), MMC1 (1), UxROM (2), CNROM (3), MMC3 (4, partial).
- SD card ROM browser with menu system.
- Pause menu with save/load state.
- About 50 FPS for most games.
- SPI ST7789 320x240 display via LovyanGFX.
