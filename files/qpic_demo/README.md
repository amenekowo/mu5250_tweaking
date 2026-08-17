# QPIC / DRM screen demo (U60Pro)

`qpic_drm_demo` 通过 **Atomic KMS + dumb RGB565 双缓冲** 向 `/dev/dri/card0` 刷帧，路径与 `zte_topsw_devui` 一致（不用 legacy `SETCRTC` / `image_dump`）。

演示内容：红/绿/蓝全屏 → 马赛克 + `HELLO` → 实时时钟（电源键背景反色）→ **触摸轨迹**（Android Pointer Location 风格，电源键退出）→ 清屏；`run_qpic_demo.sh` 负责停 UI、开背光、跑 demo、恢复 devui。

详细原理与已知问题见 [docs/screen.md](../../docs/screen.md) 方法 4。

## 构建

```bash
cd files/qpic_demo
chmod +x build.sh run_qpic_demo.sh
./build.sh
```

默认 `./build.sh`：首次运行从 [musl.cc](https://musl.cc/) 下载 `aarch64-linux-musl-cross.tgz`（约 100MB，gitignore），在 **Alpine amd64 Docker** 里静态编译。本机需 Docker；Apple Silicon 用 `--platform linux/amd64`（脚本已内置）。

若内网可拉 dockcross：`DOCKCROSS=1 ./build.sh`（镜像 `dockcross/linux-arm64-musl`）。

产物：`qpic_drm_demo`（静态 aarch64-musl，已 gitignore）。

## 部署与运行

设备无 `scp` 时可用 base64 管道：

```bash
base64 < qpic_drm_demo | ssh root@192.168.0.1 \
  'base64 -d > /data/qpic_demo/qpic_drm_demo && chmod +x /data/qpic_demo/qpic_drm_demo'
base64 < run_qpic_demo.sh | ssh root@192.168.0.1 \
  'base64 -d > /data/qpic_demo/run_qpic_demo.sh && chmod +x /data/qpic_demo/run_qpic_demo.sh'

ssh root@192.168.0.1 'sh /data/qpic_demo/run_qpic_demo.sh 12'
```

有 ADB/SCP 时也可 push 到 `/data/qpic_demo/` 后执行 `run_qpic_demo.sh`。

暂停画面改为触摸轨迹：在屏上划线，按 **电源键**（`event0`）结束；脚本 `trap` 会恢复 `zte_topsw_devui`。触摸走 Sitronix ST77921 的 `/dev/input/event3`（MT protocol B），demo 用 `EVIOCGABS` 映射到 320×480。

## 注意

- 必须先停 `zte_topsw_devui`（独占 DRM master）。
- **背光**：`/sys/class/leds/led:lcd/brightness` 须设为 255；devui 退出后常为 0，帧在刷但屏幕全黑。脚本已自动处理；勿用 `ubus call zwrt_bsp.led set`（实测会超时）。
- 日志：`/root/qpic_demo.log`。
