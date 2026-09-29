# ke-body — 小身体固件

目标板：微雪 **Waveshare ESP32-S3-Touch-LCD-3.5-C**（带外壳 + 背面 OV5640 摄像头）。
「-C」是在 **ESP32-S3-Touch-LCD-3.5** 同一块主板上加外壳和摄像头模组的套装，主板电路、引脚与不带 C 的版本相同（微雪官方仓库只有一个 `ESP32-S3-Touch-LCD-3.5`，Arduino/ESP-IDF 示例通用，其中 `03_camera_web_server` / `06_lvgl_camera` 就是给 -C 的摄像头用的）。

| 版本 | 固件 | 内容 | 真机状态 |
|------|------|------|----------|
| v1 | `release/ke-body-v1.bin` | 第一期：脸、配网、HTTP `/face /say /ping`、触摸脸红 | 已上板验证 |
| v2 | `release/ke-body-v2.bin` | + 第二期：按住说话录音上传、`/play` 播放、`/volume`、电脑端脚本 | 已上板：启动、Wi-Fi、HTTP、`face/say` 正常；**喇叭无声**；录音未测 |
| v3 | `release/ke-body-v3.bin` | + 第三期：AXP2101 电源初始化、横屏、亮度、夜间变暗、黑底白字、串口诊断命令 | 已上板：PMIC 应答、电源全开、ES8311 寄存器正确，**喇叭仍无声** |
| v3.1 | `release/ke-body-v3.1.bin` | + 不用耳朵的音频诊断：`audio test` 内部回环峰值、`audio mic`、`audio slot`、`gpio` 命令 | 已上板：回环 R=8000 证明 I2S/DAC 通；**找到功放使能 = TCA9554 P7** |
| v3.2 | `release/ke-body-v3.2.bin` | + 开机接管 TCA9554 P7（播放时开功放、空闲关）；音量改为分贝映射 | 已上板：出声；默认 10 太小 |
| v3.3 | `release/ke-body-v3.3.bin` | + 默认音量 18；表情字库补 ˍ 皿 ﹏；💢 💧 做成线条图标 | 已上板：屏幕、触摸、横屏 270、黑底、亮度、夜间变暗、喇叭、回环、麦克风都正常 |
| v4 | `release/ke-body-v4.bin`（合并）<br>`release/ke-body-app-v4.bin`（只含应用） | + 第四期：聊天记录 + 快捷按钮、动画、QMI8658 摇晃/扣桌、静音提醒、相机/相册/寄照片/远程 /snap、bridge 收 /msg /photo | **未在真机运行过**，见 §6 |

> 本固件在没有实物的环境里编写和编译。§6「未验证事项」列出了需要上板确认的点，请按顺序核对。

---

## 1. 功能

### 第一期（脸）

| # | 功能 | 说明 |
|---|------|------|
| 1 | 开机显示「脸」 | 中间大号颜文字，默认 `(—_—)` |
| 2 | 连 Wi-Fi（2.4G） | 账号密码只存 NVS，**不在仓库里**。没配置时屏幕显示「等待配网」 |
| 3 | 串口配网 | 串口命令 `wifi <ssid> <password>` 写入 NVS 后自动重启 |
| 4 | 显示 IP | 连上后右上角小字显示 IP |
| 5 | HTTP 接口 | `GET /ping` → `ok`；`POST /face`；`POST /say` |
| 6 | 触摸 | 短按屏幕任意位置，脸变成 `(—//—)`，2 秒后恢复 |

### 第二期（耳朵和嘴）

| # | 功能 | 说明 |
|---|------|------|
| 7 | 按住说话 | 按住 ≥0.5 秒开始录音（ES8311 麦克风，16kHz 单声道 16bit），松手结束，最长 30 秒。录音时脸 `(—o—)`，角落「在听」 |
| 8 | 录音上传 | 松手后把 WAV `POST` 到 NVS 里保存的地址（`Content-Type: audio/wav`），串口命令 `server <url>` 设置；没设置时气泡提示 |
| 9 | 出声 | `POST /play` body 是 WAV（16kHz 或 24kHz 单声道 16bit），喇叭播放，播放时脸 `(—▽—)` |
| 10 | 音量 | 默认 18；`POST /volume` body 0–100；串口 `volume <n>`。0 = 静音，100 = 芯片最大（+32dB），每 10 格约 6dB |
| 11 | 电脑端 | `pc/ke_bridge.py` 收录音存文件；`pc/ke_send.py` 给板子发 face/say/play/volume… |

### 第三期

