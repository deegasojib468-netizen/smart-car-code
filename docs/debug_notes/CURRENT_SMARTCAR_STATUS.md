# 智能车当前代码状态报告

> 审计日期：2026-07-01
> 审计范围：以 `example/user/main_cm7_1.c` 为主；仅为确认 GPIO 语义和 MT9V03X 参数而读取了相关库文件。未修改 CM7_0、libraries 或工程配置。
> Skill 说明：仓库隐藏目录和本机 Codex skills 中未找到名为“智能车视觉代码防错 Skill”的 `SKILL.md`；本报告按本次用户明示的防错限制和仓库 `AGENTS.md` 执行。

## 1. 当前 Git 状态

生成本报告前执行 `git status --short` 的原始结果：

```text
 M example/iar/project_config/settings/cyt4bb7_cm_7_0.Debug.cspy.bat
 M example/iar/project_config/settings/cyt4bb7_cm_7_0.Debug.cspy.ps1
 M example/iar/project_config/settings/cyt4bb7_cm_7_0.Debug.general.xcl
 M example/user/main_cm7_1.c
?? docs/debug_notes/
?? example/user/backups/
?? example/user/main_cm7_1_bad_overlay_backup.c
?? example/user/main_cm7_1_before_fsm_merge.c
```

- 主代码修改：`example/user/main_cm7_1.c` 是已跟踪且已修改的文件。
- IAR 自动修改：`example/iar/project_config/settings` 下的 3 个 CM7_0 调试配置文件已修改，不应提交。本次未读取或修改其内容。
- 未跟踪备份：`example/user/backups/` 中有 `main_cm7_1_P06_4_U8_pullup_fixed_before_old_board.c` 和 `README_P06_4_U8_pullup_fixed_before_old_board.md`；另有两个顶层备份 C 文件。不应提交或删除。
- 调试文档目录当时整体为未跟踪状态。

## 2. 当前运行目标

当前全局开关：

| 宏 | 当前值 |
| --- | ---: |
| `CAMERA_MINIMAL_RAW_TEST` | `0U` |
| `CAMERA_DIAG_TEST_PATTERN` | `0U` |
| `VISION_RAW_IMAGE_TEST` | `0U` |
| `VISION_STATUS_BAR_ENABLE` | `1U` |
| `VISION_OBJECT_OVERLAY_ENABLE` | `0U` |
| `ENABLE_FORCE_NEXT_KEY` | `0U` |

- 当前是旧主板按键版本，`OLD_BOARD_KEYS = 1U`，实际按键代码也只使用 P20_3/P20_2/P20_1/P20_0。
- 当前不是 minimal raw test 或 raw image test，而是四状态机正常视觉处理模式。
- 每帧通过 Seekfree Assistant 发送 `display_image`，因此仍用于 DAP 图传。
- 目标框/线/十字等对象 overlay 已关闭；但状态栏 overlay 仍开启，所以不能说“所有图形 overlay 都已关闭”。
- `VISION_DISPLAY_DENOISE_ENABLE = 1U`，DAP 发出的不是纯原始图，而是去噪后再叠加状态栏的 `display_image`。
- `main_cm7_1.c` 中没有 `printf`、普通 UART 文本发送或调试文字输出；`DEBUG_UART` 仅用于 `SEEKFREE_ASSISTANT_DEBUG_UART`，当前未见普通文本混发。

## 3. 当前按键映射

当前实际生效的旧主板按键映射：

- K3 / 旧主板 S3 / P20_3：S2 -> S3。
- K4 / 旧主板 S4 / P20_2：S3 -> S0，`g_mine_area_count++`。
- K5 / 旧主板 S5 / P20_1：强制下一状态；`ENABLE_FORCE_NEXT_KEY = 0U` 时编译关闭。
- K6 / 旧主板 S6 / P20_0：任意状态复位回 S0，不清空 `g_mine_area_count`。

GPIO 配置现状：

