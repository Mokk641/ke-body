# ke-body — 小身体固件

目标板：微雪 **Waveshare ESP32-S3-Touch-LCD-3.5-C**（带外壳 + 背面 OV5640 摄像头）。
「-C」是在 **ESP32-S3-Touch-LCD-3.5** 同一块主板上加外壳和摄像头模组的套装，主板电路、引脚与不带 C 的版本相同（微雪官方仓库只有一个 `ESP32-S3-Touch-LCD-3.5`，Arduino/ESP-IDF 示例通用，其中 `03_camera_web_server` / `06_lvgl_camera` 就是给 -C 的摄像头用的）。

| 版本 | 固件 | 内容 | 真机状态 |
|------|------|------|----------|
| v1 | `release/ke-body-v1.bin` | 第一期：脸、配网、HTTP `/face /say /ping`、触摸脸红 | **已上板验证**（屏幕、方向、触摸、等待配网界面正常；Wi-Fi/HTTP 未测） |
| v2 | `release/ke-body-v2.bin` | 第一期 + 第二期：按住说话录音上传、`/play` 喇叭播放、`/volume`、电脑端脚本 | **未在真机运行过**，见 §6 |

> 本固件在没有实物的环境里编写和编译。§6「未验证事项」列出了需要上板确认的点，请按顺序核对。

---

## 1. 功能

### 第一期（脸）

| # | 功能 | 说明 |
|---|------|------|
| 1 | 开机显示「脸」 | 浅色底，中间大号颜文字，默认 `(—_—)` |
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
| 10 | 音量 | 默认 70；`POST /volume` body 0–100；串口 `volume <n>` |
| 11 | 电脑端 | `pc/ke_bridge.py` 收录音存文件；`pc/ke_send.py` 给板子发 face/say/play/volume |

短按（<0.5 秒）仍然是脸红。语音转文字、文字转语音不在固件里，电脑那边另外接。

---

## 2. 烧录（Windows）

1. 用 USB 线接板子的 **USB 口**（ESP32-S3 内置 USB-Serial/JTAG，设备管理器里是一个 COM 口，下面假设是 `COM3`）。
2. 安装 esptool：`pip install esptool`
3. 一行烧录（合并好的单文件固件，从地址 0 写入）：

```
python -m esptool --chip esp32s3 --port COM3 write-flash 0x0 release/ke-body-v2.bin
```

（要回到第一期就烧 `release/ke-body-v1.bin`，两个文件都保留。NVS 里的 Wi-Fi 和服务器地址在两版之间通用，重新烧录不会丢。）

如果板子没自动进入下载模式：按住 **BOOT** 键，按一下 **RESET**，松开 BOOT，再执行上面的命令。
烧完后按一下 RESET（或重新插电）。冷上电后固件会主动软复位一次（见 §6），属正常现象。

如果 esptool 提示 flash 大小和固件头不符，可以加 `--flash-size 16MB`（微雪官方示例配置为 16MB Flash，QIO 80MHz）。

注意：上面是 esptool **v5** 的写法（本仓库用 esptool v5.4.0 生成固件）。如果你装的是 esptool v4，子命令是下划线：`write_flash`。想快一点可以加 `-b 921600`。

`release/*.bin` 内容 = bootloader（0x0）+ 分区表（0x8000）+ 应用（0x10000），中间用 0xFF 填充。v1 约 3.0MB，v2 约 3.2MB。

---

## 3. 配网与使用

### 3.1 串口命令

烧录完，用任意串口终端（PuTTY / Tera Term / `python -m serial.tools.miniterm COM3 115200`）打开同一个 COM 口，会看到 `ke-body>` 提示符：

| 命令 | 作用 |
|------|------|
| `wifi <ssid> <password>` | 保存 Wi-Fi 凭据到 NVS 并重启。含空格用双引号：`wifi "My Home" "pass word"`；开放网络密码留空 |
| `wifi` / `wifi clear` | 显示当前 SSID、状态、IP / 清除凭据并重启 |
| `server http://192.168.1.5:8770/hear` | 录音上传地址（电脑上 `ke_bridge.py` 的地址），立即生效，不用重启 |
| `server` / `server clear` | 显示 / 清除 |
| `volume <0-100>` / `volume` | 喇叭音量（只在内存里，重启回到默认 70） |
| `face <颜文字>` / `say <文字>` | 本地测试屏幕，不走网络 |
| `help` | 列出所有命令 |

保存 Wi-Fi 后板子自动重启，连上路由器后右上角显示 IP。没有凭据时右上角显示「等待配网」，气泡里提示串口命令。断线会每 3 秒自动重连。

### 3.2 HTTP 接口（局域网，端口 80）

假设板子 IP 是 `192.168.1.23`：