| # | 功能 | 说明 |
|---|------|------|
| 12 | 修喇叭 | **功放使能是 TCA9554 P7（拉高开）**，v3.2 起播放时开、空闲时关。另：开机按官方例子初始化 AXP2101 电源轨；ES8311 初始化补齐官方 esp_codec_dev 驱动写的寄存器；串口 `pmic` / `tca` / `audio test` / `audio mic` / `audio regs` 诊断命令 |
| 13 | 横屏 | `rotate 0|90|180|270`、`POST /rotate`，存 NVS，**默认 90**。触摸坐标跟着转；`touchlog on` 打印坐标 |
| 14 | 亮度 | `bright <5-100>`、`POST /brightness`，存 NVS，默认 80 |
| 15 | 夜间变暗 | 连网后 NTP 对时（`ntp.aliyun.com`，备用 `pool.ntp.org`，东八区）。`night 23:00 07:00 20` 存 NVS，`night off` 关闭。**默认开，23:00–07:00 亮度 20** |
| 16 | 黑底白字 | `theme dark|light`、`POST /theme`，存 NVS，**默认 dark**（气泡深色底浅色字） |

### 第四期

| # | 功能 | 说明 |
|---|------|------|
| 17 | 只刷应用区 | 每版另出 `release/ke-body-app-vX.bin`，烧到 **0x10000**，NVS 里的 Wi-Fi、server、rotate、亮度、按钮等全部保留 |
| 18 | 快捷按钮 | 屏幕下方两排按钮（默认 想你了 / 抱抱 / 在干嘛 / 晚安 + 一排表情），点一下发到 bridge `/msg` 并进聊天记录；`POST /buttons` JSON 配置，存 NVS |
| 19 | 聊天记录 | 脸常驻顶部，下面是聊天：克的回复（`/say`）在左，她发的（按钮、摇晃、`/heard` 语音转的字）在右，上下滑动，最近 50 条（PSRAM） |
| 20 | 六轴 QMI8658 | 摇两下 = 发一条按钮配置里的 `shake` 文字（默认「想你了」）；屏幕朝下 1.5 秒 = 睡脸 + 背光压到 5；拿起或摸一下恢复。`autorotate on` 可选自动转屏（默认关，不改 rotate 设置） |
| 21 | 静音提醒 | 收到 `/say` 时屏幕边框轻闪两下；`chime on` 再加一段很轻的两音提示（默认关） |
| 22 | 相机 | 聊天界面「相机」键进相机：实时取景（约 5–10 fps），点取景或「拍照」拍一张 SXGA 1280×1024 JPEG，存 SD 卡 `/sdcard/DCIM/`（没卡存内部 Flash 最多 12 张）；「相册」左右滑看、删除、「寄给克」（`POST /photo`，**不点绝不上传**） |
| 23 | 让克看看 | `peek on` / `POST /peek on`（存 NVS，默认关）时电脑可 `GET /snap` 远程拍一张直接回传 JPEG，屏幕角落常亮小眼睛；关着时 `/snap` 一律 403 |
| 24 | 小动画 | 待机每 3–13 秒随机眨眼；睡脸时 z/Z 从脸右边往上飘淡出；摸脸「//」粉色渐显渐隐；摇晃时脸左右抖两下；`anim off` 全关 |

语音转文字、文字转语音不在固件里，电脑那边另外接。

---

## 2. 烧录（Windows）

1. 用 USB 线接板子的 **USB 口**（ESP32-S3 内置 USB-Serial/JTAG，设备管理器里是一个 COM 口，下面假设是 `COM3`）。
2. 安装 esptool：`pip install esptool`
3. 一行烧录（合并好的单文件固件，从地址 0 写入）：

```
python -m esptool --chip esp32s3 --port COM3 write-flash 0x0 release/ke-body-v3.3.bin
```

**第四期起只刷应用区**（保留 NVS 里的所有设置）：

```
python -m esptool --chip esp32s3 --port COM3 write-flash 0x10000 release/ke-body-app-v4.bin
```

注意：v4 改了分区表（多了一个 4MB 的 `storage` 分区给照片用），**从 v3.x 升到 v4 的第一次必须烧合并版** `ke-body-v4.bin`（从 0x0），之后的 v4.x 才能只刷 0x10000。合并版从 0x0 整片写入，NVS 区（0x9000）会被写成 0xFF，也就是**合并版会清掉 Wi-Fi、server 等设置**，烧完要重新 `wifi` / `server`；只刷应用区不会。

（旧版本文件都保留，换文件名即可回退。）

如果板子没自动进入下载模式：按住 **BOOT** 键，按一下 **RESET**，松开 BOOT，再执行上面的命令。
烧完后按一下 RESET（或重新插电）。冷上电后固件会主动软复位一次（官方例子的做法），属正常现象。

如果 esptool 提示 flash 大小和固件头不符，可以加 `--flash-size 16MB`（微雪官方示例配置为 16MB Flash，QIO 80MHz）。

注意：上面是 esptool **v5** 的写法（本仓库用 esptool v5.4.0 生成固件）。如果你装的是 esptool v4，子命令是下划线：`write_flash`。想快一点可以加 `-b 921600`。

`release/ke-body-vX.bin` 内容 = bootloader（0x0）+ 分区表（0x8000）+ 应用（0x10000），中间用 0xFF 填充；`ke-body-app-vX.bin` 只有应用（烧到 0x10000）。v4 合并版约 3.4MB。

