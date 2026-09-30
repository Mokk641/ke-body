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
| v4 | `release/ke-body-v4.bin`（合并）<br>`release/ke-body-app-v4.bin`（只含应用） | + 第四期：聊天记录 + 快捷按钮、动画、QMI8658 摇晃/扣桌、静音提醒、相机/相册/寄照片/远程 /snap、bridge 收 /msg /photo | 已上板：显示、横屏、`msg` 发到 bridge 正常；**屏幕按钮点了没反应**（v4.1 修） |
| v4.1 | `release/ke-body-v4.1.bin`（合并）<br>`release/ke-body-app-v4.1.bin`（只含应用） | 修：按钮/相机/相册点击无反应；聊天上下滑方向反了；串口能输入中文 | 主机测试通过，**未上板** |
| v5 | `release/ke-body-v5.bin`（合并）<br>`release/ke-body-app-v5.bin`（**只含应用，推荐**，烧 0x10000，保留 NVS） | 第五期界面美化：脸页 + 聊天页上下滑切换、圆角气泡、+ 面板（快捷语 + 表情分页）、动画分项开关 | 编译通过、主机测试通过，**未上板** |

| v6 | `release/ke-body-v6.bin`（合并）<br>`release/ke-body-app-v6.bin`（**只含应用，推荐**） | 第六期：聊天页改成 iOS 暗色 iMessage 风格；相机转正/调色/防条纹/先预览再决定；寄给电脑更可靠（ARP 唤醒 + 重试 + 关省电）；发送中按钮置灰；SD 只探测一次；`color` / `colortest` 颜色校准工具 | 编译通过、主机测试通过，**未上板** |

| v6.1 | `release/ke-body-v6.1.bin`（合并）<br>`release/ke-body-app-v6.1.bin`（**只含应用，推荐**） | 修拍照崩溃（cam_task 栈溢出）；相机默认方向改成实测值；顶栏脸和「克」在最左；底栏透明；白色主题 + `theme auto`（默认，夜间自动变暗） | 编译通过、主机测试通过，**未上板** |

| v6.2 | `release/ke-body-v6.2.bin`（合并）<br>`release/ke-body-app-v6.2.bin`（**只含应用，推荐**） | 手写输入（「+」面板里的「手写」）；相机白平衡默认 office；`cam sweep` 一次拍出 6 组 XCLK/画质对比并打印丢帧表 | 编译通过、主机测试通过，**未上板** |

| v6.3 | `release/ke-body-v6.3.bin`（合并）<br>`release/ke-body-app-v6.3.bin`（**只含应用，推荐**） | 手写只刷新方格那一块屏幕（落笔即出线）；底部按钮触摸区加大 + `touchlog` 屏幕小圆点 + `touchrange`/`touchcal`；气泡换色：克黑（暗色下深灰）她粉；手写缩略图白底黑字粉边框；`imu invert` 默认开 | 编译通过、主机测试通过，**未上板** |

| v6.4 | `release/ke-body-v6.4.bin`（合并）<br>`release/ke-body-app-v6.4.bin`（**只含应用，推荐**） | 修上下边缘点不动（按钮上不再判拖动、手指滚动/首点偏一格也算点击、控制器把一次触摸拆成两次不再重复触发）；`touchlog` 每次松手打印为什么算/不算；手写气泡改成粉色气泡上直接画白字；底栏图标加实心圆底；顶栏小脸更大更清楚；粉色 `#F2708F` | 编译通过、主机测试通过，**未上板** |

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
| 16 | 黑底白字 | `theme dark|light`、`POST /theme`，存 NVS（v6.1 起默认改为 `auto`，见第六期补丁） |

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
| 24 | 小动画 | （第五期已调整，见下）睡脸时 z/Z 往上飘；摸脸「//」粉色渐显渐隐；摇晃时脸左右抖两下 |

语音转文字、文字转语音不在固件里，电脑那边另外接。

### 第五期（界面美化）

| # | 功能 | 说明 |
|---|------|------|
| 25 | 两个页面 | **脸页（默认）**：全黑底，脸大大居中，脸下面一行淡字 = 克最近一句，底部小字「⌃ 上滑聊天」。**聊天页**：脸页上滑（或点「⌃ 上滑聊天」）进入；下滑顶栏 / 点顶栏「⌄」返回（聊天内容很短时，在消息区下滑也能返回；内容很长时消息区下滑是翻历史）。两页之间是竖向滑动动画（约 0.25 秒） |
| 26 | 聊天页布局 | 顶栏：左边一个随表情变的小脸 +「克」，右边在线点（bridge 通 = 绿，不通 = 灰；每 15 秒探测一次 `GET <bridge>/ping`）。中间：消息列表，新消息自动滚到底；克的消息在左（v5 有发出那一刻表情的小头像，**v6 起去掉**）；她的在右。底栏：左「+」，右相机图标 |
| 27 | 「+」面板 | 点「+」从底栏上方滑出：上半是快捷语胶囊（默认 想你了 / 抱抱 / 在干嘛 / 晚安），下半是表情格子（左右滑翻页，页码小点）。再点「+」（变成 ⊗）或点上面的空白处收起。点快捷语/表情 = 发到 bridge `/msg` 并进聊天，面板保持展开 |
| 28 | 气泡 | 大圆角（16px）。（v6 起改成 iMessage 配色，见第六期。）所有颜色是宏，在 `main/ui_colors.h` |
| 29 | 表情格 | 格子更大，默认 10 个：`(—ω—) (—//—) (—▽—)♡ (—ε—) (—_—)♡ (—︵—) V(—ω—)V (—o—) (=ω=) (—∀-)`；一页里字号统一，放不下就整体缩小（最小 13px，再不行折两行），不截断。`POST /buttons` 仍可配置（最多 8 个快捷语、30 个表情） |
| 30 | 动画调整 | **默认关**：随机眨眼、粉色边框闪。**默认开**：脸红「//」、睡觉 z 飘、摇晃抖。每项一个开关，存 NVS（见 `anim` 命令、`POST /anim`） |
| 31 | 新消息提示 | 脸页：脸变 `(—o—)` 约 1 秒再变回去，下面那行字淡入；聊天页：新气泡从底部滑入，没有边框。睡觉时不变脸。`chime on` 的提示音照旧（默认关）。边框闪可用 `anim flash on` 重新打开 |


### 第六期（iMessage 风格 + 补修）

