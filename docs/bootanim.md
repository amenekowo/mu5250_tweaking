# 开机动画

> **验证说明**：本文基于对 `/usr/ui/anim` 的只读检查。向 system 分区写入 PNG 或经 fastboot 刷写属写操作，**尚未按本文步骤在设备上执行**；执行前须备份并自行确认副作用。

开机动画位于 `/usr/ui/anim`，为 320×480 的 PNG，命名沿用源文件约定（`powerup` / `powerdown`）。将目标图片按该命名放入该目录即可作为开机或关机动画。

根文件系统经 overlayfs 挂载，仅改 overlay 中的动画文件**不会**在下一次冷启动时立即生效。若需改开机即可显示的画面，须在已提取的 **system** 分区上替换 `anim` 图片，再经 fastboot 刷回设备。该路径风险较高：若不拆机，救砖不便。

屏幕控制（DRM / QPIC / `zte_topsw_devui` / debugfs）见 [screen.md](screen.md)。