---

## 3. 配网与使用

### 3.1 串口命令

烧录完，用任意串口终端（PuTTY / Tera Term / `python -m serial.tools.miniterm COM3 115200`）打开同一个 COM 口，会看到 `ke-body>` 提示符：

| 命令 | 作用 |
|------|------|
| `wifi <ssid> <password>` | 保存 Wi-Fi 凭据到 NVS 并重启。含空格用双引号：`wifi "My Home" "pass word"`；开放网络密码留空 |
| `wifi` / `wifi clear` | 显示当前 SSID、状态、IP / 清除凭据并重启 |
| `server http://192.168.1.5:8770/hear` | 录音上传地址（电脑上 `ke_bridge.py` 的地址），立即生效 |
| `server` / `server clear` | 显示 / 清除 |
| `volume <0-100>` / `volume` | 喇叭音量（只在内存里，重启回到默认 18）。0 静音，100 = 芯片最大，每 10 格约 6dB |
| `rotate 0|90|180|270` / `rotate` | 屏幕方向，存 NVS，立即生效 |
| `bright <5-100>` / `bright` | 背光亮度，存 NVS |
| `night 23:00 07:00 20` / `night off` / `night` | 夜间自动变暗：开始 结束 亮度；`night` 显示当前时间、是否已对时、是否处于夜间 |
| `theme dark|light` / `theme` | 黑底白字 / 白底黑字，存 NVS |
| `touchlog on|off` | 按住屏幕时打印原始坐标和换算后的坐标（每 200ms 一行） |
| `pmic` / `pmic <rail> on|off` / `pmic init` | AXP2101：打印所有电源轨状态和电压 / 单独开关某一路（`dc1..dc5 aldo1..aldo4 bldo1 bldo2 dldo1 dldo2 cpusldo`）/ 重新跑一遍初始化 |
| `tca` / `tca <pin> 0|1|in` | TCA9554 扩展 IO：打印 8 个引脚状态 / 把某脚设成输出低、输出高或输入（**pin 1 是屏幕复位，pin 7 是功放使能，固件自己管**） |
| `audio test [ms] [rate]` | 固件内部生成 1kHz 正弦音（默认 1 秒、16k）走正常播放通道，同时抓 ADC 回环并打印 L/R 峰值（见 §3.5） |
| `audio mic [ms]` | 录 ms 毫秒，打印 L/R 峰值和 RMS |
| `audio slot mono|stereo` | I2S 槽位模式切换（默认 stereo，与官方一致） |
| `gpio <n> 0|1|in` | 驱动一个空闲 ESP32 引脚，找功放使能用；占用引脚会拒绝 |
| `audio regs` | 打印 ES8311 全部寄存器和芯片 ID |
| `audio gain <0-7>` | 麦克风 PGA 增益，0=0dB，每步 6dB，默认 5（30dB） |
| `msg <文字>` | 等于按了一个快捷按钮：进聊天记录并发到 bridge `/msg` |
| `buttons` | 打印当前按钮配置 JSON |
| `anim on|off` | 小动画总开关，存 NVS |
| `chime on|off` | 收到消息时是否响一声，存 NVS，默认关 |
| `imu` / `imu invert on|off` | 打印加速度；如果「扣在桌上」方向反了（拿起来反而睡），`imu invert on` |
| `autorotate on|off` | 随手转屏（默认关；轴向未验证） |
| `cam on|off|shot|gallery` | 进/出相机、拍一张、进相册 |
| `cam vflip on|off` / `cam mirror on|off` | 画面上下翻/左右镜像，存 NVS（默认 vflip on，同官方例子） |
| `peek on|off` | 「让克看看」远程拍照开关，存 NVS，默认关 |
| `photos` | 列出照片（SD 卡或内部） |
| `face <颜文字>` / `say <文字>` | 本地测试屏幕，不走网络（`say` 会进聊天记录） |
| `help` | 列出所有命令 |

### 3.2 HTTP 接口（局域网，端口 80）

假设板子 IP 是 `192.168.1.23`：

```
curl http://192.168.1.23/ping                                            # -> ok
curl -X POST --data-binary "(´・ω・`)♡" http://192.168.1.23/face        # 最多 20 字符；空串恢复默认脸
curl -X POST --data-binary "你好呀，我是小身体。" http://192.168.1.23/say   # 最多 60 字符；空串清空气泡
curl -X POST -H "Content-Type: audio/wav" --data-binary @hello.wav http://192.168.1.23/play
        # PCM 16bit，单声道（双声道取平均），16000 或 24000 Hz（8k~48k 也接受），最大 3MB；新的 /play 打断正在放的