| # | 功能 | 说明 |
|---|------|------|
| 32 | 聊天页改成 iOS 暗色 iMessage | 纯黑背景；**克在左**：系统灰 `#262628`、白字；**她在右**：iOS 蓝 `#0A84FF`、白字；两边同一字号（22px）同一字重。圆角 18px，左右各留 12px，最宽约屏宽 70%；同一个人连发间距 3px，换人 10px。**没有头像、没有小尾巴** |
| 33 | 顶栏 | 去掉 IP；左上一个细的「⌄」；正中一个小圆（36px）里是克当前的表情（去掉括号、缩小字号，长颜文字会更小），下面一行小字「克」，**在线时**名字旁一个很小的绿点，不在线不显示；背景 `#1C1C1E` + 极淡分隔线。peek 开着时右边仍有小眼睛 |
| 34 | 底栏 | 一根圆角胶囊条（`#1C1C1E`）：左「+」右相机，图标是细线、浅灰。「+」面板：背景 `#1C1C1E`，快捷语小胶囊和表情格都是 `#2C2C2E` 底白字 |
| 35 | 脸页 | 纯黑，大脸纯白，最新一句浅灰小字，「⌃ 上滑聊天」更暗更小；其余不变 |
| 36 | 相机转正 | 传感器在壳子里是横躺着装的（官方例子也是向传感器要 320×480 竖图），所以取景和拍下的 JPEG 都按当前 `rotate` 转正：`rotate 270` → 逆时针 90°。拍照走 864×1536 竖图 → 解码 → 旋转 → 重新编码成 1536×864 JPEG（有一次二次压缩）。转的方向靠推导，**没实机验证**，不对就 `cam rot 90`（在原有基础上再顺时针转 90°，保存） |
| 37 | 相机调色 / 防条纹 | 默认 XCLK 20→**10 MHz**；拍照时**暂停屏幕刷新**（LCD 刷新和摄像头 DMA 抢 PSRAM 带宽是条纹的头号嫌疑）；拍前丢 5 帧让曝光稳定；AWB/AEC/AGC/镜头校正全开。串口命令：`cam xclk <6-24>`、`cam quality <4-63>`（越小越好）、`cam awb on|off`、`cam wb auto|sunny|cloudy|office|home`、`cam rot <0/90/180/270>`、`cam status` |
| 38 | 拍完先预览 | 拍照后直接显示这张照片，三个按钮：**重拍**（删掉这张回取景）、**寄给克**、**保留**（回取景）。从「相册」进入的还是 删除 / 寄给克 / 返回 |
| 39 | 寄给电脑更可靠 | 关 Wi-Fi 省电（`WIFI_PS_NONE`）；每次发送前对目标 IP 发 3 次 ARP 请求（`etharp_request`，间隔 250 ms，给睡着的电脑网卡多几次醒来的机会）；失败自动重试 3 次（1 s / 2 s / 4 s）；在线探测也先发 ARP。提示改成人话：「电脑没找到，稍后自动重试」「电脑没回应」「电脑那边出错了(500)」，最终失败：「…没送出去，电脑醒着并开着 ke_bridge 再试」。电脑端不用再每 15 秒 ping 板子了（留着也无妨） |
| 40 | 发送中防连点 | 一条消息/照片在路上时，快捷语、表情、「寄给克」「删除」置灰、点了没反应，送完（或最终失败）恢复；寄给克还在 `camui_gallery_send` 里再挡一次，一张照片只会发一份 |
| 41 | SD 卡只探测一次 | 没插卡时 `sdmmc_card_init failed (0x107)` 只在第一次进相机/相册时出现一次；之后插了卡：串口 `photos rescan` |
| 42 | 颜色 | `GFX_RGB` 从截断改成四舍五入；灰色用新的 `GFX_GREY`（R=B、G 由同一级扩展，保证不偏色）；见 §3.6 |

### v6.1 补丁

| # | 内容 | 说明 |
|---|------|------|
| 43 | 拍照崩溃 | 现象：`A stack overflow in task cam_task`。`cam_task` 是 esp32-camera 驱动自己的任务，栈只有默认的 2048 字节，一遇到丢帧（`NO-SOI`）要打日志就溢出重启。`sdkconfig.defaults` 里加了 `CONFIG_CAMERA_TASK_STACK_SIZE=6144`。**自己编译的人要先删掉旧的 `sdkconfig`（`sdkconfig.defaults` 只在没有 `sdkconfig` 时生效）**。另外，解码 / 旋转 / 重新编码挪到了自己的任务 `cam_shot`（栈 16KB），不论谁调用（触摸任务、HTTP 任务、串口）都不再占它们的栈 |
| 44 | NO-SOI | `NO-SOI/NO-SO` = 驱动没在帧头找到 JPEG 起始标记 FFD8，帧被丢掉，最常见的原因是 DMA 写 PSRAM 没跟上、数据丢了一截（也是竖条纹的同一类根源）。这版：摄像头帧缓冲 1 → 2 个；拍照时要求拿到**完整**的 JPEG（FFD8…FFD9，尺寸对）才收，不行就丢掉再取，最多 4 次，串口能看到 `photo frame N unusable`。是否根治要上板看 `NO-SOI` 还出不出现 |
| 45 | 相机默认方向 | 按你的实测：`cam rot 0` + **`vflip off` + `mirror on`** 为默认（"靠传感器自己翻转"）。注意传感器只能上下/左右翻，**不能转 90°**，所以 `rotate 90/270` 时仍要软件转一个四分之一圈（预览和拍下的 JPEG，重新编码）；`rotate 0/180` 时完全不走软件旋转。`vflip / mirror / rot` 用了新的 NVS 键（`cam2_*`），所以之前试验时存的 `cam rot 180` 等设置**自动作废**，不需要手动 `cam rot 0` |
| 46 | 顶栏 | 「⌄」、小圆脸、「克」、绿点都在**最左**，右边空（peek 开着时才有小眼睛）。「⌄」在最左边；在顶栏任意位置下滑也返回；顶栏高度 56 → 48 |
| 47 | 底栏透明 | 没有胶囊背景，只有「+」和相机两个细线图标浮在聊天上面，图标外面描了一圈页面底色的边保证压在气泡上也看得清；聊天内容可以滚到图标下面（最新一条仍停在图标上方）；展开面板时这一条变成面板同色，「+」变 ×。图标之间的空白区域属于聊天区（可拖动、可长按说话） |
| 48 | 白色主题 | 照 iOS 浅色 iMessage：背景 `#FFFFFF`，克 `#E9E9EB` 黑字，她 `#007AFF` 白字，顶栏/面板 `#F2F2F7`，分隔线 `#D1D1D6`，图标深灰，脸页白底黑脸 |
| 49 | `theme light|dark|auto` | **默认 `auto`**：白天白色，进入夜间时段（`night` 设置，默认 23:00–07:00，需要 NTP 对过时）自动切暗色，早上切回；`night off` 或还没对时 = 一直白色。`theme light` / `theme dark` 固定不变。`POST /theme` 同样接受 `auto`。NVS 键改成了 `theme_mode`，所以之前存过的 `dark` 不会让新默认失效 |

