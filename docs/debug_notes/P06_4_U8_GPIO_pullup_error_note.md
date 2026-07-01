# P06_4 / U8 GPIO 输入上拉错误记录

## 1. 问题现象

- 新主板 U8 对应 `P06_4`（P6.4）。
- S2 状态栏曾持续显示 `U8L`，程序因此认为 U8 已经按下。
- 万用表曾测得 P06_4 对 GND 约 1.4V，之后又测得约 0.4V。
- 使用完整 GPIO 配置修正后，P06_4 松开时能够测到约 3.3V。

## 2. 正常电气逻辑

- U8 应配置为上拉输入，按键按下时接地。
- 松开 U8：P06_4 应接近 3.3V，程序显示 `U8H`。
- 按下 U8：P06_4 应接近 0V，程序显示 `U8L`。
- 1.4V 或 0.4V 属于异常中间电压或引脚被拉低状态，不能作为正常高电平。

## 3. 本次根因分析

当前代码通过 `configure_p6_pullup_diagnostics()` 对 P06_4 和对比引脚 P06_0 进行完整配置：

```c
cy_stc_gpio_pin_config_t pin_config = {0};

pin_config.outVal = 1U;
pin_config.driveMode = CY_GPIO_DM_PULLUP;
pin_config.hsiom = HSIOM_SEL_GPIO;
pin_config.intEdge = CY_GPIO_INTR_DISABLE;
pin_config.intMask = 0U;
pin_config.vtrip = CY_GPIO_VTRIP_CMOS;

(void)Cy_GPIO_Pin_Init(KEY_U8_PORT, KEY_U8_PORT_PIN, &pin_config);
(void)Cy_GPIO_Pin_Init(DIAG_P60_PORT, DIAG_P60_PORT_PIN, &pin_config);
```

根据当前工程实际代码，可以确认以下事实：

- 早期逐飞 `gpio_init(..., GPI, GPIO_HIGH, GPI_PULL_UP)` 在当前 `gpio_init()` 实现中把输入 drive mode 配置为 `CY_GPIO_DM_HIGHZ`，并没有启用电阻上拉。
- 后续只调用 `Cy_GPIO_SetDrivemode(..., CY_GPIO_DM_PULLUP)` 时，没有在同一次完整初始化中明确保证 OUT/Data Register 位为 1。
- 修正后改用完整 `Cy_GPIO_Pin_Init()`，明确设置 `outVal = 1U`。
- `driveMode` 明确使用 `CY_GPIO_DM_PULLUP`。当前 PDL 将其定义为“Resistive Pull-Up, Input buffer on”。
- 没有使用 `CY_GPIO_DM_PULLUP_IN_OFF`，因此输入缓冲保持开启。
- HSIOM 明确设置为 `HSIOM_SEL_GPIO`，避免引脚继续处于外设复用功能。
- `vtrip` 使用 `CY_GPIO_VTRIP_CMOS`。
- 没有把 P06_4 配置成输出、开漏输出、下拉、模拟输入或外设功能。
- 当前诊断代码没有对 P06_4 调用 `gpio_set`、`gpio_write` 或 `gpio_toggle`。
- 摄像头初始化完成后会再次配置 P06_4/P06_0。
- 主循环中约每 1000 次循环临时重配一次，用于排除后续初始化覆盖配置的可能性。
- 当前 PDL 中未找到 `Cy_GPIO_SetPullupResistance` 或等价的独立上拉阻值配置 API，因此本次依靠 `CY_GPIO_DM_PULLUP` 启用芯片支持的内部电阻上拉。

这次 P06_4 能稳定到 3.3V，最关键的变化是：完整 `Cy_GPIO_Pin_Init()` 同时保证 `outVal = 1U`、`CY_GPIO_DM_PULLUP`、`HSIOM_SEL_GPIO` 和输入缓冲开启。摄像头初始化后再次配置及周期性重配，则排除了配置被后续流程覆盖的影响。

