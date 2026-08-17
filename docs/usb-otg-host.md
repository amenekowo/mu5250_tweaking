# USB OTG / Host

> **验证说明**：本文基于对设备固件脚本（`/sbin/start_usb`、`/etc/init.d/usb.init`）的静态分析，以及在设备上的只读检查（device-tree、sysfs、`dmesg`、已加载模块）。检查时 USB 处于 `usb_role=device`（peripheral / ADB）。向 `ssusb/mode` 或 `usb_role` 写入 `host`、插入 USB 从设备以验证 host 枚举等写操作**尚未在设备上执行**；执行前须备份并自行确认副作用。

## 一、硬件与驱动

- USB 控制器为高通 **DWC3 双角色**控制器，节点 `a600000.ssusb`，其下 DWC3 子节点 `a600000.dwc3`。
- 驱动栈（在设备上检查 `/sys/bus/platform/drivers`）：
  - `msm-dwc3`（绑定 `a600000.ssusb`，即 DWC3 wrapper）
  - `dwc3`（绑定 `a600000.dwc3`，DWC3 核心）
  - `dwc3-qcom`、`dwc3-of-simple`、`msm-usb-ssphy-qmp`（USB3 SS PHY）、`msm_eusb2_phy`（USB2 PHY）
  - `eusb2-repeater`（PMIC 上的 USB2 信号整形）
  - host 侧 HCD：`xhci-hcd`（DWC3 内部挂载 xHCI host controller）
- 已加载相关内核模块（`/sys/module`）：`dwc3_msm`、`xhci_hcd`、`usbcore`、`usb_storage`、`configfs`、`ax_usb_nic`（USB 网卡）。

## 二、双角色（DR/OTG）能力确认

**DT 属性（在设备上检查 `/proc/device-tree`）：**

- `ssusb@a600000` 节点带 **`usb-role-switch`** 属性，并有 `extcon`、`USB3_GDSC-supply`、`qcom,use-eusb2-phy`、`qcom,disable-host-ssphy-powerdown` 等。
- DWC3 子节点 **`dwc3@a600000/dr_mode = otg`** —— 明确配置为 OTG 双角色，而非固定 peripheral/host。
- 总线 `ssusb@a600000` 自身无 `dr_mode`，角色由子节点 `otg` + 上层 USB-role-switch 决定。

**角色控制器（sysfs）：**

- `/sys/class/usb_role/a600000.ssusb-role-switch/role`（可写）
- `/sys/class/usb_role/a600000.dwc3-role-switch/role`（可写）
- `/sys/bus/platform/devices/a600000.ssusb/mode`（可写，当前值 `peripheral`；可选 host/peripheral/none）

在设备上检查时的状态：`usb_role = device`、`ssusb/mode = peripheral`，即处于 **gadget（device）模式**。

**内核 role 切换日志（在设备上检查 dmesg）：**

```
dwc3_msm_usb_role_switch_set_role role[2]     # role 2 = device
msm-dwc3 a600000.ssusb: [ZTE_USB] restart peripheral ON
msm-dwc3 a600000.ssusb: DWC3 exited from low power mode
```

`role[2]` 与「重启 peripheral」对应，说明切换由 `dwc3_msm` 的 role-switch 回调驱动。

## 三、Type-C / 双角色电源（DRP、PD）

- Type-C TCPC 初始化（dmesg）：`TCPC-TYPEC:typec_init: SRC`、`Unattached.SRC`，且 `zte_qcom_pm7550b_typec_port not ready` 后成功 `register typec port (0)` —— USB-C 口实为 **DRP（双角色电源）**，可作 source 或 sink。
- `ztedev`/`zwrt_bsp.typec` 通过 CC 线检测方向（`zwrt_bsp.usb` 的 `typec_cc` 字段当前为 `cc2`）。
- 在设备上检查时的 Type-C 状态（`/sys/class/typec/port0`）：
  - `data_role: host [device]`（当前 device，host 为候选）
  - `power_role: source [sink]`
  - `power_operation_mode: 3.0A`、`usb_power_delivery_revision: 3.0`、`usb_typec_revision: 1.3`
- `data_role` / `power_role` 均为 root 可写，可在运行时强制切换 role。

## 四、Host / Device 切换的软件逻辑（start_usb）

`/etc/init.d/usb.init` 的 `start()` 只调用 `/sbin/start_usb init`。`start_usb` 内有三条与 role 切换相关的路径：

**1) `usb_bind()` —— 按当前 role 决定是否让出 UDC（核心逻辑，设备上的脚本）**

```sh
usb_bind() {
	udcname=`ls -1 /sys/bus/platform/devices/ | grep -E 'hsusb|ssusb' | head -n 1`
	while : ; do
		cur_role=`cat /sys/bus/platform/devices/${udcname}/mode`
		if [ $cur_role == "host" ]; then
			break                       # host 模式下直接让出，不绑 UDC 给 gadget
		fi
		UDC=`ls -1 /sys/class/udc | head -n 1`
		if [ ! -z $UDC ]; then
			echo $UDC > /sys/kernel/config/usb_gadget/g1/UDC   # device 模式绑回 gadget
			break
		fi
		# 超时后回退：强制设 peripheral 并绑 UDC
		echo peripheral > /sys/bus/platform/devices/${udcname}/mode
		sleep 2
		echo $UDC > /sys/kernel/config/usb_gadget/g1/UDC
		break
	done
}
```