### 第六期 v6.2

| # | 内容 | 说明 |
|---|------|------|
| 50 | 相机白平衡默认 office | 按上板实测（auto 偏粉）。`cam wb` 命令不变；NVS 里已存过 `cam wb …` 的以存的为准 |
| 51 | `cam sweep` | 竖条纹继续查。一条命令按 **XCLK 6 / 8 / 10 MHz × JPEG 质量 10 / 20** 共 6 组各拍一张，存成 `sweep-x6-q10.jpg` … `sweep-x10-q20.jpg`（SD 卡 DCIM；没卡时存内部，最多 12 张，要先删几张），同时每组抓 10 帧统计**驱动丢了几帧（NO-SOI / NO-EOI 各几次）**、每帧多少毫秒、多少 KB，串口打印成表。跑完自动恢复原来的设置。用法：先 `cam off` 退出相机界面，板子摆稳对着一个亮的、有细节的东西，再输入 `cam sweep`；把表和 6 张照片发回来对比 |
| 52 | 手写输入 | 板子只当纸，**不做识别**。「+」面板快捷语后面多一个「手写」胶囊；点开进手写页（见下） |
| 53 | 手写页 | 大方格（竖屏 300×300，横屏 260×260，有淡淡的十字虚线）里用手指写一个字；上方一条**预览条**放已经写好的字（缩小，最多 16 个，放不下时显示最后几个，左边一个小点表示还有）；按钮：**下一个**（把这个字收进预览条）、**撤销一笔**（撤最后一笔；方格是空的就把预览条里最后一个字拿回来并撤掉它的最后一笔）、**清空**（先清方格；方格已空再按就清空预览条）、**寄**（蓝色）。左上角「‹」回聊天页，写了一半的字和预览条**保留**，下次进来接着写 |
| 54 | 笔迹 | 触摸任务在手写页时 8 ms 采样一次（平时 30 ms），手一放开算抬笔要连续 4 次读不到；点与点之间用「中点二次贝塞尔」平滑（每对点 4 段），单点是圆点；笔宽约方格的 2.8%；小于约 2 像素的抖动丢掉；手指滑出方格时坐标夹在边缘。方格里画得越多每帧重画越慢（整屏软件渲染），估计 10–15 fps，线条形状不受影响，只是"跟手"的延迟 |
| 55 | 寄出去的图 | 点「寄」：方格里还有没收的字会先自动收进去；预览条里所有字拼成**一张横向长图**：**黑字白底 8 位灰度 PNG**，高 96 px，每个字一格 96×96（16 个字 1536×96 约 150KB），笔宽 6.5 px、抗锯齿、笔画取最大值不叠黑。一句只寄一张，`POST /ink`（Content-Type: image/png）。**发送队列、重试、ARP、发送中置灰全部沿用**，发送中「寄」按钮置灰。PNG 是自己写的（不压缩的 deflate 块，不需要 zlib） |
| 56 | 聊天里的缩略图 | 她那一侧（右边、蓝色气泡）里放这句话的缩小版：每格 32 px、每行最多 6 个字、最多 3 行，白线条；最近 8 张手写小图存在内存池里，更早的聊天气泡显示文字「[手写]」 |
| 57 | 电脑端 | `pc/ke_bridge.py` 新增 `POST /ink`：校验 PNG 文件头，存 `pc/inbox/ink/YYYYMMDD-HHMMSS.png` 并打印文件名。识别自己接：把 PNG 交给你的视觉模型即可 |

### 第六期 v6.3

| # | 内容 | 说明 |
|---|------|------|
| 58 | 手写：落笔即出线 | 原来每个新点都整屏重画 + 整屏推送（约 60–80 ms/帧）。现在笔在屏幕上时：只把**新来的点之间的直线**直接画到帧缓冲上，只推送**包含这几段线的那一小块矩形**（`board_lcd_flush_rect`，几十×几十像素，约 1 ms），延迟基本就是 8 ms 一次的触摸采样。**抬笔后**（这一笔有 ≥3 个点时）整屏重画一次，把这一笔换成平滑曲线；点一下（圆点）、短短一划不重画。撤销 / 清空 / 下一个 / 切页仍是整屏重画 |
| 59 | 屏幕下半部分点不上 | 我没法在没有实物时确认原因，做了三件事：① **触摸区加大**：手写页四个按钮上下各放大（最下面一排往下 10px，其余紧邻的取间隙的一半，互不重叠；按钮间隙 8→12px）、左右各放大；聊天页「+」和相机的触摸区往上多 10px；相机/相册的三个按钮上下各 8px；② **`touchlog on` 时屏幕上画黄色小圆圈 + 十字**，停在控制器报告的位置（手指抬起后留 0.9 秒），一眼就能看出偏了多少；串口同时打印原始值和换算后的值；③ **`touchrange`** 打印开机以来见过的**原始坐标最小/最大值**（先 `touchrange reset`，然后把屏幕四条边、四个角都划一遍再 `touchrange`）。如果原始范围明显比 0..319 × 0..479 小（触摸面板边缘不灵），按它提示的那行 **`touchcal xmin xmax ymin ymax`** 一拉伸就修好（存 NVS，`touchcal reset` 恢复）。另外把镜像换算的 `W - rx` 改成 `W - 1 - rx`（差 1 像素）。透明底栏的「+」和相机只占左右各 64px 宽，中间是聊天区，没有拦截下面按钮 |
| 60 | 气泡换色 | 白底：克 `#1C1C1E` 白字；暗色：克 `#3A3A3C` 白字（比黑底亮，不糊）；**她两个主题都是粉色**。你给的 `#FF7EB3` 配白字对比度只有 2.4:1（看不清），所以直接用你备选的深粉 **`#F0609E`**（3.3:1）。颜色宏在 `main/ui_colors.h`（`COL_*_KE_BUBBLE` / `COL_*_HER_BUBBLE`），想要浅一点的粉自己改成 `GFX_RGB(0xFF, 0x7E, 0xB3)` 并把 `COL_*_HER_TEXT` 改成深色 |
| 61 | 手写缩略图 | 放在她的粉色气泡里：白色圆角小纸片、黑字，四周留粉色边 |
| 62 | `imu invert` 默认开 | 按实测（平放朝上 z≈-1.07 被当成扣桌）。NVS 里已经存过的以存的为准 |