```
curl http://192.168.1.23/ping
# -> ok

curl -X POST --data-binary "(´・ω・`)♡" http://192.168.1.23/face
# 屏幕中间换成这行颜文字（最多 20 个字符；超出截断；空串恢复默认脸）

curl -X POST --data-binary "你好呀，我是小身体。" http://192.168.1.23/say
# 显示在脸下面的气泡里（最多 60 个字符，自动换行，最多 6 行；空串清空气泡）

curl -X POST -H "Content-Type: audio/wav" --data-binary @hello.wav http://192.168.1.23/play
# 喇叭播放。要求 PCM 16bit，单声道（双声道也收，取左右平均），16000 或 24000 Hz
#（8k/11.025k/12k/22.05k/32k/44.1k/48k 也接受）。最大 3MB。新的 /play 会打断正在放的

curl -X POST --data-binary "60" http://192.168.1.23/volume
# 音量 0-100
```

接口只解析 body，忽略 `/face` `/say` 的 Content-Type；body 末尾的换行会被去掉。接口没有任何鉴权，只在受信任的局域网内用。

Windows PowerShell 里用 `Invoke-WebRequest` 时注意把 body 按 UTF-8 发送，或者直接用下面的 `ke_send.py`。

### 3.3 电脑端脚本（`pc/`，只用 Python 标准库）

**收录音：**

```
python pc/ke_bridge.py
# ke_bridge listening on 0.0.0.0:8770, saving to .../pc/inbox
```

板子每次按住说话松手后，会把 WAV `POST` 到 `/hear`，脚本存成 `pc/inbox/年月日-时分秒.wav`（16kHz 单声道 16bit）并在控制台打印一行文件名。`pc/inbox/` 已加入 `.gitignore`。
板子上要先设置：`server http://<电脑IP>:8770/hear`。Windows 防火墙第一次会弹窗，允许专用网络访问即可。

**给板子发东西：**

```
set KE_BOARD=192.168.1.23          # 或者每次加 --board 192.168.1.23
python pc/ke_send.py ping
python pc/ke_send.py face "(—ω—)"
python pc/ke_send.py say "今天天气不错"
python pc/ke_send.py play hello.wav     # 会先检查 WAV 头，不合规打印 warning 但照样发
python pc/ke_send.py volume 60
```

### 3.4 按住说话的流程

1. 手指按住屏幕 0.5 秒 → 脸变 `(—o—)`，角落「在听」，开始录音。
2. 松手（或到 30 秒自动停）→ 脸恢复，角落「发送中」，WAV 发往 `server` 地址。
3. 发完角落回到 IP。失败时气泡显示原因（没设地址 / 没网 / 发送失败 / 服务器返回非 2xx）。
4. 发送期间（最长 15 秒超时）板子不处理新的录音和播放；这期间按住再松开会被忽略。
5. 播放中长按会打断播放并开始录音。

---

## 4. 硬件资料来源（以官方为准）

所有引脚、驱动芯片、初始化顺序都取自微雪官方代码仓库，没有凭记忆写：

- 官方仓库：**https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5**（本固件参考的提交：`283ec84`，2026-05-28）
  - `ESP-IDF/07_lvgl_wifi/components/esp_port/esp_3inch5_lcd_port.cpp` — SPI/LCD/背光/触摸引脚与初始化
  - `ESP-IDF/07_lvgl_wifi/main/main.cpp` — I2C 引脚、TCA9554 复位 LCD 的顺序、LVGL 显示方向设置
  - `ESP-IDF/07_lvgl_wifi/components/esp_lcd_st7796/` — ST7796 面板驱动（**原样拷贝到本仓库 `components/esp_lcd_st7796/`**，Apache-2.0）
  - `ESP-IDF/07_lvgl_wifi/components/esp_lcd_touch_ft6336/` — FT6336 寄存器定义（本固件自己按同样的寄存器读取，不依赖 esp_lcd_touch 组件）
  - `ESP-IDF/07_lvgl_wifi/components/esp_port/esp_es8311_port.cpp` — **I2S 引脚、ES8311 地址、无功放使能脚**（第二期）
  - `Arduino/examples/01_audio_out`、`04_es8311_example` — ES8311 初始化顺序、音量 70、模拟麦克风（第二期）
  - `Arduino/libraries/es8311/` — Espressif 的 es8311 驱动（Apache-2.0），**拷贝到本仓库 `components/es8311/`，只把 I2C 读写层从 Arduino `Wire` 改成 ESP-IDF `i2c_master`**，其余原样
  - `ESP-IDF/*/sdkconfig.defaults` 与 `partitions.csv` — Flash 16MB / QIO / 80MHz，8MB 八线 PSRAM，分区表
  - `Arduino/examples/08_gfx_helloworld` — 同样的引脚和 TCA9554 复位序列（交叉验证）
