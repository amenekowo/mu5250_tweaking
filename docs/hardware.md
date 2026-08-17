# 硬件拆解配置

> **验证说明**：本文基于充电头网拆解报告，以及在设备上经 SSH（`root@192.168.0.1`）的只读检查（`dmesg`、`/proc`、`/sys` 等节点），**未对设备执行写操作**。各表「验证依据」列标明数据来源（sysfs 节点 / JEDEC 字段 / dmesg 关键字 / 拆解丝印）。拆解报告：充电头网《拆解报告：ZTE中兴5G随身WiFi U60 Pro》(2025-06-03) https://www.chongdiantou.com/archives/1748936225812.html

中兴 U60 Pro（型号 **MU5250**）的硬件规格、拆解器件与系统侧核对如下。

---

## 一、产品概览

| 项目 | 值 | 验证依据 |
|---|---|---|
| 产品名称 | 中兴 5G 随身WiFi U60 Pro | 包装 / 拆解报告 |
| 型号 | **MU5250** | 包装 / 拆解报告；电池固件名 `ZTE_MU5250_...`（power_supply） |
| 配色 | 钛银 | 拆解报告 |
| 网络 | 5G-A（Sub-6GHz），下行峰值 **4.29Gbps**（骁龙X75） | 拆解报告 |
| WiFi | **Wi-Fi 7**（WCN7851/FastConnect 7800），峰值 3600Mbps，64 台设备 | 拆解报告；在设备上检查为 WCN7851 + WCN7850；网页交叉验证（FastConnect 7800） |
| 屏幕 | 3.5 英寸触摸屏 | 拆解报告；在设备上检查 DRM 320×480 |
| 电池 | 10000mAh（典型），标称 29 小时 | 拆解；在设备上检查满充 10214mAh |
| 快充输入 | 最大 27W（5V3A/9V3A/12V1.5A） | 贴纸 / 拆解；在设备上检查 PD 双档 |
| 反向快充 | 最大 18W（5V3A/9V2A/12V1.5A），充电宝功能 | 贴纸 / 拆解；在设备上检查 `powerbank_zte` 供电节点 |
| 天线 | 9 根 5G 全向 FPC 天线 + N79 | 拆解报告 |
| 尺寸/重量 | 158.1×73.2×16.1 mm，274.0g | 拆解报告 |

**USB-C 协议**（ChargerLAB POWER-Z KM003C，拆解报告）：QC3.0 / FCP / SCP / AFC / PD3.0 / DCP / Apple2.4A；PDO 为 5V3A 与 9V2A 两组固定档位。

---

## 二、主控平台（SoC）

| 项目 | 值 | 验证依据 |
|---|---|---|
| SoC | 高通 **骁龙 X75**（调制解调器平台代号 **SDXPINN / SDX_PINNACLES**） | `socinfo.raw_id=436`、`chip_id=SDX_PINNACLES`、`machine=SDXPINN`、`sku=SDXPINN-0`、`family=Snapdragon`、`soc_id=556` |
| 内核 | Linux **5.15.167-perf**（Android clang 14.0.7，LLD 14.0.7） | `/proc/version` |
| OpenWrt | 23.05.4 r24012-d8dd03c46f，target `sdx75/generic`，arch `aarch64_cortex-a53` | `/etc/openwrt_release` |
| CPU | **4× ARM Cortex-A53**（aarch64，部分 0xd05） | `/proc/cpuinfo`（4 核，BogoMIPS 38.40，Features 含 asimd/aes/sha2/crc32/atomics） |
| 内存 | **LPDDR4X 2GB**（拆解 = 济州半导体 JSL4BAG167ZAMF-05A，4266Mbps） | 拆解丝印；在设备上检查 `MemTotal=1628588 kB`（≈1.55GiB 可用） / `/proc/meminfo` |
| 启动方式 | eMMC 启动，A/B 双槽 | `androidboot.bootdevice=8804000.sdhci`、`SLOT_SUFFIX=_a`（dmesg kernel cmdline） |
| Boot 镜像 | BOOT.MXF.2.3-PINNACLES / MPSS.DE.7.0-PINNACLES_ALL_PACK (modem) | `socinfo.images` / `/sys/devices/soc0/images` |

> **PMIC 数量**：`socinfo` 报 `pmic_model=65610`、`num_pmics=4`。SPMI 树中实际挂载 **pmk8550 + pmx75 + pm7550ba + pmg1110** 四颗，与拆解所列 **PMX75 + PM7550BA** 对应。

---

## 三、核心器件清单（拆解 + 系统核对）

### 3.1 无线 / 射频

