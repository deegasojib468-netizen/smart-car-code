CYT4BB7 CM7_1 摄像头独立采集测试版
====================================

一、本版本做了什么
1. CM7_0 不再初始化摄像头，也不再占用 DEBUG UART，只保留空循环，后续用于电机控制。
2. CM7_1 负责：DEBUG UART、逐飞助手、MT9V03X 初始化、图像采集和图像发送。
3. 本阶段没有加入二值化、方框识别、双核识别结果通信。
4. 原始 main 文件已放在 example/user/original_backup 中。

二、打开工程
1. 保持压缩包内目录结构不变，不要单独移动 example 或 libraries。
2. 使用 IAR 9.40.1 打开：
   example/iar/cyt4bb7.eww
3. 工作区中应看到 cyt4bb7_cm_7_0 与 cyt4bb7_cm_7_1 两个工程。
4. 对两个工程分别执行 Project -> Clean，然后 Rebuild All。

三、下载与运行
1. 摄像头接主板摄像头接口，并使用主板正常供电；不要仅依靠下载器给摄像头供电。
2. 建议使用原工程已有的双核调试配置，同时下载/运行两个核心。
3. 打开逐飞助手，选择图像传输。
4. 连接 DEBUG UART 对应串口，波特率 115200。
5. 正常情况下应显示 188x120 灰度图。115200 发送完整图像较慢，两三秒一帧属于正常现象。

四、故障判断
1. CM7_1 的 LED 缓慢翻转：摄像头初始化失败。重点检查供电、排线方向和摄像头接口。
2. LED 不闪但逐飞助手没有图像：先检查串口号、115200 波特率和逐飞助手模式。
3. 仍无图像时，在 example/user/main_cm7_1.c 中将：
      #define CAMERA_DIAG_TEST_IMAGE (0)
   改为：
      #define CAMERA_DIAG_TEST_IMAGE (1)
   重新编译下载。
   - 能看到水平渐变图：串口发送正常，问题在摄像头采集或接线。
   - 仍看不到渐变图：问题在 CM7_1 DEBUG UART、下载方式或逐飞助手连接。
4. 若编译报错，请保存完整 Build 窗口文本，不要只截最后一行。

五、重要限制
1. CM7_0 与 CM7_1 不能同时调用 debug_init()，本测试只允许 CM7_1 使用 DEBUG UART。
2. 两个核心不能同时调用 mt9v03x_init()。
3. 本环境没有 IAR 编译器，因此文件已按现有工程和驱动接口完成静态检查，但仍需你在 IAR 中实际编译验证。

============================================================
V2 update:
This package now includes image statistics, Otsu threshold,
fixed-threshold binary conversion and key-triggered RAM snapshot.
See README_参数标定说明.txt.
============================================================