- `usb_bind` 在 `start_usb init` 时以 `usb_bind &` 后台启动（行 479）。
- 逻辑要点：**若当前 role 为 host，则 UDC 不绑定到 configfs `g1`**，从而把 USB 口让给 host（xHCI）控制器去枚举外部从设备；否则绑回 `g1` 使用 ADB/存储等组合。

**2) `start_usb init` 开头的 role 判断（设备上的脚本，行 42-59）**

```sh
udcname=`ls -1 /sys/bus/platform/devices/ | grep -E 'hsusb|ssusb' | head -n 1`
cur_role=`cat /sys/bus/platform/devices/${udcname}/mode`
if [ $cur_role == "host" ]; then
	# ...即为 host 分支
fi
# 否则
echo peripheral > /sys/bus/platform/devices/${udcname}/mode
```

**3) `bind_udc` 子命令 —— 退出 host 模式后重绑（设备上的脚本，行 512-540）**

```sh
bind_udc)
	# owrt targets 无 usbd/udev，靠 hotplug.d 在退出 host 后重绑 UDC；
	# 期望以 USB2.0 roothub 移除作为 host→device 切换信号。
	udcname=`ls -1 /sys/class/udc | head -n 1`
	if [ ! -z $udcname ]; then
		checkudc=$(cat /sys/kernel/config/usb_gadget/g1/UDC)
		if [ -z $checkudc ]; then
			/etc/init.d/adbd.init restart   # 重绑前恢复 adbd
			sleep 1
			echo $udcname > /sys/kernel/config/usb_gadget/g1/UDC
		fi
	fi
```

> `bind_udc` 子命令虽然存在，但**当前固件没有任何脚本调用它**（`grep bind_udc/usb_bind` 仅在 `start_usb` 自身命中），且 `/etc/hotplug.d/usb/` 是**空目录**——即「靠 hotplug.d 重绑 UDC」目前是设计意图/遗留，实际未在 OpenWrt 侧落地规则。当前 device 模式由 `usb_bind` 正常绑回 UDC 保证。

## 五、Host 应用：usb2rj45（USB 转 RJ45 有线网卡）

- 已加载 `ax_usb_nic` 驱动（`/sys/module/ax_usb_nic/drivers/usb:ax_usb_nic`），这是 **asix 系 USB 以太网卡**驱动——典型 host 应用：把 USB 口当 host 去接一个 USB-to-RJ45 网卡，实现有线回程/旁路。
- `zwrt_bsp.usb` 的 `list` 返回含 `usb2rj45` 字段（当前 `0`），即该 host 网卡功能由 USB BSP 控制对象管理，可开/关。
- USB 口在「device（ADB/上网/存储）」与「host（扩展有线网卡 usb2rj45）」之间由 `zwrt_bsp.usb` 驱动动态复用。

## 六、控制入口与 mode 语义

- `zwrt_bsp.usb` 对象（`zte_ubus_bsp_usb`）提供 `list` / `set{mode}`。
- 其 mode 字符串集合**不包含直接的 `host` 值**（strings 仅见 `debug` 及 power 相关 `mode_power_*`）——因此 host 模式下不是通过 `set mode=host` 简单触发，而是由 **Type-C DRP 检测 / DWC3 usb-role-switch + `usb_bind` 的角色判断**决定 USB 口作为 host 还是 device。
- device 模式的组合（ADB/debug 等）仍由 `zwrt_bsp.usb set mode=debug` + `usb_switch` 重建 configfs 组合完成（详见 [adb.md](adb.md)）。

## 七、总结

ZTE zwrt（sdx75/U60Pro）的 USB OTG/host 实现要点：

1. **硬件**：高通 DWC3 双角色控制器，`dr_mode=otg`，USB-C 为 DRP（Type-C/PD 3.0）。
2. **角色切换**：由 `dwc3_msm` 的 usb-role-switch 回调 + Type-C TCPC 驱动，通过 `/sys/class/usb_role/<...>/role` 与 `ssusb/mode` 暴露，`data_role`/`power_role` 可写。
3. **软件分派**：`start_usb` 的 `usb_bind` 根据 `ssusb/mode`/role 决定是否把 UDC 绑给 configfs gadget；host 模式让出 UDC，device 模式绑回。
4. **host 用途**：主要是 `usb2rj45`（asix `ax_usb_nic` 网卡）。
5. **已知问题**：`hotplug.d/usb` 为空、`bind_udc` 无调用方——主机切换后的自动化重绑脚本未落地，属设计遗留。

## 八、未决事项

Host 模式尚未在设备上用外接从设备验证。若需验证：在 USB-C 口插入 USB 从设备（U 盘或网卡），观察 `dmesg` 是否出现 xHCI 枚举（`New USB device found`）、`/sys/bus/usb/devices` 是否出现节点，以及 `usb_role` 是否变为 `host`。

> **写操作，尚未执行**：`echo host > /sys/bus/platform/devices/a600000.ssusb/mode`（或写入 `usb_role`）可强制切换角色。该操作会改变 USB 口工作模式（ADB 等 gadget 将不可用），执行前须备份并自行确认副作用。

## 相关文档

- [adb.md](adb.md) — USB gadget / ADB（`zwrt_bsp.usb set mode=debug`）
- [hardware.md](hardware.md) — DWC3 / Type-C / SY6998
- [powerbank.md](powerbank.md) — Type-C PD 与反向供电
- [README.md](../README.md) — 设备总览
