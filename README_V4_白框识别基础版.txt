CYT4BB7 CM7_1 白色空心方框识别基础版 V4

一、版本作用
本版本在 V3 脱机拍照保存基础上增加“白色空心方框识别”。
仍然不控制电机，只在 CM7_1 中完成：
1. 从 Work Flash 读取已保存的 188x120 灰度图；
2. 根据大津阈值计算识别阈值；
3. 搜索白色空心方框候选区域；
4. 判断亮边框 + 暗内部；
5. 输出 center、error、width、height 等识别结果；
6. 在逐飞助手图像上用黑框和白色中心十字标出识别结果。

二、按键功能
S3 / P20.3：拍照并保存下一帧到 Work Flash。
S4 / P20.2：切换下一张已保存图片。
S5 / P20.1：切换原图 / 二值图。
S6 / P20.0：识别当前选中图片，并发送到逐飞助手。

三、测试方法
1. 打开 example/iar/cyt4bb7.eww。
2. 先编译 cyt4bb7_cm_7_1 - Debug。
3. 再编译 cyt4bb7_cm_7_0 - Debug。
4. 从 cyt4bb7_cm_7_0 下载运行。
5. 按 S6 显示当前图片。
6. 如果识别到白框，图像上会出现黑色矩形框和白色中心十字。
7. 切到逐飞助手“串口助手”可以看到 target valid、center、error、size 等参数。

四、重要参数位置
所有基础识别参数都在 example/user/main_cm7_1.c 顶部：
RECOG_ROI_X_MIN / RECOG_ROI_X_MAX：识别横向范围。
RECOG_ROI_Y_MIN / RECOG_ROI_Y_MAX：识别纵向范围。
RECOG_OTSU_OFFSET：在大津阈值基础上增加的偏移量。
RECOG_THRESHOLD_MIN：识别阈值下限。
RECOG_ROW_MIN_COUNT：横向白边候选阈值。
RECOG_COL_MIN_COUNT：纵向白边候选阈值。
RECOG_MIN_BORDER_RATIO：方框边框白色比例要求。
RECOG_MAX_INNER_RATIO：方框内部白色比例上限。

五、当前限制
这是基础版，不是最终比赛算法。
它主要用于验证“能否从真实图像中找到白色空心方框”。
如果出现误识别，需要根据串口输出和截图继续调参数。
暂时没有双核通信，也没有电机控制。

六、看结果时重点记录
请把以下内容截图或复制给我：
1. 逐飞助手图像，是否画出了识别框；
2. 串口输出的 target valid；
3. center=(x,y)；
4. error=(x,y)；
5. size=宽x高；
6. border 和 inner。