curl -X POST --data-binary "30"    http://192.168.1.23/volume            # 0-100，0 静音，100 最大
curl -X POST --data-binary "90"    http://192.168.1.23/rotate            # 0 / 90 / 180 / 270，存 NVS
curl -X POST --data-binary "40"    http://192.168.1.23/brightness        # 5-100，存 NVS（低于 5 按 5）
curl -X POST --data-binary "light" http://192.168.1.23/theme             # dark / light，存 NVS
curl -X POST --data-binary "我想你"  http://192.168.1.23/heard             # 她说的话（语音转文字结果）进聊天右侧，不闪
curl -X POST -H "Content-Type: application/json" --data-binary @buttons.json http://192.168.1.23/buttons
        # {"text":["想你了","抱抱","在干嘛","晚安"],"emoji":["(´ω`)","(≧▽≦)","♡","💧"],"shake":"想你了"}
        # 每排最多 8 个、每个最多 12 字，整体 <1KB；也可以只给一个数组（=文字行）。GET /buttons 看当前值
curl -X POST --data-binary "off"   http://192.168.1.23/anim              # 动画 on/off
curl -X POST --data-binary "on"    http://192.168.1.23/chime             # 提示音 on/off
curl -X POST --data-binary "on"    http://192.168.1.23/peek              # 让克看看 on/off
curl -o snap.jpg                   http://192.168.1.23/snap              # 远程拍一张（peek 关着返回 403）
```

接口只解析 body；body 末尾的换行会被去掉。接口没有任何鉴权，只在受信任的局域网内用。

### 3.3 电脑端脚本（`pc/`，只用 Python 标准库）

**收录音：**

```
python pc/ke_bridge.py
# ke_bridge listening on 0.0.0.0:8770, saving to .../pc/inbox
```

板子发来的东西都进 `pc/inbox/`，每个文件打印一行：

| 板子发的 | 路径 | 存成 |
|---|---|---|
| 按住说话的录音（WAV 16k 单声道） | `POST /hear` | `inbox/年月日-时分秒.wav` |
| 快捷按钮 / 摇晃发的文字 | `POST /msg` | `inbox/年月日-时分秒.txt` |
| 相册里点「寄给克」的照片 | `POST /photo` | `inbox/photos/年月日-时分秒.jpg` |

`/msg` `/photo` 的地址是板子从 `server` 设置推出来的（把最后一段 `/hear` 换掉），只要设一个地址。`pc/inbox/` 已加入 `.gitignore`。
板子上要先设置：`server http://<电脑IP>:8770/hear`。Windows 防火墙第一次会弹窗，允许专用网络访问即可。

**给板子发东西**（脚本对板子的请求**不走** `HTTP_PROXY` 等代理环境变量，不用再设 `NO_PROXY`）：

```
set KE_BOARD=192.168.1.23          # 或者每次加 --board 192.168.1.23
python pc/ke_send.py ping
python pc/ke_send.py face "(—ω—)"
python pc/ke_send.py say "今天天气不错"
python pc/ke_send.py play hello.wav     # 会先检查 WAV 头，不合规打印 warning 但照样发
python pc/ke_send.py volume 60
python pc/ke_send.py rotate 90
python pc/ke_send.py brightness 40
python pc/ke_send.py theme dark
python pc/ke_send.py heard "我想你"        # 语音转文字的结果送回板子，显示在她那一侧
python pc/ke_send.py buttons buttons.json # 按钮配置（文件或 JSON 字符串）
python pc/ke_send.py anim off
python pc/ke_send.py chime on
python pc/ke_send.py peek on
python pc/ke_send.py snap photo.jpg       # 远程拍一张存到本地（需要 peek on）
```

### 3.3.1 屏幕操作（第四期）

- **聊天界面**：顶部是脸（右上角 IP，peek 开着时多一个小眼睛），中间聊天记录可上下滑，底部第一排文字按钮 + 「相机」，第二排表情按钮。点脸或记录区 = 脸红；**按住 0.5 秒 = 说话**（同第二期）；按钮点一下就发。
- **相机**：点「相机」进入取景；点画面或「拍照」拍一张，画面下方提示「已保存 xxx.jpg」；「相册」看照片；「返回」回聊天并关相机（省电）。取景是横屏 480×320，竖屏模式下只显示中间一截。
- **相册**：左右滑动切换（拖动超过 40px），「删除」直接删当前这张，「寄给克」发到 bridge `/photo`（只有点了才发），「返回」回聊天。
- **睡觉**：屏幕朝下扣桌上 1.5 秒 → 睡脸 + z 飘 + 背光 5；拿起来或摸一下屏幕恢复。

### 3.4 按住说话的流程

1. 手指按住屏幕 0.5 秒 → 脸变 `(—o—)`，角落「在听」，开始录音。
2. 松手（或到 30 秒自动停）→ 脸恢复，角落「发送中」，WAV 发往 `server` 地址。
3. 发完角落回到 IP。失败时气泡显示原因（没设地址 / 没网 / 发送失败 / 服务器返回非 2xx）。
4. 发送期间（最长 15 秒超时）板子不处理新的录音和播放；这期间按住再松开会被忽略。
5. 播放中长按会打断播放并开始录音。