- ESP-IDF 自带例子 `examples/peripherals/i2s/i2s_codec/i2s_es8311`（ESP-IDF v5.4.4）— 同一个 es8311 驱动的标准用法（16kHz、16bit、立体声槽位、MCLK 走 MCLK 脚）
- 微雪 wiki 页面（本次开发环境的网络策略封了 waveshare.com / waveshare.net / docs.waveshare.com，**没能直接打开**，以上信息全部来自官方 GitHub 仓库）：
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
| 功放 | 官方例子 `pa_pin = GPIO_NUM_NC`，没有功放使能脚，喇叭默认常通 | `esp_es8311_port.cpp` |
| 麦克风 | ES8311 模拟麦克风输入（`es8311_microphone_config(false)`），本固件 PGA 增益 30dB | Arduino 例子；增益值是本固件选的 |
| 电源管理 | AXP2101（I2C 0x34） | 官方 ESP-IDF 例子有初始化；本固件**未初始化**，第一期实测屏幕不需要 |
| 其它 | QMI8658 IMU、PCF85063 RTC、TF 卡、OV5640 | 不使用 |
| 屏幕方向 | 竖屏 320×480，`mirror_x = 1`，`invert_color = 1`，BGR | 官方 3 个 LVGL 例子；**第一期真机验证正确** |

### 4.2 音频实现说明

- I2S 线上始终是 16bit 立体声（和官方 `esp_es8311_port.cpp`、ESP-IDF 例子一致）。录音取左右声道平均存成单声道；播放时单声道样本复制到左右两路。
- 采样率按需切换：录音固定 16kHz；播放用 WAV 自己的采样率（16k/24k…），切换时先停 I2S 两个通道，重配时钟，再给 ES8311 重配分频（`es8311_sample_frequency_config`），MCLK = 256 × fs。
- 录音缓冲 30 秒 × 16kHz × 2 字节 ≈ 960KB，放 PSRAM；`/play` 的 WAV 也整段收进 PSRAM 再播。
- ES8311 在录音、播放之间不关闭，空闲时 I2S TX 自动清零（`auto_clear`），理论上不会有底噪循环。

---

## 5. 目录结构

```
CMakeLists.txt / sdkconfig.defaults / partitions.csv   ESP-IDF 工程
main/
  main.c          启动流程、Wi-Fi 状态 → 屏幕、触摸状态机（短按/长按）、录音上传
  board.c/.h      板级：I2C、TCA9554 复位、ST7796、背光、FT6336
  audio.c/.h      ES8311 + I2S：录音（WAV）、播放、音量、采样率切换
  uploader.c/.h   esp_http_client POST WAV
  settings.c/.h   NVS 里的字符串设置（server_url）
  ui.c/.h         UI 状态（互斥锁）+ 渲染任务 + 脸/角落的临时覆盖（脸红/在听/播放）
  ui_render.c/.h  画面布局（纯 C，可在电脑上编译预览）
  gfx.c/.h        小型软件渲染器：RGB565、圆角矩形、4bpp 抗锯齿文字、自动换行
  kb_font.h       位图字体格式
  fonts/          生成的字体（见下）
  wifi_mgr.c/.h   NVS 凭据 + STA 连接 + 自动重连
  http_api.c/.h   /ping /face /say /play /volume
  console_cmd.c/.h  串口命令 wifi / server / volume / face / say
components/esp_lcd_st7796/   官方 ST7796 驱动（原样拷贝）
components/es8311/           官方 Arduino 库里的 Espressif es8311 驱动（I2C 层改为 i2c_master）
pc/ke_bridge.py              电脑端：收录音，存 pc/inbox/
pc/ke_send.py                电脑端：给板子发 face / say / play / volume / ping
tools/gen_fonts.py           字体生成脚本
tools/host_preview.c         电脑上渲染画面到 PPM，检查布局和字体
release/ke-body-v1.bin       第一期合并固件
release/ke-body-v2.bin       第二期合并固件
```

### 5.1 字体方案

没有用 LVGL，文字是自己渲染的 4bpp 抗锯齿位图字体，由 `tools/gen_fonts.py` 用系统字体生成：

| 字体 | 像素 | 内容 | 来源 |
|------|------|------|------|
| face64 / face44 / face30 | 64/44/30 | ASCII + 约 280 个颜文字常用符号（— ︵ ♡ ∀ ω ´ ・ ≧ ≦ ° ╥ ▽ 〇 ヽ ﾉ 等） | DejaVu Sans，缺字回退文泉驿正黑 / Unifont |
| text22 | 22 | ASCII + 中文标点 + **GB2312 全部 6763 个汉字**（含常用 3500 字，也含「嗯」「呗」等聊天常用二级字）+ 颜文字符号 | 文泉驿正黑（WenQuanYi Zen Hei） |
| small14 | 14 | ASCII + 状态用的几十个汉字 | 文泉驿正黑 |

