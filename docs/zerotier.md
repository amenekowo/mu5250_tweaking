# ZeroTier 安装与运行

> **验证说明**：本文整理仓库内已有的安装做法与 `files/init.sh` 中的启动方式。ZeroTier 官方包的网络 ID、授权等配置**不在本仓库**，须按所用网络自行填写。解包安装与 `service zerotier start` 属写操作，执行前须备份并自行确认副作用。

## 为何 `opkg install zerotier` 失败

README「一些注意事项」已说明：内核为中兴定制版（非官方 OpenWrt 内核），`core` 源不适用，`kmod` 依赖（如 `kmod-tun`）在已配置的架构与源中找不到，直接 `opkg install zerotier` 会报：

```
Unknown package 'zerotier'.
 * pkg_hash_check_unresolved: cannot find dependency kmod-tun for zerotier
 * pkg_hash_fetch_best_installation_candidate: Packages for zerotier found, but incompatible with the architectures configured
```

## 手动安装（解包 ipk）

将 opkg 的 `.ipk` 下载后解包，把 `data.tar.gz` 解压到根目录（`/usr` 已 overlay 到 `/data` 时文件可持久化）：

```sh
tar -zxvf zerotier_xxx.ipk            # 解出 control/data 等
tar -zxvf data.tar.gz -C /            # 把二进制/脚本放到根目录
```

若缺少内核模块依赖（如 `kmod-tun`），须自行交叉编译或寻找与当前内核匹配的模块。README 未给出具体来源。

## 开机自启动

仓库 `files/init.sh` 在启动时执行：

```sh
for service in sshd uhttpd vsftpd zerotier; do
    /etc/init.d/$service start
done
```

即 `zerotier` 随 `init.sh` 启动。`init.sh` 由 `/etc/rc.local` 以 `(sleep 10; /bin/sh /data/init.sh ...) &` 调用（见 README「ssh持久化及开机启动adb」）。

## 手动控制

```sh
service zerotier start     # 启动
service zerotier stop      # 停止
service zerotier restart   # 重启
```

加入网络与节点认证按 ZeroTier 官方客户端操作（本仓库不提供配置示例）。