### 3.5 喇叭的来龙去脉

v2 无声 → v3 把 AXP2101 电源全开、ES8311 寄存器对齐官方驱动，仍无声 → v3.1 用 ES8311 内部回环（0x44=0x58 把 DAC 送回 ADC 右声道）证明 I2S 和 DAC 都通（`audio test` 回环 R=8000）→ 逐脚试 `tca <n> 1`，**TCA9554 P7 拉高后立即有声**。结论：功放使能脚 = TCA9554 P7，任何公开代码里都没有。

v3.2 起固件自己管这根脚：开机拉低（功放关），每次播放（`/play`、`audio test`）前拉高并等 30ms，播完拉低。空闲时功放关掉，没有底噪也省电。

音量：ES8311 的音量寄存器是 0.5dB 一格，原来的驱动按百分比线性写寄存器，10% 对应 -83dB 等于没声。v3.2 改成分贝映射：100 = 芯片最大（+32dB，就是你试 `volume 100` 时听到的那个），每降 10 格约小 6dB，0 = 静音。默认 18（上板试出 10 听不到）。

诊断命令都还在（`audio test / mic / regs / slot`、`pmic`、`tca`、`gpio`），只是开机不再碰 P7 以外的任何脚。

---

## 4. 硬件资料来源（以官方为准）

所有引脚、驱动芯片、初始化顺序都取自微雪官方代码仓库，没有凭记忆写：

- 官方仓库：**https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5**（本固件参考的提交：`283ec84`，2026-05-28）
  - `ESP-IDF/07_lvgl_wifi/components/esp_port/esp_3inch5_lcd_port.cpp` — SPI/LCD/背光/触摸引脚与初始化，各方向的触摸 swap/mirror 表
  - `ESP-IDF/07_lvgl_wifi/main/main.cpp` — I2C 引脚、TCA9554 复位 LCD 的顺序、**各方向的显示 swap/mirror 表**（第三期横屏用）
  - `ESP-IDF/07_lvgl_wifi/components/esp_lcd_st7796/` — ST7796 面板驱动（**原样拷贝到本仓库 `components/esp_lcd_st7796/`**，Apache-2.0）
  - `ESP-IDF/07_lvgl_wifi/components/esp_lcd_touch_ft6336/` — FT6336 寄存器定义
  - `ESP-IDF/07_lvgl_wifi/components/esp_port/esp_es8311_port.cpp` — I2S 引脚、ES8311 地址、无功放使能脚
  - `ESP-IDF/07_lvgl_wifi/components/esp_port/esp_camera_port.cpp` — **OV5640 引脚**、SCCB 走 I2C 口 0、20MHz XCLK、vflip（第四期）
  - `ESP-IDF/07_lvgl_wifi/components/esp32-camera/` — Espressif esp32-camera 2.0.15（**原样拷贝到 `components/esp32-camera/`**，去掉 examples/test）
  - `ESP-IDF/07_lvgl_wifi/components/esp_port/esp_sdcard_port.cpp` — **SD 卡 SDMMC 1 线引脚**（第四期）
  - `ESP-IDF/07_lvgl_wifi/components/sensorlib/src/REG/QMI8658Constants.h` + `esp_qmi8658_port.cpp` — QMI8658 地址、寄存器、±4g 配置（本固件自己按寄存器写了个 60 行的驱动，没有拷 SensorLib）
  - `ESP-IDF/07_lvgl_wifi/components/esp_port/esp_axp2101_port.cpp` — **AXP2101 电源轨设置**（第三期，`main/pmic.cpp` 逐行照抄电压和使能，去掉了看门狗和中断）
  - `ESP-IDF/07_lvgl_wifi/components/XPowersLib/` — AXP2101 驱动 XPowersLib（MIT），**拷贝到本仓库 `components/XPowersLib/`**（只留 src 和 Kconfig）
  - `Arduino/examples/01_audio_out`、`04_es8311_example` — ES8311 初始化顺序、音量 70、模拟麦克风
  - `Arduino/libraries/es8311/` — Espressif 的 es8311 驱动（Apache-2.0），**拷贝到本仓库 `components/es8311/`**，I2C 层改成 ESP-IDF `i2c_master`，另加两个裸寄存器读写函数供诊断
  - `ESP-IDF/*/sdkconfig.defaults` 与 `partitions.csv` — Flash 16MB / QIO / 80MHz，8MB 八线 PSRAM，分区表