### 第六期 v6.4

| # | 内容 | 说明 |
|---|------|------|
| 63 | 上下边缘点不动 | 你的 `touchlog` 数据显示控制器边缘坐标是有的，所以问题在"这次触摸算不算点击"。代码里能吞掉点击的地方有四处，都改了：① **拖动判定**：手指在边缘会滚动，坐标跳几十像素，原来超过 10 px 就当拖动、点击作废。现在**手指落在按钮上时 28 px 内都不算拖动**（滑出按钮 28 px 以上才取消，仍能取消）；不在按钮上时阈值 14 px，离屏幕边 40 px 以内 20 px。② **目标判定**：原来"按下位置的元素 = 松手位置的元素"才算，边缘第一个采样点常落在按钮旁边。现在按下或松手任意一处在按钮上就算这个按钮（都不在按钮上才要求两处一致）。③ **松手判定**：连续 2 次读不到算松手 → 3 次（手写页 4 次），边缘控制器时断时续时不会把一次触摸切成两次。④ **切成两次也不重复触发**：同一个元素 250 ms 内第二次点击忽略（否则"+"会开了又关、发送键会发两遍）。松手用最后一次按下位置（v4.1 的修复）对所有页面仍有效，有主机测试覆盖 |
| 64 | 顶栏下滑不吞点击 | 「⌄」是按钮，手指落在上面 28 px 内按点击处理；往下滑 ≥40 px 才是"返回脸页"，两者不冲突（主机测试：⌄ 上手指滚了 20 px 仍然点击有效，下滑仍然返回） |
| 65 | `touchlog` 更详细 | 每次按下、开始拖动、松手都打印一行：`down (x,y) hit=… [edge] drag>Npx`、`drag started …`、`up: tap hit=… -> app` / `handled by the UI` / `ignored (…原因…)`（"两端不是同一个元素"、"发送中"、"250 ms 内重复"）/ `drag … -> swipe handling` / `long press ended`。hit 的数字对照 `main/ui_render.h` 里的枚举 |
| 66 | `touchrange` 为什么漏了边缘 | 代码里每个读到的采样点都会更新最小/最大值，没有路径漏记。原因是**采样太稀**：触摸任务 30 ms 读一次，快速划过屏幕只有几个点，落不到最边上（你划出来的 y 38..458 就是这样）。所以这个统计只能用来判断"面板边缘是不是死区"——**手指按住边缘不动 1 秒**再看。现在会同时打印采样点数。`touchcal` 别再按快速划出来的范围设 |
| 67 | 手写气泡 | 手写笔迹**直接画在她的粉色气泡上，白色笔迹、透明底**，气泡就是普通粉色气泡（寄给电脑的 PNG 仍是白底黑字） |
| 68 | 「+」和相机图标 | 图标下面加实心圆底（和顶栏同色），压在气泡上也清楚，不再像错位的十字 |
| 69 | 顶栏小脸 | 小圆 36→**40 px**；圆里只放眼睛和嘴（第一个 `(` 到最后一个 `)` 之间，不带括号和 ♡ 等），再按宽度缩小 |
| 70 | 粉色 | `#F2708F`（比 `#F0609E` 偏橙一点），以实机看起来为准，不对就改 `main/ui_colors.h` 里两个 `COL_*_HER_BUBBLE` |
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
python -m esptool --chip esp32s3 --port COM3 write-flash 0x10000 release/ke-body-app-v4.1.bin
```

注意：v4 改了分区表（多了一个 4MB 的 `storage` 分区给照片用），**从 v3.x 升到 v4 的第一次必须烧合并版** `ke-body-v4.bin`（从 0x0），之后的 v4.x 才能只刷 0x10000。**v4 → v4.1 分区表没变（已逐字节对比），直接刷应用区即可，Wi-Fi 等设置保留。**合并版从 0x0 整片写入，NVS 区（0x9000）会被写成 0xFF，也就是**合并版会清掉 Wi-Fi、server 等设置**，烧完要重新 `wifi` / `server`；只刷应用区不会。

（旧版本文件都保留，换文件名即可回退。）

如果板子没自动进入下载模式：按住 **BOOT** 键，按一下 **RESET**，松开 BOOT，再执行上面的命令。
烧完后按一下 RESET（或重新插电）。冷上电后固件会主动软复位一次（官方例子的做法），属正常现象。

如果 esptool 提示 flash 大小和固件头不符，可以加 `--flash-size 16MB`（微雪官方示例配置为 16MB Flash，QIO 80MHz）。

注意：上面是 esptool **v5** 的写法（本仓库用 esptool v5.4.0 生成固件）。如果你装的是 esptool v4，子命令是下划线：`write_flash`。想快一点可以加 `-b 921600`。

`release/ke-body-vX.bin` 内容 = bootloader（0x0）+ 分区表（0x8000）+ 应用（0x10000），中间用 0xFF 填充；`ke-body-app-vX.bin` 只有应用（烧到 0x10000）。v4 / v4.1 合并版约 3.4MB。

---

## 3. 配网与使用

### 3.1 串口命令

> v4.1 起串口控制台用自带的行编辑器（`main/lineedit.c`），**可以输中文**（`msg 想你了`、`say 你好`）：IDF 自带的 linenoise 会把所有高位字节（UTF-8）删掉，命令行里只要有中文就变成空参数，所以换掉了。需要终端编码设为 UTF-8（PuTTY: Window → Translation → UTF-8；Tera Term: Setup → Terminal → Kanji 选 UTF-8；Windows Terminal / miniterm 默认就是）。输入不是合法 UTF-8 时会提示。上下箭头有历史（16 条），退格按整个字符删；**没有 Tab 补全**。需要终端支持 ANSI 转义（上面几个都支持）。

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
| `buttons` / `buttons reset` | 打印当前按钮配置 JSON / 恢复内置默认（并清掉 NVS 里存的旧配置） |
| `anim` | 打印各动画开关状态 |
| `anim on|off` | 所有动画的总开关（关掉时各项开关的设置保留），存 NVS |
| `anim blink|blush|zzz|shake|flash on|off` | 单项开关，存 NVS。blink 眨眼（默认关）、blush 脸红、zzz 睡觉的 z 飘、shake 摇晃抖脸、flash 新消息边框闪（默认关） |
| `chime on|off` | 收到消息时是否响一声，存 NVS，默认关 |
| `imu` / `imu invert on|off` | 打印加速度；如果「扣在桌上」方向反了（拿起来反而睡），`imu invert on` |
| `autorotate on|off` | 随手转屏（默认关；轴向未验证） |
| `cam on|off|shot|gallery|status` | 进/出相机、拍一张、进相册、打印相机设置 |
| `cam xclk <MHz>` / `cam quality <n>` / `cam awb on|off` / `cam wb <auto\|sunny\|cloudy\|office\|home>` / `cam rot <0\|90\|180\|270>` | 相机画质调节，全部存 NVS，对比着试（见 §3.7） |
| `touchrange [reset]` / `touchcal [xmin xmax ymin ymax \| reset]` | 原始触摸范围诊断 / 拉伸校准（见 v6.3） |
| `theme light\|dark\|auto` | 主题（默认 auto，夜间时段自动暗色），存 NVS |
| `color` / `color <gamma%> <r%> <g%> <b%>` / `color reset` / `colortest` | 屏幕颜色校准、色块对照屏（见 §3.6） |
| `cam vflip on|off` / `cam mirror on|off` | 画面上下翻/左右镜像，存 NVS（默认 vflip on，同官方例子） |
| `peek on|off` | 「让克看看」远程拍照开关，存 NVS，默认关 |
| `photos [rescan]` | 列出照片（SD 卡或内部）；`rescan` = 开机后才插卡时重新找一次 |
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
curl -X POST --data-binary "auto"  http://192.168.1.23/theme             # light / dark / auto（夜间自动暗色），存 NVS
curl -X POST --data-binary "我想你"  http://192.168.1.23/heard             # 她说的话（语音转文字结果）进聊天右侧，不闪
curl -X POST -H "Content-Type: application/json" --data-binary @buttons.json http://192.168.1.23/buttons
        # {"text":["想你了","抱抱","在干嘛","晚安"],"emoji":["(´ω`)","(≧▽≦)","♡","💧"],"shake":"想你了"}
        # 快捷语最多 8 个（每个 12 字以内）、表情最多 30 个（每个 20 字以内），整体 <2KB；也可以只给一个数组（=快捷语）。GET /buttons 看当前值