位图总计约 1.9MB，全部放在 Flash（应用分区 6MB），不占 RAM。`/say` 遇到字库里没有的字会画一个空心方框。
颜文字大字号会自动选择：先试 64px，放不下降到 44px、30px，30px 仍放不下则折成两行。

字体版权：DejaVu（自由字体许可）、文泉驿正黑（GPLv2 + 字体嵌入例外）、GNU Unifont（GPLv2+ 字体例外 / OFL）。

重新生成字体（需要 Pillow 和 fontTools）：

```
python3 -m venv .venv && .venv/bin/pip install pillow fonttools
.venv/bin/python tools/gen_fonts.py
```

### 5.2 电脑上预览画面

```
gcc -O1 -Imain -o preview tools/host_preview.c main/gfx.c main/ui_render.c main/fonts/font_*.c
./preview out.ppm "(´・ω・\`)♡" "你好呀，我是小身体。" "192.168.1.23"
```

---

## 6. 未验证事项（没有实物，请上板确认）

### 第一期（v1，已上板）

已验证：屏幕点亮、方向正确不镜像、等待配网界面、气泡、触摸脸红、不初始化 AXP2101 屏幕也能亮、冷上电软复位一次不影响使用。
**仍未验证**：Wi-Fi 连接、IP 显示、HTTP `/ping /face /say`（还没配网）。

### 第二期（v2，未上板）

1. **整个音频链路没有在真机跑过**：ES8311 能否被 I2C 找到（地址 0x18）、录音是否有声、喇叭是否出声。
2. **功放**：官方例子没有功放使能脚（`pa_pin = NC`），本固件假设喇叭功放常通。如果播放无声，首先怀疑功放需要某个 GPIO / TCA9554 引脚 / AXP2101 电源轨使能；官方 ESP-IDF 例子在 ES8311 之前会先跑 `esp_axp2101_port_init()` 把 DC2~5、ALDO1~4、BLDO1~2、DLDO1~2 全打开，本固件没做。
3. **麦克风增益** 30dB 是本固件选的（官方 `esp_codec_dev` 测试代码用 40dB）。太小改 `main/audio.c` 的 `MIC_GAIN`（`ES8311_MIC_GAIN_36DB` / `42DB`），削波则调小。
4. **左右声道**：录音取 L/R 平均。如果 ES8311 只在一路上输出 ADC 数据，录音幅度会比预期低 6dB，但仍然有声；播放把单声道复制到两路，不受影响。
5. **采样率切换**（16k ↔ 24k）是在 I2S 全双工下重配两个通道的时钟再重配 ES8311 分频，逻辑按 ESP-IDF 驱动的要求写（先 disable 再 reconfig），没有实测。如果 24k 播放异常而 16k 正常，可以先在电脑上把 WAV 重采样到 16k 绕过。
6. **触摸长按判定** 0.5 秒基于 30ms 轮询；FT6336 在手指持续按住时是否一直报告触点数 >0，未验证（第一期只验证了「按下瞬间」）。如果按住时触点时有时无，会表现为录音提前结束。
7. **上传**：`esp_http_client` POST 到局域网电脑，15 秒超时，只支持 `http://`。30 秒录音约 960KB，在 2.4G Wi-Fi 上大约 1–3 秒。
8. **`/play` 大文件**：最多 3MB 整段收进 PSRAM，HTTP 接收超时 10 秒/次。
9. **音量映射**：`es8311_voice_volume_set(70)` 直接写 DAC 音量寄存器，实际响度取决于功放，可能需要调默认值 `AUDIO_DEFAULT_VOLUME`。
10. 第一期遗留：Wi-Fi / HTTP 尚未在真机测试；串口控制台的行编辑在不同终端里的表现未验证。

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
idf.py merge-bin -o /绝对路径/ke-body/release/ke-body-v2.bin     # 生成合并固件（路径要写绝对路径，相对路径会落到 build/ 里）
```

编译完 `build/ke-body.bin` 是应用本体，`release/ke-body-v2.bin` 是把 bootloader（0x0）、分区表（0x8000）、应用（0x10000）合并后的单文件，直接从 0x0 烧。

---

## 8. 没做的事

摄像头、SD 卡、IMU、RTC、侧面按键、电源管理、OTA、HTTPS、鉴权、语音识别 / 合成（电脑端另外接）。HTTP 接口没有任何认证，只应在受信任的局域网内使用。