- Espressif `esp_codec_dev` 的 ES8311 驱动（官方例子实际用的驱动，`esp-adf/components/esp_codec_dev/device/es8311/es8311.c`）— 第三期把它 `es8311_open/es8311_start` 里多写的寄存器（0x0B 0x0C 0x10 0x11 0x1B 0x44 0x17 0x15 0x45）补进了本固件的初始化
- ESP-IDF 自带例子 `examples/peripherals/i2s/i2s_codec/i2s_es8311`（ESP-IDF v5.4.4）— I2S 全双工 16bit 立体声、MCLK 走 MCLK 脚
- 第三方交叉验证：xiaozhi-esp32 的板级文件 `main/boards/waveshare/esp32-s3-touch-lcd-3.5/`— AXP2101 只开 ALDO1/BLDO1/BLDO2，TCA9554 pin0 拉低；横屏配置 swap_xy 与官方一致。（它也没写 P7，所以它在这块板上是否真能出声存疑）
- 微雪 wiki 页面（本次开发环境的网络策略封了 waveshare.com / waveshare.net / docs.waveshare.com，**没能直接打开**）：
  - https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5
  - https://www.waveshare.net/wiki/ESP32-S3-Touch-LCD-3.5

### 4.1 关键硬件信息

| 项目 | 值 | 出处 |
|------|----|------|
| SoC | ESP32-S3R8（8MB 八线 PSRAM） | 官方 sdkconfig：`SPIRAM_MODE_OCT`, `SPIRAM_SPEED_80M` |
| Flash | 16MB，QIO，80MHz | 官方 sdkconfig.defaults |
| 屏幕驱动 | **ST7796**，4 线 SPI，320×480 | `esp_lcd_st7796` 组件 |
| 屏幕引脚 | MOSI=GPIO1，SCLK=GPIO5，DC=GPIO3，CS/RST 不接 SoC，背光=GPIO6 | `esp_3inch5_lcd_port.cpp` |
| 屏幕复位 | 由 I/O 扩展芯片 **TCA9554**（I2C 地址 0x20）的 P1 控制 | `main.cpp: io_expander_init()` |
| 触摸芯片 | **FT6336**，I2C 地址 0x38，INT/RST 不接 SoC | `esp_lcd_touch_ft6336.h` |
| I2C | SDA=GPIO8，SCL=GPIO7 | `main.cpp` |
| 音频编解码 | **ES8311**，I2C 地址 0x18，I2S0：MCLK=GPIO12，BCLK=GPIO13，LRCK=GPIO15，DOUT(ESP→DAC)=GPIO16，DIN(ADC→ESP)=GPIO14 | `esp_es8311_port.cpp` |
| 功放使能 | **TCA9554 P7，高电平开**。官方例子 `pa_pin = GPIO_NUM_NC`、任何公开代码都没写；2026-09-29 真机逐脚试出来的 | 本仓库实测 |
| 电源管理 | **AXP2101**，I2C 0x34。官方：DC1~5 3.3/1.0/3.3/1.0/3.3V，ALDO1~4 3.3V，BLDO1 1.5V，BLDO2 2.8V，DLDO1/2 3.3V，CPUSLDO 1.0V，除 DC1 外全部使能 | `esp_axp2101_port.cpp` |
| 摄像头 | **OV5640**，DVP：XCLK=38 PCLK=41 VSYNC=17 HREF=18 D0~D7 = 45 47 48 46 42 40 39 21，SCCB 共用 I2C（SDA8/SCL7），无 PWDN/RESET | `esp_camera_port.cpp` |
| SD 卡 | SDMMC 1 线：CLK=11 CMD=10 D0=9 | `esp_sdcard_port.cpp` |
| 六轴 | **QMI8658**，I2C 0x6B（备选 0x6A），WHO_AM_I=0x05 | SensorLib 寄存器表 |
| 显示方向表 | 0: swap0 mx1 my0（已验证）；90: swap1 mx1 my1；180: swap0 mx0 my1；270: swap1 mx0 my0 | 官方 `main.cpp` |
| 触摸方向表 | 0: 无；90: mirror_y+swap；180: mirror_x+mirror_y；270: mirror_x+swap（esp_lcd_touch 先镜像后交换） | 官方 `esp_3inch5_touch_port_init` + esp_lcd_touch 源码 |

### 4.2 音频实现说明

- I2S 线上始终是 16bit 立体声。录音取左右声道平均存成单声道；播放时单声道样本复制到左右两路。
- 采样率按需切换：录音固定 16kHz；播放用 WAV 自己的采样率，切换时先停 I2S 两个通道，重配时钟，再给 ES8311 重配分频，MCLK = 256 × fs。
- 录音缓冲 30 秒 ≈ 960KB 放 PSRAM；`/play` 的 WAV 整段收进 PSRAM 再播。空闲时 I2S TX 自动清零（`auto_clear`）。

---

## 5. 目录结构

