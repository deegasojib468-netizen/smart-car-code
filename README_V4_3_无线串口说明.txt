CYT4BB7_CM7_1_Mine_Recognition_V4_3
功能：在 V4.2 白色空心方框识别基础上，将逐飞助手图像发送通道从 DEBUG_UART 改为无线串口模块。

一、硬件接口
根据 CYT4Bx 主板 V1.2.1 原理图，无线模块-串口接口 P7 使用：
- MCU TX：P4.1
- MCU RX：P4.0
- RTS：P22.6
- 供电：VCC5V_EXT 与 GND

如果使用逐飞无线串口模块，优先直接插主板“无线模块-串口”座子，不建议飞线。

二、代码修改点
修改文件：example/user/main_cm7_1.c
关键修改：
1. 新增 ASSISTANT_USE_WIRELESS_UART = 1。
2. 初始化 wireless_uart_init()。
3. seekfree_assistant_interface_init(SEEKFREE_ASSISTANT_WIRELESS_UART)。
4. 统计文字也改为通过 wireless_uart_send_buffer() 输出。

三、测试方法
1. 先编译 cyt4bb7_cm_7_1 - Debug。
2. 再编译 cyt4bb7_cm_7_0 - Debug。
3. 从 cyt4bb7_cm_7_0 下载运行。
4. 电脑端连接无线串口接收端，逐飞助手选择该 COM 口，波特率 115200。
5. 图像传输模式下按 S6/P20.0，发送当前保存图像。

四、注意事项
1. 115200 传完整 188x120 图像很慢，大概两秒左右一张，FPS 低是正常现象。
2. 无线图传只建议调试使用，正式比赛建议只传 valid、center_x、error_x、width、height 等识别结果。
3. 如果无线模块不亮，先检查 P7 接口供电 VCC5V_EXT 和 GND。
4. 如果能编译但无图像，先确认无线模块成对、波特率一致、电脑 COM 口选择正确。