- 4 个按键均由 `Cy_GPIO_Pin_Init` 配置为 `HSIOM_SEL_GPIO`、`CY_GPIO_DM_PULLUP`、`outVal = 1U`。
- SDK 对 `CY_GPIO_DM_PULLUP` 的定义明确是“Resistive Pull-Up, Input buffer on”，因此输入缓冲器开启。
- 按键以低电平为按下。
- 初始化 API 会先将输出锁存值写为 `1U`，之后主文件中没有对这些按键执行输出写入。
- K3/K4 均有 armed 机制和事件锁存；进入对应状态时清锁存和 armed，先观察到松开高电平才能 armed。
- K3 事件只在 S2 产生并由 S2 消费后清零；K4 事件只在 S3 产生并由 S3 消费后清零。
- 存在 `VISION_KEY_DEBOUNCE_COUNT = 20U` 的稳定电平/按下沿消抖链路，但 K3/K4 的 armed 和锁存实际直接使用 raw pressed 电平，没有使用消抖后的 `s_key_stable_level` 或 `pressed_edge`。K5/K6 使用消抖后的 `pressed_edge`。这是当前现状，本次未修改。

新主板残留检查：

- `main_cm7_1.c` 中没有 U8/P06_4、U1/P12_0、U10/P10_3、U11/P11_1 按键路径。
- 没有 `64H/64L`、`60H/60L`、`PU`、P06_0/P06_4 周期性重配等诊断残留。
- 新旧主板按键当前没有混用。

## 4. 当前四状态机流程

枚举与含义：

- S0：`VISION_STATE_BOX_ALIGN`，检测白框并计算水平中心误差。
- S1：`VISION_STATE_ENTER_LINE`，检测进入线并确认首条线跨越。
- S2：`VISION_STATE_POST_LINE_FORWARD`，等待过线后前进完成信号。
- S3：`VISION_STATE_SPIN_PROTECT`，旋转期间检测最近边界线并生成限速/停车/后退请求。

切换条件：

- S0 -> S1：白框有效且水平误差绝对值 `<= 10`，连续稳定 5 帧后锁存 `enter_line_enable`。
- S1 -> S2：进入线有效且 `line1_y_bottom >= MT9V03X_H - 25`，连续稳定 3 帧后锁存 `enter_ready`。
- S2 -> S3：`g_post_line_forward_done_input` 外部完成信号，或 K3/P20_3 锁存事件（`g_key_post_forward_done` / `g_key_old_s3_event_latched`）。
- S3 -> S0：`g_spin_done_input` 外部完成信号，或 K4/P20_2 锁存事件（`g_key_spin_done` / `g_key_old_s4_event_latched`）；切换前雷区计数加 1。
- K6/P20_0 可在任意状态调用 `vision_reset_to_s0()`，不修改雷区计数。

## 5. 当前图像链路

- 每帧首先执行 `mt9v03x_image -> work_image` 完整复制。
- 每帧同时执行 `mt9v03x_image -> display_image` 完整复制。
- 识别函数只读 `work_image`；主文件中没有对 `mt9v03x_image` 写入。
- `VISION_DISPLAY_DENOISE_ENABLE = 1U` 时，去噪只写 `display_image`，不改变 `work_image` 和原始采集缓冲。
- 对象 overlay 的 `draw_box_overlay`、`draw_enter_line_overlay`、`draw_nearest_line_overlay` 调用均受 `VISION_OBJECT_OVERLAY_ENABLE` 保护；当前值为 0，运行时不调用其中的框、线和十字绘制。
- 状态栏仍开启，每帧调用 `draw_status_overlay(display_image[0])`。
- 状态栏是 y=2 处的单行 3x5 小字体，不填充大面积背景，不会大面积覆盖图像，但会改写少量 `display_image` 像素。
- DAP 发送缓冲是 `display_image`，不是 `mt9v03x_image`；内存隔离和 DEBUG_UART 专用性当前安全，但发出画面并非纯 raw。

当前状态栏格式：

- S0 有效：`S0 Xddd EX+ddd EN0/1`；无效：`S0 X--- EX--- EN0`。
- S1：`S1 Yddd C0/1 L0/1`，无效 Y 显示 `---`。
- S2：`S2 K3H/K3L A0/A1 K0/K1`。
- S3：`S3 K4H/K4L A0/A1 K0/K1`。
- 不再存在 `U8H/U8L`、`64H/64L` 或 `60H/60L` 状态栏残留。

## 6. 当前摄像头噪点问题

已知现象是 MT9V034 图像噪点严重。本次只做代码审计，没有修改识别算法或 libraries。应优先检查曝光、增益、VCCCAM、GND、排线、光照和图像缓存链路，不应先改状态机。