curl -X POST --data-binary "reset" http://192.168.1.23/buttons           # 恢复内置默认按钮
curl -X POST --data-binary "off"   http://192.168.1.23/anim              # 全部动画 on/off
curl -X POST --data-binary "blink on"  http://192.168.1.23/anim          # 单项：blink blush zzz shake flash
curl -X POST --data-binary "status"    http://192.168.1.23/anim          # 看各项开关
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
python pc/ke_send.py anim off              # 全部动画
python pc/ke_send.py anim blink on         # 单项：blink blush zzz shake flash
python pc/ke_send.py anim status
python pc/ke_send.py buttons reset         # 恢复默认快捷语和表情
python pc/ke_send.py chime on
python pc/ke_send.py peek on
python pc/ke_send.py snap photo.jpg       # 远程拍一张存到本地（需要 peek on）
```

### 3.3.1 屏幕操作（第五期）

- **脸页**（开机默认）：黑底大脸；脸下面一行淡字是克最近一句；右上角小字是 IP（peek 开着时旁边有小眼睛）；底部「⌃ 上滑聊天」。点脸 = 脸红；**按住 0.5 秒 = 说话**（同第二期）。**手指从下往上划 ≥40px**（或点底部那行小字）进聊天页。
- **聊天页**：顶栏「⌄ ｜ 小圆里的脸 / 克 ●」——点⌄，或在顶栏上往下划，回脸页；消息区上下拖动翻历史（往下拖 = 看更早的），按住 0.5 秒同样是说话。底栏左「+」右相机：点「+」展开面板（快捷语 + 表情，表情区左右滑翻页），点面板里的按钮 = 发送；再点「+」或点面板上方空白处收起。点相机图标进相机。
- **相机**：取景；点画面或「拍照」拍一张（屏幕冻结几秒，拍完直接显示这张照片，按 重拍 / 寄给克 / 保留 决定）；「相册」看照片；「返回」回**聊天页**并关相机（省电）。取景是横屏 480×320，竖屏模式下只显示中间一截。
- **相册**：左右滑动切换（拖动超过 40px），「删除」直接删当前这张，「寄给克」发到 bridge `/photo`（只有点了才发），「返回」回聊天页。
- **睡觉**：屏幕朝下扣桌上 1.5 秒 → 睡脸 + z 飘 + 背光 5；拿起来或摸一下屏幕恢复。
- **换颜色**：改 `main/ui_colors.h` 里的 `COL_D_*`（暗色）/ `COL_L_*`（亮色）宏，重新编译。字段有 BG、KE_BUBBLE、KE_TEXT、HER_BUBBLE、HER_TEXT、BAR、SEP、CAP（胶囊）、CELL（表情格）、ONLINE、OFFLINE 等。

### 3.6 颜色对不上时：校准

v5 反馈「墨绿显示成亮薄荷绿、奶米色显示成灰白」。我在电脑上能证明的部分：`GFX_RGB` 的 RGB565 打包（红在高位、绿 6 位、蓝低位）、帧缓冲里的字节序（高字节在前，和微雪自己 LVGL 例子送屏的格式一致）、屏幕初始化（BGR 元素顺序 + 反色，和官方例子逐行相同，`esp_lcd_st7796.c` 与官方逐字节相同）都是对的（`tools/test_colors.c`）。颜色偏差因此更像**屏幕本身的伽马/色温**（便宜的 IPS 屏常见偏冷、暗部发亮），以及 RGB565 的量化（5/6/5 位，灰色最容易偏绿或偏蓝——v6 已改成四舍五入 + `GFX_GREY`）。我没有实物没法量，所以给你两个工具：

1. `colortest`：屏幕上出一组色块，每块标着它**应该是**的 `#RRGGBB`（黑、`#1C1C1E`、`#262628`、`#2C2C2E`、`#0A84FF`、白、绿、旧的墨绿 `#2F4A3A` 和奶米 `#EFE3CF`、红绿蓝、一排灰阶）。拿手机打开同样的色值对着看；点屏幕退出。
2. `color <gamma%> <r%> <g%> <b%>`：在送屏时对每个像素做校准，**立刻生效并存 NVS**。`gamma%` 大于 100 让中间调变暗（暗部发亮时用，如 `color 120 100 100 100`）；`r/g/b%` 是三个通道的增益（偏冷偏蓝时把 b 降一点，如 `color 110 100 96 88`）。`color reset` 恢复。默认不校准（100 100 100 100）。校准会多花约几毫秒/帧的 CPU。

