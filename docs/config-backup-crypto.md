# 配置备份解密（back_parameter）

> **验证说明**：本文基于 `zte_topsw_mc`、`backup_config.sh`、`libztecrypto.so` 的静态分析，全盘备份 `playground/full_disk_bkp.img` 上的交叉核对，以及在设备上运行 `MBB_decryption` 探针的解密验证。分析产物位于 `playground/issue4_analysis/`、`playground/decrypt_test/`（已列入 `.gitignore`，不纳入版本库）。口令派生与 `back_parameter` 解密已验证成功。`backup_config.sh` 的 `restore` 会改写设备配置，**尚未在设备上执行**；写操作须先行备份并自行确认副作用。

`backup_config.sh` 生成配置备份文件 `back_parameter`。本文说明其解密方法与口令来源。UCI 配置见 [uci-config.md](uci-config.md)。本文仅涉及配置备份加解密。

## 概述

- 备份脚本：`/sbin/backup_config.sh`（本仓库已收录到 `playground/issue4_analysis/sbin/backup_config.sh`，与本设备一致）
- 备份文件：`/tmp/back_parameter`（导出即本库 `playground/issue4_analysis/back_parameter`）
- 加密算法：3DES-CBC（`openssl enc -des-ede3-cbc`）
- 口令模式：openssl 口令模式（`-pass pass:<口令>`，产生 `Salted__` 头），**非** `-K/-iv` 的 AES 模式
- 口令来源：UCI 配置 `zwrt_zte_mc` 的 `back_restore_enckey` + 设备 IMEI。真正口令 = `IMEI + MBB_decryption(back_restore_enckey)`（见「口令派生」）
- UCI 中的 `back_restore_enckey` 为**密文**。`zte_topsw_mc` 先经 `MBB_decryption` 解密得到口令后缀，再拼上 IMEI 前缀，才作为 openssl 口令。直接把 `back_restore_enckey` 当作口令会得到 `bad decrypt`。

## 口令

**口令 = `<IMEI>` + `MBB_decryption(back_restore_enckey)`**（已在设备上验证，见「口令派生」）。

- `back_restore_enckey` 的**原始值不是** openssl 口令本身。它经 `libztecrypto.so` 的 `MBB_decryption`（AES-128-CBC，密钥为三因素派生的 `g_common_dk`）解密后得到**口令后缀**；真正口令还须在前面拼上设备 IMEI。
- 在设备上，由 `back_restore_enckey` 还原出的后缀为只含可打印字符的短串（非 48 字节原文）；拼上 IMEI（15 位）后即得到 32 字节 openssl 口令，可成功解开 `back_parameter`。
- 具体解密在设备端运行探针完成（见 `playground/decrypt_test/`）。IMEI 与 enckey 均为设备私密信息，本文不写完整口令。

```sh
uci get zwrt_zte_mc.@zudata_device_backup[0].back_restore_enckey   # 密文，非口令
uci get zwrt_zte_mdm.device_info.imei                              # IMEI（口令前缀）
```

> 真正口令为设备相关且按版本 / 时段变化。旧导出的 `back_parameter` 若与当前 enckey（或其还原出的口令）不匹配则无法解开（见文末「常见问题」）。

## 解密步骤

在**与设备 openssl 一致的环境**（设备 openssl 3.0.13，使用 legacy EVP_BytesToKey）中执行：

```sh
# 0) 取口令 = IMEI + MBB_decryption(back_restore_enckey)
#    须在能访问 libztecrypto.so 的环境（如本设备）运行探针取得还原后的后缀：
#    PLAIN=$(/tmp/mbb_decrypt_test "$(uci get zwrt_zte_mc.@zudata_device_backup[0].back_restore_enckey)")
IMEI=$(uci get zwrt_zte_mdm.device_info.imei)
PWD="${IMEI}${PLAIN}"        # 32 字节，实测可解

# 1) 解外层：3DES-CBC + EVP_BytesToKey(sha256)
openssl enc -d -des-ede3-cbc -md sha256 \
  -in back_parameter -out outer.tgz \
  -pass pass:"$PWD"

# 2) outer.tgz 为 gzip 外层，内含两层
#    tmp/back_parameter_r1.tgz（真正的配置包）
#    tmp/back_parameter_r.md5（r1.tgz 的 MD5）
tar xzf outer.tgz

# 3) 校验内层完整性（两个值应一致）
cat tmp/back_parameter_r.md5
md5sum tmp/back_parameter_r1.tgz

# 4) 解出真正的 UCI 配置（约 170 个文件：network/wireless/uhttpd/fstab/...）
mkdir -p inner && tar xzf tmp/back_parameter_r1.tgz -C inner
# 配置在 inner/etc/config/ 下
```

## 解密结构

```
back_parameter (openssl 3DES-CBC, Salted__)
└─ outer.tgz (gzip)
   ├─ tmp/back_parameter_r1.tgz  (gzip, 真正的 /etc 配置 sysupgrade.conffiles)
   └─ tmp/back_parameter_r.md5    (r1.tgz 的 md5，用于校验)
```

## 口令派生

### 结论（已在设备上验证）：口令 = `<IMEI>` + `MBB_decryption(back_restore_enckey)`

本节依据 `playground/full_disk_bkp.img`（本设备全盘备份）静态交叉核对，以及在设备上运行 `MBB_decryption` 探针验证。`back_restore_enckey` 的原始 base64 字符串并不是 `backup_config.sh` 的 openssl 口令。真正口令 = **IMEI 前缀 + `MBB_decryption(back_restore_enckey)` 还原出的后缀**。用该口令执行 `openssl enc -d -des-ede3-cbc -md sha256 -pass pass:"<IMEI><后缀>"` 可解开 `back_parameter` 为 gzip `outer.tgz`，其内 `back_parameter_r.md5` 与 `md5sum back_parameter_r1.tgz` 一致，再解出 170 个 UCI 配置文件。