```
CMakeLists.txt / sdkconfig.defaults / partitions.csv   ESP-IDF 工程
main/
  main.c          启动流程：板级 → PMIC → UI → 亮度 → 音频 → 触摸 → Wi-Fi → 串口
  board.c/.h      板级：I2C、TCA9554、ST7796、旋转、背光、FT6336（含坐标换算、touchlog）
  pmic.cpp/.h     AXP2101 电源轨（照官方例子），dump / 单路开关
  audio.c/.h      ES8311 + I2S：录音、播放、音量、采样率切换、测试音、寄存器 dump
  light.c/.h      亮度（NVS）+ 夜间变暗 + NTP + 睡觉时的临时压暗
  imu.c/.h        QMI8658：摇晃、扣桌、自动转屏（可选）
  camera.c/.h     OV5640：JPEG 取景解码、拍照、相册解码
  storage.c/.h    SD 卡 / Flash 分区上的 DCIM
  cam_ui.c/.h     相机、相册屏幕，远程 /snap（peek）
  bridge.c/.h     出站队列：/msg /photo
  app_actions.h   触摸动作回调（main.c 实现）
  uploader.c/.h   esp_http_client POST WAV
  settings.c/.h   NVS 里的字符串设置
  ui.c/.h         UI 状态：聊天环、按钮配置、动画 tick、触摸状态机、旋转/主题（NVS）
  ui_render.c/.h  画面布局 + 命中测试（纯 C，横竖屏、深浅两套配色、聊天/相机/相册三个屏幕）
  gfx.c/.h        小型软件渲染器
  kb_font.h / fonts/   位图字体
  wifi_mgr.c/.h   NVS 凭据 + STA 连接 + 自动重连
  http_api.c/.h   /ping /face /say /play /volume /rotate /brightness /theme
  console_cmd.c/.h  串口命令
components/esp_lcd_st7796/   官方 ST7796 驱动（原样拷贝）
components/es8311/           官方 Arduino 库里的 Espressif es8311 驱动（I2C 层改为 i2c_master）
components/XPowersLib/       官方例子用的 AXP2101 驱动（MIT）
components/esp32-camera/     Espressif 摄像头驱动 2.0.15（官方仓库里的那份）
pc/ke_bridge.py              电脑端：收录音 /hear、文字 /msg、照片 /photo，存 pc/inbox/
pc/ke_send.py                电脑端：给板子发 face / say / heard / play / volume / rotate / brightness / theme / buttons / anim / chime / peek / snap / ping
tools/gen_fonts.py           字体生成脚本
tools/host_preview.c         电脑上渲染画面到 PPM（支持横竖屏、深浅色）
release/ke-body-vX.bin       合并固件（0x0）
release/ke-body-app-vX.bin   只含应用（0x10000，第四期起）
```

### 5.1 字体方案

没有用 LVGL，文字是自己渲染的 4bpp 抗锯齿位图字体，由 `tools/gen_fonts.py` 用系统字体生成：

| 字体 | 像素 | 内容 | 来源 |
|------|------|------|------|
| face64 / face44 / face30 | 64/44/30 | ASCII + 约 280 个颜文字常用符号（含 ˍ 皿 ﹏）+ 两个线条图标 💢 💧 | DejaVu Sans，缺字回退文泉驿正黑 / Unifont；图标由脚本用 Pillow 画 |
| text22 | 22 | ASCII + 中文标点 + **GB2312 全部 6763 个汉字** + 颜文字符号 | 文泉驿正黑（WenQuanYi Zen Hei） |
| small14 | 14 | ASCII + 状态用的几十个汉字 | 文泉驿正黑 |

位图总计约 1.9MB，全部放在 Flash，不占 RAM。字库里没有的字画一个空心方框。💢（U+1F4A2）和 💧（U+1F4A7）是彩色 emoji，没有单色字体，`gen_fonts.py` 里的 `render_icon()` 用线条画成和表情同高的位图，当作普通字形放进 face 字体和气泡字体，颜色跟随主题。要加别的 emoji 图标就在 `ICON_CODEPOINTS` 里加一项并写画法。颜文字先试 64px，放不下降到 44px、30px，再放不下折两行。横屏时脸的可用宽度是 456px，长颜文字更容易保持 64px。

字体版权：DejaVu（自由字体许可）、文泉驿正黑（GPLv2 + 字体嵌入例外）、GNU Unifont（GPLv2+ 字体例外 / OFL）。

重新生成字体：`python3 -m venv .venv && .venv/bin/pip install pillow fonttools && .venv/bin/python tools/gen_fonts.py`

### 5.2 电脑上预览画面

```
gcc -O1 -Imain -o preview tools/host_preview.c main/gfx.c main/ui_render.c main/fonts/font_*.c
./preview out.ppm "(´・ω・\`)♡" "你好呀，我是小身体。" "192.168.1.23" landscape dark
```

---

## 6. 未验证事项（没有实物，请上板确认）

### 已上板验证

v1：屏幕点亮、方向 0 正确不镜像、等待配网界面、气泡、触摸脸红、冷上电软复位一次不影响使用。
v2：开机 `ES8311 in Slave mode and I2S format`、`audio: ready`；家里 2.4G Wi-Fi 连上；HTTP `/face /say` 200 ok 且屏幕跟着变。**喇叭无声**（`/play` 返回 200 但听不到）。录音未测。

v3 / v3.1：AXP2101 应答、电源轨全开；ES8311 寄存器与官方驱动一致；`audio test` 回环 R=8000、`play done` 999ms（I2S、DAC 通）；TCA9554 P7 拉高后喇叭出声，`volume 100` 很大。

