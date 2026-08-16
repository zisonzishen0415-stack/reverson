> **项目状态（2026-08-16 修订）：失败。**
> 经 G1on 实机多次验证，自制 ZDL **无法在 G1on 上稳定工作**（表现为：无 UI、bypass、或切换死机）。
> 此前文档中的“已验证 / 可用 / 可运行 / 已对齐 / 可收口”等表述，均不代表 G1on 实机可用。
> 本项目的 ZDL 目标当前**未达成**，现状为失败；任何后续结论不得以“G1on 可用”为前提。

# Installing ZDLs With Zoom Effect Manager

The release `.ZDL` files are in [../dist/](../dist/). Use
[Zoom Effect Manager](https://zoomeffectmanager.com/en/download/) 2.3.3 or
newer and point it at that folder.

## Steps

1. Connect the pedal first, then open Zoom Effect Manager.
2. Open `Settings`.
3. Choose `Read Effects from folder`.
4. Select this repo's `dist/` folder.

![Read effects from folder](images/read-effects.png)

5. In the effect source/filter area, enable `Effects from devices`.
6. Enable `From Folder`.
7. Add the desired custom effects and write them to the pedal.

![Enable From Folder](images/from-folder.png)

## Notes

Back up your current effect list before writing. Current release effects target
ZDL-based MultiStomp pedals and are only hardware-tested on MS-70CDR firmware
2.10 so far.

If an effect does not appear, confirm that Zoom Effect Manager is reading the
same `dist/` folder shown in this repo and that the `From Folder` source is
enabled.

On MS-70CDR firmware 2.10, a Drive-category custom effect can flash correctly
but stay hidden in the pedal's on-device FX browser if no stock Drive effect is
installed. If `ToTape9.ZDL` writes successfully but does not appear while
scrolling effects, also install at least one stock Drive effect. With a stock
Drive effect present, the custom Drive effect has been reported visible.
