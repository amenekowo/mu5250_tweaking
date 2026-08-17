# 分区表（eMMC / mmcblk0）

> **验证说明**：本文基于在设备上的只读检查（`/proc/partitions`、`lsblk`、`/dev/block/bootdevice/by-name`）。挂载点为检查当时的运行态。`dd` 备份整盘属读出操作，见 README「备份分区」。

内置 eMMC（`/dev/mmcblk0`，约 7.58GB）为 **Android A/B 双槽**布局（`_a` / `_b`），共 **69 个用户分区**（p1..p69），另有两块 4MB `boot` 硬件分区（`mmcblk0boot0/1`）。

## 分区命名映射（by-name → 块设备 → 大小 → 挂载点）

| by-name | 块设备 | 大小 | 挂载点 | 说明 |
|---|---|---|---|---|
| modem | mmcblk0p1 | 300M | `/firmware` | 调制解调器固件（A） |
| modem_b | mmcblk0p2 | 300M | — | 调制解调器固件（B） |
| cpucp | mmcblk0p3 | 1M | — | Qualcomm CPU-CP 固件（A） |
| cpucp_b | mmcblk0p4 | 1M | — | （B） |
| xbl | mmcblk0p5 | 3.5M | — | eXtensible Bootloader（A） |
| xbl_b | mmcblk0p6 | 3.5M | — | （B） |
| catecontentfv | mmcblk0p7 | 1M | — | 分类内容校验值 |
| modemst1 | mmcblk0p8 | 4M | — | 调制解调器 NV 状态（冗余区 1） |
| modemst2 | mmcblk0p9 | 4M | — | 调制解调器 NV 状态（冗余区 2） |
| fsg | mmcblk0p10 | 4M | — | FSG（文件系统组） |
| manufacture | mmcblk0p11 | 50M | `/etc_ro/manufacture` | 出厂信息（RO） |
| ART | mmcblk0p12 | 1M | — | 备份分区（by-name 为 `0:ART`） |
| ztebat | mmcblk0p13 | 20M | `/ztebat` | ZTE 电池信息 |
| tz | mmcblk0p14 | 4M | — | TrustZone OS（A） |
| tz_b | mmcblk0p15 | 4M | — | （B） |
| tz_devcfg | mmcblk0p16 | 512K | — | TrustZone 配置（A） |
| tz_devcfg_b | mmcblk0p17 | 512K | — | （B） |
| ddr | mmcblk0p18 | 256K | — | DDR 初始化 |
| ddr_debug | mmcblk0p19 | 1.3M | — | DDR 调试 |
| apdp | mmcblk0p20 | 512K | — | App 数据（A） |
| apdp_b | mmcblk0p21 | 512K | — | （B） |
| xbl_config | mmcblk0p22 | 1M | — | XBL 配置（A） |
| xbl_config_b | mmcblk0p23 | 1M | — | （B） |
| xbl_ramdump | mmcblk0p24 | 1M | — | XBL RAM dump（A） |
| xbl_ramdump_b | mmcblk0p25 | 1M | — | （B） |
| shrm | mmcblk0p26 | 128K | — | SHRM（A） |
| shrm_b | mmcblk0p27 | 128K | — | （B） |
| multi_oem | mmcblk0p28 | 512K | — | 多镜像 OEM（A） |
| multi_oem_b | mmcblk0p29 | 512K | — | （B） |
| multi_qti | mmcblk0p30 | 512K | — | 多镜像 QTI（A） |
| multi_qti_b | mmcblk0p31 | 512K | — | （B） |
| aop | mmcblk0p32 | 512K | — | Always-On Processor（A） |
| aop_b | mmcblk0p33 | 512K | — | （B） |
| aop_devcfg | mmcblk0p34 | 512K | — | AOP 配置（A） |
| aop_devcfg_b | mmcblk0p35 | 512K | — | （B） |
| qhee | mmcblk0p36 | 1.5M | — | Trusted Execution Env（A） |
| qhee_b | mmcblk0p37 | 1.5M | — | （B） |
| abl | mmcblk0p38 | 512K | — | Android Bootloader（A） |
| abl_b | mmcblk0p39 | 512K | — | （B） |
| uefi | mmcblk0p40 | 4M | — | UEFI（A） |
| uefi_b | mmcblk0p41 | 4M | — | （B） |
| boot | mmcblk0p42 | 70M | — | 内核 boot 镜像（A） |
| boot_b | mmcblk0p43 | 70M | — | （B） |
| misc | mmcblk0p44 | 1.3M | — | misc（bootloader 控制） |
| devinfo | mmcblk0p45 | 1M | — | 设备信息 |
| recoveryinfo | mmcblk0p46 | 4K | — | 恢复信息 |
| sec | mmcblk0p47 | 512K | — | 安全 |
| ipa_fw | mmcblk0p48 | 512K | — | IPA 固件（A） |
| ipa_fw_b | mmcblk0p49 | 512K | — | （B） |
| qupfw | mmcblk0p50 | 80K | — | QUP 固件（A） |
| qupfw_b | mmcblk0p51 | 80K | — | （B） |
| keymaster | mmcblk0p52 | 512K | — | Keymaster（A） |
| keymaster_b | mmcblk0p53 | 512K | — | （B） |
| cmnlib64 | mmcblk0p54 | 1M | — | 公共安全库（A） |
| cmnlib64_b | mmcblk0p55 | 1M | — | （B） |
| persist | mmcblk0p56 | 8M | `/persist` | 持久化（深色/校准数据） |
| cache | mmcblk0p57 | 800M | `/cache` | 缓存 |
| systemrw | mmcblk0p58 | 8M | `/overlay` | OpenWrt overlay（非易失用户数据） |
| system | mmcblk0p59 | 800M | `/` | OpenWrt 根（A） |
| system_b | mmcblk0p60 | 800M | — | （B） |
| fota | mmcblk0p61 | 2M | — | FOTA（在线升级） |
| ztedata | mmcblk0p62 | 50M | `/usr/zte_web` | ZTE Web 界面数据（A） |
| ztedata_b | mmcblk0p63 | 50M | — | （B） |
| zteoverlay | mmcblk0p64 | 100M | `/zteoverlay` | ZTE overlay |
| zterw | mmcblk0p65 | 50M | `/etc_rw` | ZTE 可写配置 |
| ai_app | mmcblk0p66 | 400M | `/ai_app` | AI 应用 |
| ai_data | mmcblk0p67 | 124M | `/ai_data` | AI 数据 |
| userdata | mmcblk0p68 | 1.9G | `/data` | 用户数据（容量最大） |
| rawdump | mmcblk0p69 | 206M | — | 原始 dump |

## 块设备统计

```
mmcblk0      179:0   7G   disk
mmcblk0boot0 179:8   4M   disk
mmcblk0boot1 179:16  4M   disk
```

- 总计：**69** 个 user 分区（mmcblk0p1..p69），A/B 双槽以 `_a`/`_b` 后缀成对出现。
- README 中告知的 `userdata`（p68，1.9G）为容量最大分区，用于 overlay 挂载只读 `/usr` 等目录（见 README 「开启ssh」）。

## 说明

- 数据来源：`cat /proc/partitions`、`lsblk`、`ls -la /dev/block/bootdevice/by-name/`。
- `by-name` 中的 `0:ART` 冒号是 Qualcomm 分区命名约定，代表物理分区 `ART`。
- 备份整颗 eMMC 见 README「备份分区」（`dd if=/dev/mmcblk0`）。