之前出现 1.4V/0.4V，最可能的代码原因是早期封装实际使用了 `CY_GPIO_DM_HIGHZ`，没有真正启用电阻上拉；另一个可能原因是只修改 drive mode 时没有明确把 OUT/Data Register 位设置为 1。是否确实发生过后续初始化覆盖，仅凭现象不能单独证明，因此只能列为待排除因素。若完整配置后电压仍异常，还应检查按键板、排线、焊点、测量点及引脚是否存在硬件短路或漏电。

引脚占用也必须注意：

- `P06_5` 是摄像头 `MT9V03X_PCLK_PIN`，禁止拿来做普通 GPIO 输入上拉对比。
- `P06_7` 被定义为 `IMU660RC_INT2_PIN`，禁止在使用 IMU 时随意改为普通 GPIO。

## 4. 关键经验

- 不能只说“配置上拉”，必须同时确认 driveMode、HSIOM、outVal 和输入缓冲状态。
- TRAVEO / Infineon GPIO 的 Resistive Pull-Up 需要 OUT/Data Register 位为 1。
- GPIO 输入必须确认 input buffer on，不能误用 input buffer off 的上拉模式。
- 被外设复用的引脚不能当作普通 GPIO 测试。
- 同一 Port 上存在摄像头、IMU 等功能时，应检查后续初始化是否重新配置了端口或引脚。
- P06_5 是摄像头 PCLK，禁止拿来做普通 GPIO 输入上拉对比。
- P06_7 是 IMU INT2，禁止随便改动。
- DEBUG_UART 用于 DAP 图传，不能混发普通文本。
- 周期性重配仅用于定位问题；确认没有覆盖后，应评估是否删除，避免长期保留诊断代码。

## 5. 以后排查按键低电平误判的步骤

1. 先确认原理图：按键是否上拉、按下是否接地。
2. 用万用表测量 3.3V 电源是否正常。
3. 测量按键引脚松开和按下时的电压。
4. 如果松开时不是 3.3V，先不要修改状态机或消抖算法。
5. 检查 HSIOM 是否为普通 GPIO。
6. 检查 driveMode 是否为 `CY_GPIO_DM_PULLUP`。
7. 检查是否误用 `CY_GPIO_DM_PULLUP_IN_OFF`。
8. 检查 `outVal` / OUT 位是否为 `1U`。
9. 检查是否被配置成输出、开漏、下拉、模拟或外设复用。
10. 检查配置是否被后续初始化覆盖。
11. 找确认未占用的引脚做对比，不要碰摄像头、IMU、UART、PWM 或编码器引脚。
12. 必要时只在确认未占用、不会被外部接地的测试引脚上做强推输出高测试，验证测量孔是否正确；不要对按键或外设信号脚直接这样测试。
13. 电气状态确认正常后，再恢复按键触发状态机。

## 6. Codex 防错提示词模板

```text
只处理按键 GPIO 电气配置，不要先修改状态机或图像算法。

修改前必须：
1. 查原理图、引脚宏和所有引脚复用。
2. 确认按键引脚没有被摄像头、IMU、UART、PWM、编码器等外设占用。
3. 确认按键是输入上拉、低电平按下。
4. 在 Infineon PDL 下确认 HSIOM_SEL_GPIO、CY_GPIO_DM_PULLUP、outVal=1U、input buffer on。
5. 禁止使用 CY_GPIO_DM_PULLUP_IN_OFF、输出、开漏、下拉、模拟或外设复用模式。
6. 禁止对按键引脚调用 gpio_set、gpio_write、gpio_toggle。
7. 不能修改 P06_5 / MT9V03X_PCLK_PIN。
8. 不能修改 P06_7 / IMU660RC_INT2_PIN。
9. DEBUG_UART 只用于 DAP 图传，不能输出普通文本。
10. 不改状态机、不改图像算法，先完成松开高电平、按下低电平的电气验证。

修改后必须列出实际 API、driveMode、HSIOM、outVal、输入缓冲状态、读取函数及 Git 状态。
```
