# ke-body — 小身体固件 · 第一期（只做「脸」）

目标板：微雪 **Waveshare ESP32-S3-Touch-LCD-3.5-C**（带外壳 + 背面 OV5640 摄像头）。
「-C」是在 **ESP32-S3-Touch-LCD-3.5** 同一块主板上加外壳和摄像头模组的套装，主板电路、引脚与不带 C 的版本相同（微雪官方仓库只有一个 `ESP32-S3-Touch-LCD-3.5`，Arduino/ESP-IDF 示例通用，其中 `03_camera_web_server` / `06_lvgl_camera` 就是给 -C 的摄像头用的）。

> **重要：本固件在没有实物的环境里编写和编译，没有在真机上运行过。** 文末「未验证事项」列出了所有需要上板确认的点，请按顺序核对。

---

## 1. 功能（第一期）

| # | 功能 | 说明 |
|---|------|------|
| 1 | 开机显示「脸」 | 浅色底，中间大号颜文字，默认 `(—_—)` |
| 2 | 连 Wi-Fi（2.4G） | 账号密码只存 NVS，**不在仓库里**。没配置时屏幕显示「等待配网」 |
| 3 | 串口配网 | 串口命令 `wifi <ssid> <password>` 写入 NVS 后自动重启 |
| 4 | 显示 IP | 连上后右上角小字显示 IP |
| 5 | HTTP 接口 | `GET /ping` → `ok`；`POST /face`；`POST /say` |
| 6 | 触摸 | 触摸屏幕任意位置，脸变成 `(—//—)`，2 秒后恢复 |

---

## 2. 烧录（Windows）

1. 用 USB 线接板子的 **USB 口**（ESP32-S3 内置 USB-Serial/JTAG，设备管理器里是一个 COM 口，下面假设是 `COM3`）。
2. 安装 esptool：`pip install esptool`
3. 一行烧录（合并好的单文件固件，从地址 0 写入）：

```
python -m esptool --chip esp32s3 --port COM3 write-flash 0x0 release/ke-body-v1.bin
```

如果板子没自动进入下载模式：按住 **BOOT** 键，按一下 **RESET**，松开 BOOT，再执行上面的命令。
烧完后按一下 RESET（或重新插电）。冷上电后固件会主动软复位一次（见 §5），属正常现象。

如果 esptool 提示 flash 大小和固件头不符，可以加 `--flash-size 16MB`（微雪官方示例配置为 16MB Flash，QIO 80MHz）。

注意：上面是 esptool **v5** 的写法（本仓库用 esptool v5.4.0 生成固件）。如果你装的是 esptool v4，子命令是下划线：`write_flash`。想快一点可以加 `-b 921600`。

`release/ke-body-v1.bin` 大小约 3.0MB，内容 = bootloader（0x0）+ 分区表（0x8000）+ 应用（0x10000），中间用 0xFF 填充。

---

## 3. 配网与使用

### 3.1 串口配网

烧录完，用任意串口终端（PuTTY / Tera Term / `python -m serial.tools.miniterm COM3 115200`）打开同一个 COM 口，会看到 `ke-body>` 提示符：

```
wifi MyHomeWiFi MyPassword
```

- SSID 或密码含空格用双引号：`wifi "My Home" "pass word"`
- `wifi` 不带参数：显示当前保存的 SSID、连接状态和 IP
- `wifi clear`：清除凭据并重启
- `face <颜文字>` / `say <文字>`：本地测试屏幕，不走网络
- `help`：列出所有命令

保存后板子自动重启，连上路由器后右上角显示 IP。没有凭据时右上角显示「等待配网」，气泡里提示串口命令。断线会每 3 秒自动重连。

### 3.2 HTTP 接口（局域网）

假设 IP 是 `192.168.1.23`：

```
curl http://192.168.1.23/ping
# -> ok

curl -X POST --data-binary "(´・ω・`)♡" http://192.168.1.23/face
# 屏幕中间换成这行颜文字（最多 20 个字符；超出截断；空串恢复默认脸）