v3.3：横屏 270、黑底、亮度、夜间变暗、喇叭（TCA9554 P7 使能）、`audio test` 回环 R=8000、麦克风有信号。按住录音上传还没测。

### 第四期（v4，未上板）

1. **触摸坐标**：第四期开始真的用坐标（按钮、滑动、相册）。方向换算按官方表写，第三期没验证。上板先 `touchlog on`，按四个角看换算坐标对不对；错了的话按钮会点不准，告诉我具体对应关系我改 `board_touch_read()`。
2. **相机**：esp32-camera 在这块板上的初始化是按官方 esp_camera_port.cpp 写的，但改成了 JPEG 模式 + 尺寸切换（取景 HVGA、拍照 SXGA），没实测；看串口 `camera: sensor PID 0x5640`。取景帧率估计 5–10 fps。画面上下/左右不对用 `cam vflip` / `cam mirror`。
3. **SD 卡**：1 线 SDMMC，FAT32 卡；没插卡走内部 4MB 分区（第一次会格式化，几秒）。
4. **QMI8658**：地址 0x6B/0x6A 自动探测；摇晃阈值 1.9g 两次/0.9 秒；「屏幕朝下」假设芯片 Z 轴朝屏幕外（az ≈ +1g 时正放），反了用 `imu invert on`。自动转屏的轴向映射未验证，默认关。
5. **动画**：50ms tick 全帧重绘，眨眼/z 飘时约 10 fps；触摸和 HTTP 在别的任务里，理论上不受影响。
6. **静音提醒**：边框闪两下用的是主题里的粉色；提示音是固件生成的两音。
7. **聊天记录**只在内存，重启清空（按施工单）。
8. **只刷应用区**：v4 改了分区表，第一次要烧合并版（会清 NVS）；之后 `ke-body-app-vX.bin` 到 0x10000。
9. 第二期遗留：按住录音上传、24k 播放未测。

1. **功放开关时序**：播放前拉高 P7 等 30ms，播完拉低。开关瞬间可能有轻微「啪」声，有的话把 `main/audio.c` 里 30ms 加大，或改成常开（`board_amp_enable(true)` 放到 `board_init` 之后即可）。
2. **音量映射**是按「100 = 你听到的最大值」推的；默认 18 是上板调的。改默认值在 `main/audio.h` 的 `AUDIO_DEFAULT_VOLUME`。
3. **AXP2101 初始化**保留官方全开的做法（已上板，没发现副作用）。
4. **横屏方向 90 vs 270**：显示用官方表；90 和 270 都是横屏，只是上下颠倒，哪个 USB 口在你顺手的一边就用哪个。默认 90，不顺就 `rotate 270`。
5. **触摸坐标换算**：按官方 esp_lcd_touch 的「先镜像后交换」实现，没验证。`touchlog on` 后按屏幕四个角看打印的换算坐标是否和屏幕位置一致（横屏左上角应接近 0,0，右下角接近 479,319）。目前固件只用「有没有按」，坐标错不影响功能，但第四期要用。
6. **横屏排版**：脸中心 y=100（有气泡）/150（无），气泡最多 4 行，宽 456px。主机端预览过，真机没看。
7. **亮度下限 5**：`bright 5` 在真机上是否还能看清没验证；如果全黑，串口 `bright 80` 恢复。
8. **NTP**：`ntp.aliyun.com` 在家庭网络下一般几秒内对时；`night` 命令能看到「synced / NOT synced」。对时前夜间变暗不生效。时区写死东八区 `CST-8`。
9. **夜间变暗和手动亮度**：夜间生效时背光取 min(夜间亮度, 手动亮度)；`bright` 改的是手动亮度，夜间时段里改完仍然被压到夜间值，这是有意的。
10. **主题切换**是整帧重绘，切换瞬间会闪一下。
11. 第二期遗留：录音上传、24k 播放的采样率切换没测（`audio mic` 可先看麦克风有没有信号）；麦克风增益 30dB 是本固件选的（`audio gain` 可调）；按住时 FT6336 是否持续报告触点未验证。

---

## 7. 自己编译

- **ESP-IDF v5.4.4**（官方例子用的是 v5.4.0，同一个大版本）。
- 本工程**没有使用 IDF 组件管理器**（没有 `idf_component.yml`），所有依赖都在 ESP-IDF 自带组件和 `components/` 目录里，离线也能编。

```
git clone -b v5.4.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3 && . ./export.sh
cd /path/to/ke-body
idf.py set-target esp32s3
idf.py build
idf.py merge-bin -o /绝对路径/ke-body/release/ke-body-v3.bin     # 路径要写绝对路径，相对路径会落到 build/ 里
```

---

## 8. 没做的事

RTC、侧面按键、OTA、HTTPS、鉴权、语音识别 / 合成（电脑端另外接）。HTTP 接口没有任何认证，只应在受信任的局域网内使用。