当前可查到的摄像头参数：

| 参数 | 当前值 | 位置/说明 |
| --- | ---: | --- |
| `MT9V03X_AUTO_EXP_DEF` | `63` | `libraries/zf_device/zf_device_mt9v03x.h:78` |
| `MT9V03X_EXP_TIME_DEF` | `800` | `libraries/zf_device/zf_device_mt9v03x.h:82` |
| `MT9V03X_FPS_DEF` | `50` | `libraries/zf_device/zf_device_mt9v03x.h:84` |
| `MT9V03X_GAIN_DEF` | `64` | `libraries/zf_device/zf_device_mt9v03x.h:92` |

- 工程中没有名为 `MT9V03X_EXP` 的宏，实际曝光时间宏为 `MT9V03X_EXP_TIME_DEF`。
- `main_cm7_1.c:1460` 循环调用 `mt9v03x_init()` 直至成功，主文件没有后续曝光/增益覆盖。
- `libraries/zf_device/zf_device_mt9v03x.c:156-166` 将上述默认值放入配置表，并调用 `mt9v03x_sccb_set_config()`。
- 工程中没有精确名为 `mt9v03x_set_config` 的符号；实际是 `mt9v03x_sccb_set_config`，声明在 `libraries/zf_device/zf_device_config.h:44`。
- 头文件注释说明自动曝光范围为 0-63，0 为关闭；当前值为 63，因此从数值看很可能已开启自动曝光。同一行又写有“默认不开启”，注释与数值存在矛盾，需实机确认。
- 增益注释范围为 16-64，当前 `64` 是上限，代码可以明确看出增益配置较高，是噪点的高优先级嫌疑项。

## 7. 已知风险

1. IAR 配置文件已修改，但不应提交。
2. 未跟踪备份文件不应提交，也不应删除。
3. 新旧主板按键映射不要混用；当前代码是旧主板 P20_3/P20_2/P20_1/P20_0 版本。
4. P06_5 是摄像头 PCLK，不能拿来做普通 GPIO 测试。
5. P06_7 是 IMU INT2，不要随意修改。
6. `DEBUG_UART` 用于 DAP 图传，不能输出普通文本。
7. 松开按键不是 3.3V 时，先查 GPIO 配置、后续初始化覆盖和硬件，不要先改状态机。
8. 对象 overlay 已关，但状态栏 overlay 仍开；如果目标是严格纯图，当前状态不符合“全部 overlay 关闭”。
9. 显示去噪已开，当前 DAP 图传不是 raw sensor 画面，会影响对原始噪点的直观判断。
10. K3/K4 事件锁存路径使用 raw 电平而不是消抖后电平，实机需关注边沿抖动；本次不改按键逻辑。
11. 涉及按键 GPIO 修改前，必须先读取 `docs/debug_notes/KEY_GPIO_DESIGN_SKILL.md`。
12. 涉及按键 GPIO、按键映射、主板切换、按键状态机触发前，必须先读取 `docs/debug_notes/KEY_GPIO_DESIGN_SKILL.md`。

## 8. 下一步建议

1. 先用万用表或 DAP 变量确认旧主板 K3/K4/K5/K6 松开为高、按下为低，并确认没有后续初始化覆盖 P20 配置。
2. 在 S2 实测 K3/P20_3 是否进入 S3，同时观察状态栏 `K3H/L A0/1 K0/1`。
3. 在 S3 实测 K4/P20_2 是否回 S0，并确认雷区计数只增加 1。
4. 再对 MT9V034 噪点做受控排查：优先确认自动曝光实际状态和最大增益 64，然后检查 VCCCAM、GND、排线和光照。为了观察纯噪点，后续另行评估是否临时进入 raw test；本次不改开关。
5. 硬件与图像问题确认后，再整理 Git 范围；不纳入 IAR 自动文件和备份文件。

## 9. 调试文档 / 错误集现状

- `docs/debug_notes/P06_4_U8_GPIO_pullup_error_note.md`：已存在。
- `docs/debug_notes/MT9V034_camera_noise_error_note.md`：不存在。
- `docs/debug_notes/CURRENT_SMARTCAR_STATUS.md`：本次新建。