#### 全盘备份上的静态分析

- `zte_topsw_mc`（与 `issue4_analysis/usrbin/zte_topsw_mc` 逐字节一致）的 `ubus_backup_proc` / `ubus_restore_proc`：
  - 通过 UCI 读取 `back_restore_enckey`（`0x15bcc` / `0x15f30`，字符串 `0x20c5e`）。
  - 调用子函数 `0x6a04`，其内部先 `MBB_cipher_init`（GOT `0x3fd60`），再 `MBB_decryption(flag=0, enckey, out, &len)`（GOT `0x3fae0`，`0x15bf4` / `0x15f58`），得到真正口令。
  - 口令的第二段 `%s` 来自 IMEI 前缀（`zte_topsw_mc` 从 `device_info.imei` 取），第三段 `%s` 为 `MBB_decryption` 还原出的后缀；即 `$2 = IMEI + 后缀`。最终命令如 `backup_config.sh checkRestoreFile <IMEI><后缀>`（`sprintf` 于 `0x15c78` / `0x15fe4` / `0x16000`）。
- `backup_config.sh`（与 `issue4_analysis/sbin/backup_config.sh` 逐字节一致）的 `backup` / `restore` / `checkRestoreFile` 分支为 `openssl enc -des-ede3-cbc -pass pass:$2`（口令模式，产出 `Salted__` 头）；`backup_aes` / `restore_aes`（`-K/-iv`）在本设备的 `zte_topsw_mc` / `zte_web` 中未被引用。
- 实测样本 `back_parameter` 带 `Salted__` 头（口令模式），与 3DES 口令路径吻合。
- UCI `/etc/config/zwrt_zte_mc` 实测 `back_restore_enckey` base64 解码 48 字节；`cakey_restore_enckey` base64 解码 32 字节。

因此口令并非 enckey 原文，而是 **IMEI 前缀 + `MBB_decryption(back_restore_enckey)` 后缀**。其中 `MBB_decryption`（AES-128-CBC 解密 `base64(enckey)`，`enckey` 为密文形式；见 `libztecrypto.so` `0x283c`：base64 解码 → AES-128 key schedule（0x100）→ CBC 16 字节块循环）还原出的后缀是**短的可打印口令串**，拼上 IMEI 后共 32 字节。

#### 三因素与 `g_common_dk` / `g_priv_dk`

- `ZTE_PKCS5_pbkdf2_hmac_sha256_device_key_generate`（三因素：`/proc/zte_unique_id`、`/proc/zte_soc_id`、硬编码 `dkz8`；salt=`/etc_ro/gsspone`，实测内容为设备相关字符串，**不转录**；PBKDF2-SHA256，迭代 `0x7d0=2000`，输出长度由调用方指定）生成 `g_common_dk` / `g_priv_dk`。
- `g_common_dk` / `g_priv_dk` 是 **`MBB_decryption` / `MBB_encryption` 用的 AES 密钥**（`flag=0→g_common_dk`，`flag=1→g_priv_dk`），亦即解开 `back_restore_enckey` 得到备份口令的钥匙。
- `back_restore_enckey` 并非等于 `g_common_dk`，而是**用 `g_common_dk` 加密后的密文**。
- `libzteencrypt.so` 内的 `j38s`（`fac_open` / `adb_switch` 工厂口令后缀 `%s.%s.%s`=MAC.IMEI.后缀）是又一条独立用途，与配置备份无关。

验证工具见 `playground/decrypt_test/`（aarch64-musl 探针 `mbb_decrypt_test`，`build.sh` 用 Docker / musl.cc 交叉编译，`run_on_device.sh` 推送到设备并解密；探针的 `MBB_decryption` 第 5 参 `*ctx` 需预置输出容量，否则返回 -6）。

## 常见问题

- **直接 `bad decrypt` 先查口令形式**：`back_restore_enckey` 原始值是密文，须先经 `MBB_decryption` 还原真正口令（见「口令派生」）；直接把它当 `-pass` 口令必然 `bad decrypt`。
- **旧文件解不开**：若 `back_parameter` 是由旧 enckey 或 web/cgi（`IMEI + 固定词`）路径导出，而当前 UCI 的 `back_restore_enckey`（或其还原出的口令）已变化，则当前口令无法解密。须用与新文件同一版本的 enckey / 口令，或重新导出。
- **openssl 版本**：本地新版 openssl 默认可能走 PBKDF2，需加 `-md sha256`（EVP_BytesToKey）与设备保持一致；否则出现 `bad decrypt`。
- **不要公开口令**：`back_restore_enckey` 为设备相关密钥，勿提交到公开仓库；本文示例用占位。

## 相关文件

- 脚本：`playground/issue4_analysis/sbin/backup_config.sh`
- 二进制：`playground/issue4_analysis/usrbin/zte_topsw_mc`、`usrlib/libztecrypto.so`、`usrlib/libzteencrypt.so`
- 备份样本：`playground/issue4_analysis/back_parameter`
- 解密工具：`playground/decrypt_test/`（`mbb_decrypt_test.c` 探针 + `build.sh`(Docker/musl.cc aarch64) + `run_on_device.sh`）

## 相关文档

- [uci-config.md](uci-config.md) — UCI 配置（含 `zwrt_zte_mc`）
- [README.md](../README.md) — 设备总览