调好的一组数请告诉我，下一版写成默认。

### 3.7 相机画质怎么调（对比着试）

| 现象 | 试这个 |
|------|--------|
| 满屏竖条纹 | `cam xclk 10`（默认）；还有就 `cam xclk 8` 或 `cam xclk 6`；条纹和画质此消彼长，帧率会更低 |
| 画面发灰、偏紫偏粉 | `cam awb on`（默认）、`cam wb sunny/cloudy/office/home` 换一个试；OV5640 模组如果**没有红外截止滤光片**，白平衡永远救不回来（发紫发粉的典型原因），那是硬件 |
| 照片躺着 / 倒着 | 默认（v6.1）已是 `vflip off` + `mirror on` + `rot 0`（实测）；仍不对再 `cam vflip` / `cam mirror`（传感器自己翻，最省）或 `cam rot 90/180/270`（软件转，会重新编码，180 会让整张照片多走一遍软件旋转，尽量别用） |
| JPEG 太糊 / 太大 | `cam quality 6`（更好更大）…`cam quality 20`（更小） |

`cam status` 打印当前所有设置。**`cam sweep`**（v6.2）一次拍 6 组 XCLK × 画质并打印 NO-SOI 丢帧表，用来客观选条纹最少的一组。

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
  ui.c/.h         UI 状态：聊天环、按钮配置、页面滑动/面板/气泡滑入动画 tick、触摸状态机、旋转/主题/动画开关（NVS）
  ui_render.c/.h  画面布局 + 命中测试（纯 C，横竖屏、深浅两套配色、脸页/聊天页/相机/相册）
  ui_colors.h     所有界面颜色宏（COL_D_* 暗色、COL_L_* 亮色）
  colorcal.c/.h   可选的屏幕颜色校准（gamma + 三通道增益，纯 C）
  imgrot.c/.h     相机图像旋转 + 字节交换（纯 C）
  ink.c/.h        手写：笔画存储/撤销/平滑、拼成一张横向 PNG、聊天缩略图（纯 C）
  gfx.c/.h        小型软件渲染器
  kb_font.h / fonts/   位图字体
  wifi_mgr.c/.h   NVS 凭据 + STA 连接 + 自动重连
  http_api.c/.h   /ping /face /say /play /volume /rotate /brightness /theme
  console_cmd.c/.h  串口命令 + 控制台任务
  lineedit.c/.h   UTF-8 行编辑器（纯 C）