| 器件 | 型号 | 验证依据 |
|---|---|---|
| 5G 调制解调器 | 高通 **骁龙 X75**（SDX75 / SDXPINN） | 拆解；在设备上检查 `socinfo.chip_id=SDX_PINNACLES`、modem 固件 `MPSS...PINNACLES_ALL_PACK` |
| Wi-Fi 7 芯片 | 高通 **WCN7851**（FastConnect 7800 方案，三频 2x2 MIMO Wi-Fi 7） | ① 拆解报告：无线通信芯片 WCN7851，WiFi7；② 权威 PCI ID 库 pci.ids 交叉验证：vendor `17cb=Qualcomm Technologies`、device `1107=WCN785x Wi-Fi 7(802.11be) 320MHz 2x2 [FastConnect 7800]`；③ 在设备上检查 PCI `0000:01:00.0`=`17CB:1107`（class 0x028000 无线网卡）驱动 `cnss_pci`，固件 `wlan/qca_cld/WCNSS_qcom_cfg.ini` |
| 蓝牙 | 高通 **WCN7850** 平台节点（`qcom,wcn7850`，含 rfkill；属 WCN78xx Wi-Fi7+BT 组合） | 在设备上检查：`/sys/bus/platform/devices/soc:bt_wcn7850/of_node/compatible`=`qcom,wcn7850`；Linux 内核 ath12k 驱动源码/固件路径为 `WCN7850/hw2.0`（`MODULE_FIRMWARE`），与 PCI ID `1107=WCN785x` 同属高通 WCN78xx Wi-Fi 7 家族（WiFi 与 BT 伴生）；具体 pin 级关系未逐一验证 |
| 射频收发器 | 高通 **SDR875** | 拆解丝印 |
| 5G PA | 慧智微 **S55643-51** ×2 | 拆解丝印 |
| RF FEM | 慧智微 **S55235-11** ×2 | 拆解丝印 |
| WiFi FEM | 高通 丝印 2H6/2J4 ×2 | 拆解丝印 |
| 天线 | 9 根 5G 全向 FPC 天线 | 拆解 |

### 3.2 存储 —— JEDEC / sysfs

| 器件 | 型号 | 验证依据（JEDEC / sysfs） |
|---|---|---|
| eMMC 存储 | **JS08AC**（拆解标：济州半导体 JSMC08AUM1ASAEA-H5，eMMC5.1，MLC，3.3V，153FBGA；供应商信息来自拆解丝印，未独立验证） | JEDEC：`/sys/block/mmcblk0/device` → `manfid=0x0000f2`、`oemid=0x01f2`、`name=JS08AC`、`date=07/2024`、`rev=0x8`、`serial=0x…xxxx` |
| └─ CID | `f201f24a53303841430a…xxxx7b00` | `cid`；解码 MID=0xF2、OEM=0x01F2、PNM=`JS08AC`（"S08AC" 6 字节）、序列号省略 |
| └─ 容量 | 约 **7.58GB（≈8GB）** | `/sys/block/mmcblk0/size` = 14778368 扇区 × 512B |
| └─ 分区 | A/B 双槽，共 69 个 user 分区（p1..p69） | `/proc/partitions` + `lsblk` + `/dev/block/bootdevice/by-name`（详见 [partitions.md](partitions.md)） |
| LPDDR4X | 2GB，济州半导体 JSL4BAG167ZAMF-05A | 拆解丝印；在设备上检查 MemTotal 1628588 kB |

> **JEDEC 说明**：内核从 CID 解码出的原始 JEDEC 字段为 `manfid=0x0000F2`、`oemid=0x01F2`、产品名 `JS08AC`、日期 07/2024、`rev=0x8`。这些是本机 sysfs 直接读取的原始值；但**0xF2 具体对应哪家 JEDEC 厂商未能通过权威公开来源交叉验证**，故不作断言。"济州半导体（Jeju Semiconductor）"这一供应商标注来自充电头网拆解报告的丝印解读（型号 `JSMC08AUM1ASAEA-H5`），未经独立二次验证，仅供参考。

### 3.3 电源 / 接口