curl -X POST --data-binary "你好呀，我是小身体。" http://192.168.1.23/say
# 显示在脸下面的气泡里（最多 60 个字符，自动换行，最多 6 行；空串清空气泡）
```

Windows PowerShell 里用 `Invoke-WebRequest`/`curl.exe` 时注意把 body 按 UTF-8 发送，例如：

```powershell
$b = [System.Text.Encoding]::UTF8.GetBytes("你好")
Invoke-WebRequest -Method POST -Uri http://192.168.1.23/say -Body $b
```

接口只解析 body，忽略 Content-Type；body 末尾的换行会被去掉。

---

## 4. 硬件资料来源（以官方为准）

所有引脚、驱动芯片、初始化顺序都取自微雪官方代码仓库，没有凭记忆写：

- 官方仓库：**https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5**（本固件参考的提交：`283ec84`，2026-05-28）
  - `ESP-IDF/07_lvgl_wifi/components/esp_port/esp_3inch5_lcd_port.cpp` — SPI/LCD/背光/触摸引脚与初始化
  - `ESP-IDF/07_lvgl_wifi/main/main.cpp` — I2C 引脚、TCA9554 复位 LCD 的顺序、LVGL 显示方向设置
  - `ESP-IDF/07_lvgl_wifi/components/esp_lcd_st7796/` — ST7796 面板驱动（**原样拷贝到本仓库 `components/esp_lcd_st7796/`**，Apache-2.0）
  - `ESP-IDF/07_lvgl_wifi/components/esp_lcd_touch_ft6336/` — FT6336 寄存器定义（本固件自己按同样的寄存器读取，不依赖 esp_lcd_touch 组件）
  - `ESP-IDF/*/sdkconfig.defaults` 与 `partitions.csv` — Flash 16MB / QIO / 80MHz，8MB 八线 PSRAM，分区表
  - `Arduino/examples/08_gfx_helloworld` — 同样的引脚和 TCA9554 复位序列（交叉验证）
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
| 电源管理 | AXP2101（I2C 0x34） | 官方例子有初始化；本固件**未初始化**（见 §5） |
| 其它 | ES8311 音频、QMI8658 IMU、PCF85063 RTC、TF 卡、OV5640 | 第一期不使用 |
| 屏幕方向 | 竖屏 320×480，`mirror_x = 1`，`invert_color = 1`，BGR | 官方 3 个 LVGL 例子的 `display_cfg.rotation` 均为 mirror_x=1 |

---

## 5. 未验证事项（没有实物，请上板确认）

1. **整个固件都没有在真机跑过。** 编译通过、主机端渲染预览正常（`tools/host_preview.c`），仅此而已。
2. **屏幕方向**：`main/board.c` 里 `BOARD_LCD_MIRROR_X 1` 取自官方 ESP-IDF 例子；官方 Arduino 例子（Arduino_GFX rotation 0）却不镜像，两者矛盾。第三方 xiaozhi-esp32 的该板配置（横屏、仅 swap_xy）换算后与官方 ESP-IDF 例子一致，所以采用 mirror_x=1。**如果上板发现文字左右镜像**，把 `BOARD_LCD_MIRROR_X` 改成 0 重新编译即可。
3. **冷上电软复位**：官方 `esp_3inch5_lcd_port.cpp` 在上电复位（POWERON）时创建完 SPI 面板 IO 后会 `esp_restart()` 一次（`soft_reset_once`），原因官方没说明，本固件照做。表现为上电后串口日志出现一次重启。如果确认不需要，可删除 `board.c` 里的 `soft_reset_once()`。
4. **AXP2101 电源管理没有初始化**。官方 Arduino hello world 不初始化 AXP2101 屏幕也能亮，所以第一期省掉。如果屏幕不亮/背光不亮，第一个怀疑对象就是这里（官方 ESP-IDF 例子的 `esp_axp2101_port.cpp` 把 DC2~DC5、ALDO1~4、BLDO1~2、DLDO1~2 全部打开）。
5. **触摸**：FT6336 按官方寄存器轮询（0x02 触点数，0x03~0x06 坐标），30ms 一次，只用「有没有按下」；坐标方向没验证（第一期不需要）。
6. **SPI 刷屏**：帧缓冲在 PSRAM，分 24 行一块经内部 DMA 缓冲送到面板，全屏刷新理论约 40ms。没验证 80MHz SPI 在实物上是否稳定，不稳定可把 `LCD_PCLK_HZ` 降到 40MHz。
7. **串口控制台**走 USB-Serial/JTAG（`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`），也就是烧录用的那个 COM 口。行编辑/回显在不同终端里的表现没验证；命令本身按 esp_console 标准实现。
8. **Wi-Fi**：标准 STA 流程，WPA/WPA2 密码；开放网络把密码留空。企业 Wi-Fi、Portal 认证不支持。
9. **Flash 大小**：按官方配置写 16MB；esptool 烧录时会自动读取，如果实际不同请以 esptool 输出为准，并改 `sdkconfig.defaults` 重新编译。

---

## 6. 目录结构

```
CMakeLists.txt / sdkconfig.defaults / partitions.csv   ESP-IDF 工程
main/
  main.c          启动流程、Wi-Fi 状态 → 屏幕、触摸轮询
  board.c/.h      板级：I2C、TCA9554 复位、ST7796、背光、FT6336
  ui.c/.h         UI 状态（互斥锁）+ 渲染任务 + 触摸红脸定时器
  ui_render.c/.h  画面布局（纯 C，可在电脑上编译预览）
  gfx.c/.h        小型软件渲染器：RGB565、圆角矩形、4bpp 抗锯齿文字、自动换行
  kb_font.h       位图字体格式
  fonts/          生成的字体（见下）
  wifi_mgr.c/.h   NVS 凭据 + STA 连接 + 自动重连
  http_api.c/.h   /ping /face /say
  console_cmd.c/.h  串口命令 wifi / face / say
components/esp_lcd_st7796/   官方 ST7796 驱动（原样拷贝）
tools/gen_fonts.py           字体生成脚本
tools/host_preview.c         电脑上渲染画面到 PPM，检查布局和字体
release/ke-body-v1.bin       合并好的单文件固件（0x0 烧录）
```

### 6.1 字体方案

没有用 LVGL，文字是自己渲染的 4bpp 抗锯齿位图字体，由 `tools/gen_fonts.py` 用系统字体生成：

| 字体 | 像素 | 内容 | 来源 |
|------|------|------|------|
| face64 / face44 / face30 | 64/44/30 | ASCII + 约 190 个颜文字常用符号（— ︵ ♡ ∀ ω ´ ・ ≧ ≦ ° ╥ ω 等） | DejaVu Sans，缺字回退文泉驿正黑 / Unifont |
| text22 | 22 | ASCII + 中文标点 + **GB2312 全部 6763 个汉字**（含常用 3500 字，也含「嗯」「呗」等聊天常用二级字）+ 颜文字符号 | 文泉驿正黑（WenQuanYi Zen Hei） |
| small14 | 14 | ASCII + 状态用的几个汉字 | 文泉驿正黑 |

位图总计约 1.9MB，全部放在 Flash（应用分区 6MB），不占 RAM。`/say` 遇到字库里没有的字会画一个空心方框。
颜文字大字号会自动选择：先试 64px，放不下降到 44px、30px，30px 仍放不下则折成两行。

字体版权：DejaVu（自由字体许可）、文泉驿正黑（GPLv2 + 字体嵌入例外）、GNU Unifont（GPLv2+ 字体例外 / OFL）。

重新生成字体（需要 Pillow 和 fontTools）：

```
python3 -m venv .venv && .venv/bin/pip install pillow fonttools
.venv/bin/python tools/gen_fonts.py
```

### 6.2 电脑上预览画面

```
gcc -O1 -Imain -o preview tools/host_preview.c main/gfx.c main/ui_render.c main/fonts/font_*.c
./preview out.ppm "(´・ω・\`)♡" "你好呀，我是小身体。" "192.168.1.23"
```

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
idf.py merge-bin -o release/ke-body-v1.bin     # 生成合并固件
```

编译完 `build/ke-body.bin` 是应用本体，`release/ke-body-v1.bin` 是把 bootloader（0x0）、分区表（0x8000）、应用（0x10000）合并后的单文件，直接从 0x0 烧。

---

## 8. 第一期没做的事

摄像头、麦克风、喇叭、SD 卡、IMU、RTC、侧面按键、电源管理、OTA、HTTPS、鉴权。HTTP 接口没有任何认证，只应在受信任的局域网内使用。