components/esp_lcd_st7796/   官方 ST7796 驱动（原样拷贝）
components/es8311/           官方 Arduino 库里的 Espressif es8311 驱动（I2C 层改为 i2c_master）
components/XPowersLib/       官方例子用的 AXP2101 驱动（MIT）
components/esp32-camera/     Espressif 摄像头驱动 2.0.15（官方仓库里的那份）
pc/ke_bridge.py              电脑端：收录音 /hear、文字 /msg、照片 /photo，存 pc/inbox/
pc/ke_send.py                电脑端：给板子发 face / say / heard / play / volume / rotate / brightness / theme / buttons / anim / chime / peek / snap / ping
tools/gen_fonts.py           字体生成脚本
tools/host_preview.c         电脑上渲染画面到 PPM（支持横竖屏、深浅色）
tools/run_host_tests.sh      主机端测试（行编辑器、触摸状态机）；tools/test_*.c，tools/hoststubs/ 是 IDF 头文件的桩
release/ke-body-vX.bin       合并固件（0x0）
release/ke-body-app-vX.bin   只含应用（0x10000，第四期起）
```

### 5.1 字体方案

没有用 LVGL，文字是自己渲染的 4bpp 抗锯齿位图字体，由 `tools/gen_fonts.py` 用系统字体生成：

| 字体 | 像素 | 内容 | 来源 |
|------|------|------|------|
| face96 / face64 / face44 / face30 / face18 / face13 | 96/64/44/30/18/13 | ASCII + 约 280 个颜文字常用符号（含 ˍ 皿 ﹏）+ 两个线条图标 💢 💧 | DejaVu Sans，缺字回退文泉驿正黑 / Unifont；图标由脚本用 Pillow 画 |
| text22 | 22 | ASCII + 中文标点 + **GB2312 全部 6763 个汉字** + 颜文字符号 | 文泉驿正黑（WenQuanYi Zen Hei） |
| small14 | 14 | ASCII + 扫描 `main/*.c` 得到的所有界面用汉字 | 文泉驿正黑 |

位图总计约 2.4MB，全部放在 Flash，不占 RAM。字库里没有的字画一个空心方框。💢（U+1F4A2）和 💧（U+1F4A7）是彩色 emoji，没有单色字体，`gen_fonts.py` 里的 `render_icon()` 用线条画成和表情同高的位图，当作普通字形放进 face 字体和气泡字体，颜色跟随主题。要加别的 emoji 图标就在 `ICON_CODEPOINTS` 里加一项并写画法。脸页颜文字先试 96px，再依次降到 64/44/30px，再放不下折两行；聊天小脸和表情格用 30/18/13px。**新加界面文字后要重新跑 `gen_fonts.py`**，否则小字显示成方框（small14 会自动扫描源码里的汉字）。横屏时脸的可用宽度是 456px，长颜文字更容易保持 64px。

字体版权：DejaVu（自由字体许可）、文泉驿正黑（GPLv2 + 字体嵌入例外）、GNU Unifont（GPLv2+ 字体例外 / OFL）。

重新生成字体：`python3 -m venv .venv && .venv/bin/pip install pillow fonttools && .venv/bin/python tools/gen_fonts.py`

### 5.2 电脑上预览画面

```
gcc -O1 -Imain -o preview tools/host_preview.c main/gfx.c main/ui_render.c main/fonts/font_*.c -lm
./preview out.ppm --land --page chat --panel 255          # 聊天页 + 面板
./preview out.ppm --port --line "早安" --light             # 竖屏、亮色、脸页
# 其它：--slide N(切页中间帧) --epage N --sleep --blush N --offline --toast 文字 --scroll PX，见 tools/host_preview.c 开头
```

---

### 5.3 主机端测试（不需要板子）

```
tools/run_host_tests.sh [/path/to/esp-idf]
```

- `tools/test_colors.c`：`ui_colors.h` 里的颜色打进帧缓冲后是不是预期的 RGB565、字节序对不对、`GFX_GREY` 是否保持中性、颜色校准。
- `tools/test_ink.c`：手写笔画编辑（撤销/清空/下一个/上限）、平滑连续、PNG 头和拼接尺寸、缩略图换行；`INK_PNG_OUT=x.png` 可以把生成的 PNG 存出来看。
- `tools/test_imgrot.c`：相机图像 90/180/270 度旋转方向和字节交换。
- `tools/test_lineedit.c`：串口行编辑器（中文/emoji 原样通过、退格删整字、历史、Ctrl-C/U、CRLF、超长）。
- `tools/test_ui_touch.c`：把**真实的** `main/ui.c` 触摸状态机和 `main/ui_render.c` 命中测试在电脑上编译（`tools/hoststubs/` 里桩掉 FreeRTOS、定时器），模拟点按、松手不带坐标、长按、拖动、聊天滑动、相机/相册按钮和左右滑。v5 起覆盖：脸页上滑进聊天、点提示条、点⌄、顶栏下滑、短聊天下滑返回、长聊天下滑是翻历史而不是返回、「+」展开/收起、点空白收起、快捷语和表情点击（含绝对序号）、表情左右翻页、相机图标、两个页面的长按说话、松手不带坐标（v4 那个 bug）、新消息 `(—o—)` 一秒后恢复、动画默认值和分项开关、默认表情在各字号字库里有字。需要 ESP-IDF 里的 cJSON 源码。

---

## 6. 未验证事项（没有实物，请上板确认）

### 已上板验证

v1：屏幕点亮、方向 0 正确不镜像、等待配网界面、气泡、触摸脸红、冷上电软复位一次不影响使用。
v2：开机 `ES8311 in Slave mode and I2S format`、`audio: ready`；家里 2.4G Wi-Fi 连上；HTTP `/face /say` 200 ok 且屏幕跟着变。**喇叭无声**（`/play` 返回 200 但听不到）。录音未测。

v3 / v3.1：AXP2101 应答、电源轨全开；ES8311 寄存器与官方驱动一致；`audio test` 回环 R=8000、`play done` 999ms（I2S、DAC 通）；TCA9554 P7 拉高后喇叭出声，`volume 100` 很大。

v3.3：横屏 270、黑底、亮度、夜间变暗、喇叭（TCA9554 P7 使能）、`audio test` 回环 R=8000、麦克风有信号。按住录音上传还没测。

### v4.1 修复（主机测试通过，未上板）

1. **按钮点了没反应**：触摸控制器松手后不再报坐标，`touch_task` 传给 `ui_touch` 的是 (0,0)，v4 用它做松手时的命中测试，得到的是「脸」，和按下时的按钮不一致，所有点击被丢弃。现在松手一律用最后一次按下时的坐标（`ui.c`），触摸任务也保留最后坐标。
2. **触摸任务容忍漏读一次**：FT6336 偶尔会在按住途中某一次扫描报「无触点」，v4 会当成松手（长按录音会提前结束、拖动会中断）。现在连续两次读不到才算松手，松手识别慢约 30ms。
3. **聊天上下滑方向反了**（自查发现，你还没碰到）：v4 往下拖是更靠近最新，与手机相反。现在手指往下拖 = 看更早的消息，往上拖回最新；新消息来了自动回到底部。
4. **串口中文**：见 §3.1。IDF linenoise 的 `sanitize()` 用 `isprint()` 把 ≥0x80 的字节全删，不是终端 GBK 的问题；自带行编辑器已经替换。上板后 `msg 想你了` 应该能进聊天并发到 bridge。
5. 串口现在跑在我们自己的任务里（栈 8KB，`cam on` 之类耗栈的命令也在这个任务里跑）。和之前相比只少了 Tab 补全，其它命令不变。

### v6.4（全部未上板）

1. **边缘点击**：改动针对的是"控制器边缘坐标乱跳"这个最可能的原因。如果上板后仍然有点不动的，`touchlog on` 点几次，把每次松手打印的那一行（`up: …` 是哪种）发我，能直接看出是拖动、两端不一致、还是根本没有 `down`（那就是控制器没报，要查硬件/`touchrange`）。
2. **顶栏小脸**：长颜文字（如 `´・ω・\``）在 40 px 小圆里依然很小，这是字库大小限制。
3. 粉色 `#F2708F`、白字 3.0:1 的对比度，只在电脑上看过。

### v6.3（全部未上板）

1. **手写延迟**：应该是落笔即出线，但"直线→抬笔后变曲线"会有一次轻微形变，请看观感。写得极快时点与点之间是直线（8 ms 采样，FT6336 自己的报点率也有上限）。屏幕上另有别的东西触发整屏重画时（`touchlog on` 的小圆圈每次移动都要整屏重画），手写会退回到慢速，**测手写延迟前先 `touchlog off`**。
2. **底部点不上的真正原因**未知；先看 `touchlog on` 的黄圈是否落在手指下面，再看 `touchrange`。如果黄圈和手指对不上（特别是越靠底边越偏），把那两个输出发我。
3. 粉色 `#F0609E` 上的白字、白底黑字缩略图的观感只在电脑预览里看过。

### v6.2（全部未上板）

1. **手写手感**：采样 8 ms + 全屏重画，跟手程度只能上板看。太慢的话告诉我：可以改成只刷新方格那一段 LCD（`board_lcd_flush` 目前只有整屏）。
2. **PNG 有多大、对方读不读得清**：16 个字约 150KB，POST 到 bridge 没问题；笔宽 6.5 px/96 px 对于笔画多的字（如「麻烦」）会有点糊，不够粗或太粗告诉我改 `main/ink.c` 里 `ink_make_png` 的 `6.5f` 和 `8.f`（边距）。
3. **`cam sweep`**：`NO-SOI`/`NO-EOI` 是靠拦截驱动的日志行来数的（驱动自己丢了帧不会告诉应用），所以只有真的打了这两行日志才计数。
4. **手写页不在自动转屏里特殊处理**：写到一半转屏，笔迹是归一化坐标，会跟着方格一起缩放，不会丢。
5. 寄手写时如果 bridge 不通：走和文字一样的 ARP + 重试；最终失败聊天里那张缩略图**仍然在**（和文字消息一致）。

### v6.1（全部未上板）

1. **崩溃**：栈加到 6144 后拍照不应再重启；请看 `cam_task` 还有没有栈溢出、`NO-SOI` 是否还出现、`photo frame N unusable` 出现几次。`NO-SOI` 的根因（10MHz XCLK 下为什么还丢帧头）我没法在没有实物时查清，加了双缓冲和"只收完整帧"两个缓解；如果仍然频繁，试 `cam xclk 8` / `cam xclk 16` 对比，并把日志发我。
2. **相机方向**用你的实测值做默认，但软件转 90° 的方向（rotate 270 → 逆时针）仍是推导的；如果 rotate 270 下取景现在是对的，说明推导没错。
3. **白天/夜间自动切换**依赖 `light_night_active()`：NTP 没对上时它是"不在夜间"，所以会一直是白色；每秒检查一次，切换时整屏重绘会闪一下。
4. **透明底栏**的图标描边、聊天内容滚到图标下面的观感只在主机预览看过。深色主题的描边是黑色，压在蓝色气泡上是一圈黑边，浅色主题是白边。

### 第六期（v6，已上板）

只在电脑上验证过：编译通过（应用 3.8MB，6MB 分区剩 38%）、分区表和 bootloader 与 v4.1 逐字节相同、主机测试通过（触摸/页面、颜色、图像旋转）、画面用 `host_preview` 渲染后看过。**上板后请确认**：

1. **颜色**：先 `colortest`，和手机对着看；不对就 `color …`（§3.6）并告诉我数值。若 `colortest` 里 `#1C1C1E`、`#262628`、`#2C2C2E` 三块在屏幕上分不太开（RGB565 只有 32 级红蓝，这三个灰色量化后是 25/41/41），说明需要调 `color`，不是 bug。
2. **顶栏小圆里的脸**：36px 圆里放缩小的颜文字，短的（`—_—`）能看清，长的（`´・ω・\``）会很小。比「约 28px」大了一点是为了看得清；要改 `ui_render.c` 里的 `AV_D`。
3. **相机转正的方向**是推导出来的（传感器 x 轴沿壳子短边），没实机验证；错了用 `cam rot`。预览转正在每帧软件旋转 320×480，帧率估计比 v5 低（XCLK 10 MHz + 旋转），没测。
4. **条纹是否消失**：XCLK 10 MHz + 拍照时冻结屏幕只是最可能的两个原因，不保证；用 `cam xclk` 对比。预览时屏幕仍在刷新，若预览有条纹而照片没有，就是预览没暂停刷新的原因。
5. **拍照重编码**：864×1536 JPEG 解码 → 旋转 → 编码，占 PSRAM 约 5MB 短暂峰值，耗时几秒（屏幕冻结）；失败时回退成没转的原图。
6. **重试时间**：`ESP_ERR_HTTP_CONNECT` 之类失败会重试 3 次，最坏情况这一条消息占用发送队列约 7 秒+每次的 3 次 ARP（0.75 秒）+ HTTP 超时（15 秒）；这期间按钮置灰。
7. **ARP 唤醒**对「电脑网卡省电漏广播」应该有帮助，但**不保证**（要看路由器的 DTIM 和电脑网卡设置）；根治办法是关电脑无线网卡的省电（设备管理器 → 网卡 → 电源管理 / 高级）。
8. **顶栏高度**：为放小圆加到 56px，横屏时聊天区比 v5 少约 10px。

### 第五期（v5，已上板：功能都在，观感反馈见第六期）

只在电脑上验证过：编译通过（应用 3.8MB，6MB 分区剩 39%）、分区表和 bootloader 与 v4.1 逐字节相同（所以 app-only 可以直接刷）、主机测试通过、画面用 `host_preview` 渲染后看过。**以下上板后请逐条确认**：

1. **切页/气泡滑入的帧率**：整屏软件渲染 + SPI 推屏，估计 12–20 fps，0.25 秒的滑动可能只有 3–5 帧，会有点顿。如果观感不行，把 `main/ui.c` 里 `PAGE_STEP` 调大（更快）或把切页改成硬切。
2. **手势手感**：竖滑阈值 40px、判定为拖动 10px。太灵敏/太迟钝改 `SWIPE_PX` / `DRAG_PX`。FT6336 偶尔漏读一帧，已容忍一次。
3. **小头像**：克的消息头像是发送时表情缩小到 18px（放不下 13px），很长的颜文字会被裁在头像框里，没在真机看。
4. **在线点**：探测的是 `server` 地址同一主机的 `GET /ping`；`ke_bridge.py` 对任何 GET 都回 200，所以电脑上 bridge 开着 = 绿。没设 `server` 或 Wi-Fi 没连 = 灰。第一次探测在开机后 Wi-Fi 连上才开始，最长延迟 15 秒。
5. **旧 NVS 里的按钮配置**：如果你之前 `POST /buttons` 存过自己的配置，升级后仍然用旧配置（只有 4 个表情），要用新默认表情请 `buttons reset`。
6. **动画默认值变了**：眨眼、边框闪默认关。NVS 里没有这些键，所以升级后自动是新默认；之前的 `anim off` 总开关（若存过）仍然生效，想恢复用 `anim on`。
7. **横屏表情格**：横屏每格约 110px 宽，`(—ω—)` 在 30px 下放不下，所以整页统一用 18px，比竖屏小一些；想要更大可以在 `ui_render.c` 里减小格间距或改列数。
8. **脸页的长按说话**、**相机回聊天页的滑动动画（无，直接切）**、**竖屏排版**（主机预览看过，真机没看）。
9. 聊天页在「+」面板展开时，消息区高度变小，最新消息始终留在可见区；这个滚动位置行为只在主机测试里验证。

### 第四期（v4，v4.1 之外的部分仍未验证）

1. **触摸坐标**：rot270 下 `touchlog` 显示按钮行落在 y≈261–283，和布局一致，方向换算看来是对的；其它旋转方向（0/90/180）没验证。
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