| 器件 | 型号 | 验证依据 |
|---|---|---|
| PMIC 组 | 高通 **pmk8550 + pmx75 + pm7550ba + pmg1110**（4 颗） | SPMI 平台节点 compatible（`qcom,pmk8550`/`qcom,pmx75`/`qcom,pm7550ba`/`qcom,pmg1110`）；`socinfo.num_pmics=4` |
| PMIC（拆解对照） | **PMX75** + **PM7550BA** | 拆解丝印；在设备上检查 pmx75@1 / pm7550ba@7 节点 |
| 电荷泵快充 | 高通 **SMB1394** | 拆解丝印；在设备上检查 `4-0034` 节点 compatible=`qcom,smb1394`（i2c_pmic 驱动） |
| Type-C/PD 控制器 | Silergy **SY6998** | 在设备上检查 `2-006b` 节点 name=`sy6998` compatible=`silergy,sy6998`（usb_type_c 驱动）；dmesg `sy6998_chipID=0x0` |
| USB-C 复用器 | 艾为 **AW35710**（丝印 GPS5） | 拆解丝印 |
| LED/GPIO 扩展 | 艾为 **AW9523B** | 拆解丝印；在设备上检查 `1-005b` name=`aw9523b` compatible=`aw9523b`（aw9x-led 驱动，背光 `led:lcd`） |
| SAR 传感 | 艾为 **AW9610x** | 在设备上检查 `1-0012` name=`aw9610x_sar` compatible=`awinic,aw9610x_sar` |
| TVS | 豪威 **ESD56161D24** | 拆解丝印 |

### 3.4 NFC

| 器件 | 型号 | 验证依据 |
|---|---|---|
| NFC 接口芯片 | 复旦微 **FM11NT083C**（拆解）；仓库 ref/脚本按 **FM11NT082C** | 拆解丝印；在设备上检查 `zte_topsw_nfc` daemon 运行、NFC 总线 `/dev/i2c-3`、从机 0x57（详见 [nfc.md](nfc.md)） |
| NFC 天线 | 复旦微 FM11NT 系列标签，底部 NFC 线圈 | 拆解（TDFN10 封装）；石墨贴纸内 NFC 线圈 |

---

## 四、显示 / 触摸

### 4.1 显示屏

| 项目 | 值 | 验证依据 |
|---|---|---|
| DRM 设备 | `/dev/dri/card0` | `/sys/class/drm/card0` |
| Connector | `card0-DIN-1`, status=connected | `/sys/class/drm/card0-DIN-1/status` |
| 分辨率 | **320×480**（单 mode） | `/sys/class/drm/card0-DIN-1/modes` |
| 驱动/面板 | **ST7783**（QPIC） | dmesg `qpic_display ... select st7783 panel`；节点 `1c98000.qcom,msm_qpic` compatible=`qcom,mdss_qpic` |

> 拆解称 3.5 寸触摸屏；在设备上检查分辨率为 320×480，链路 DRM+QPIC，详见 [screen.md](screen.md)。

### 4.2 触摸屏

| 项目 | 值 | 验证依据 |
|---|---|---|
| 触摸控制器 | Sitronix **ST77921**（驱动 v41.00.240628） | dmesg `Sitronix ST77921 Touch Driver`；`/dev/input/event3` name=`sitronix_ts_i2c` |
| I2C 总线/地址 | QUPv3 I2C-1 `994000.i2c`，`1-0055` / **0x55** | `/sys/bus/i2c/devices/1-0055`；DT `sitronix@55` compatible=`sitronix_ts` |
| 固件 | version `02`，revision `01.02.01.07` | `sitronix_ts_i2c.0/firmware_version` |
| 协议 | Linux **MT-B**（最多 2 点）+ `BTN_TOUCH` | `ABS_MT_{SLOT,TOUCH_MAJOR,POSITION_X,POSITION_Y,TRACKING_ID,PRESSURE}`；`PROP=2`（`INPUT_PROP_DIRECT`） |
| GPIO | Interrupt=500，Reset=487（`gpiochip379` + DT） | dmesg `sitronix_ts_i2c_parse_dt`；DT 另有 `lcd-pw-gpio` / `lcd-rst-gpio` |
| 手势唤醒 | 开 | `sitronix_ts_i2c.0/gesture` → `Gesture wakup is enable` |
| 输入节点 | event0=pmic_pwrkey, event1=pmic_resin, event2=gpio-keys, event3=touch | `/sys/class/input/event*/device/name` |

---

## 五、电池 / 充电

| 项目 | 值 | 验证依据 |
|---|---|---|
| 模组名 | `7527761_ZTE_MU5250_HIGHPOWER_10000MAH_PM7550B_AVERAGED_MASTERSLAVE_NOV15TH2024` | `/sys/class/power_supply/battery/uevent` → `MODEL_NAME` |
| 技术 | Li-ion，满充电压 4.5V | power_supply `TECHNOLOGY`/`VOLTAGE_MAX=4500000` |
| 设计满充 | 10214000 mAh（≈10000mAh 级） | `CHARGE_FULL_DESIGN` / `CHARGE_FULL=10214000` |
| 供应商 | 惠州豪鹏科技（拆解标注） | 拆解报告参数表 |
| 拆解参数 | 额定 9800mAh/38.32Wh，典型 10000mAh/39.1Wh，标称 3.91V，限压 4.5V | 拆解报告 |
| 充电管理 | PM7550B + SMB1394 电荷泵 | 在设备上检查 SPMI `pm7550ba` + i2c `smb1394` |
| 供电节点 | `charger_zte` / `powerbank_zte`（反向充电宝）/ `type-c_zte` | `/sys/class/power_supply/*/uevent` |

---

## 六、网络接口

| 接口 | 说明 | 验证依据 |
|---|---|---|
| `br-lan`/`eth0` | 有线 LAN 桥 | `/sys/class/net/`，MAC `5c:4d:bf:…`（ZTE OUI，dmesg QCMAP） |
| `wlan0`(2.4G)/`wlan2`(5G) 等 | WiFi（hostapd-daemon） | dmesg hostapd wlan0；`/etc/init.d/hostapd-daemon` |
| `rmnet_data*`/`rmnet_ipa0` | 5G 蜂窝数据链路 | `/sys/class/net/`；IPA 平台节点 `3e00000.qcom,ipa` |
| `wg0`/`wg1` | WireGuard 隧道 | `/sys/class/net/` |
| 以太网 MAC | `5c:4d:bf:…` | dmesg `QCMAP: HW Address ...` |
| 以太网控制器 | 高通 **ethqos（qcom-ethqos）** | dmesg `qcom-ethqos` / `ioss ... qcom-ethqos_ioss` |

---

## 七、系统接口映射

| 硬件 | 系统挂载/接口 | 参考文档 |
|---|---|---|
| 触摸屏 UI（LVGL/DRM/QPIC） | `/dev/dri/card0`, 320×480 | [screen.md](screen.md) |
| NFC 标签（复旦微 FM11NT） | `/dev/i2c-3`, 0x57 | [nfc.md](nfc.md) |
| WLAN 开关（WCN7851） | `uci wireless.zte_mbb` / `zte_topsw_wlan` | [wifi.md](wifi.md) |
| ADB / USB | `a600000.dwc3`, configfs gadget | [adb.md](adb.md) |
| 输入（电源/复位/键/触摸） | `/dev/input/event0..3` | [screen.md](screen.md) |

---

## 八、拆解参考图（充电头网）

> 图片来源：充电头网《拆解报告：ZTE中兴5G随身WiFi U60 Pro》(2025-06-03)，按报告图注下载保存到 `ref/teardown/`，仅供核对器件丝印与布局。

| 器件 | 本地参考图 |
|---|---|
| Wi-Fi 芯片 WCN7851 | `ref/teardown/wcn7851_wifi.jpg` |
| 射频收发器 SDR875 | `ref/teardown/sdr875_rf_trx.jpg` |
| eMMC JSMC08（济州半导体丝印） | `ref/teardown/emmc_jsmc08.jpg` |
| PMIC PMX75 | `ref/teardown/pmx75_pmic.jpg` |
| PMIC PM7550BA | `ref/teardown/pm7550ba_pmic.jpg` |
| 射频前端 FEM S55235-11 | `ref/teardown/rf_fem_s55235.jpg` |
| WiFi FEM（丝印 2H6/2J4） | `ref/teardown/wifi_fem_2h6_2j4.jpg` |
| 电池 10000mAh | `ref/teardown/battery_10000mah.jpg` |
| NFC FM11NT083C | `ref/teardown/nfc_fm11nt083c.jpg` |
| 电荷泵快充 SMB1394 | `ref/teardown/smb1394_charger.jpg` |
| 拆解全家福 | `ref/teardown/teardown_family.jpg` |

## 附：注意事项

1. 拆解器件以充电头网拆解报告为准（部分型号为报告列举的丝印）。
2. 在设备上核对所用的只读节点：`/sys/devices/soc0/*`（socinfo）、`/sys/block/mmcblk0/device/*`（eMMC JEDEC）、`/sys/bus/i2c/devices/*`（I2C 外设）、`/sys/class/drm`、`/sys/class/power_supply`、`/proc/partitions`、`/sys/bus/platform/devices/*/of_node/compatible`、`dmesg`。
3. 涉及改写（NFC、开机画面、WiFi 配置等）见各主题文档；写操作须先行备份并自行确认副作用。

## 相关文档

- [partitions.md](partitions.md) — eMMC 分区
- [screen.md](screen.md) — 显示 / 触摸
- [nfc.md](nfc.md) — NFC
- [wifi.md](wifi.md) — WLAN
- [adb.md](adb.md) — USB gadget / ADB
- [usb-otg-host.md](usb-otg-host.md) — USB OTG / Host
- [powerbank.md](powerbank.md) — 充电宝反向充电
- [README.md](../README.md) — 设备总览
