# 旧主板按键测试可用版本检查点

## 1. 检查点说明

这是旧主板按键测试可用版本，用于防止后续修改后无法恢复。

## 2. 当前实测电压

- P20.3 松开约 3.34V，当前正常。
- P20.1 松开约 2.34V，能用但不是理想 3.3V，需要作为已知风险继续关注。
- 按键按下应接近 0V。
- 松开不是 3.3V 时，不要先改状态机，要先查 GPIO 和硬件。

## 3. 当前按键逻辑

- S2 -> S3 当前临时使用 P20_1 / 旧主板 S5；进入 S2 后必须先检测到松开，再按下才能锁存事件。
- S3 -> S0 使用 P20_2 / 旧主板 S4，成功切换时 `g_mine_area_count++`。
- 任意状态复位使用 P20_0 / 旧主板 S6，复位回 S0 时不清空 `g_mine_area_count`。
- P20_3 当前只在 S2 状态栏显示 `P3H/P3L`，不参与状态机跳转。
- P20_1 当前是临时 S2 -> S3 测试键，尚未恢复为默认 S5 强制下一状态功能；`ENABLE_FORCE_NEXT_KEY = 0U`。

## 4. 当前关键 GPIO 规则

所有按键必须满足：

```text
HSIOM = HSIOM_SEL_GPIO
driveMode = CY_GPIO_DM_PULLUP
outVal = 1U
input buffer on
低电平按下
```

按键引脚不能执行输出写入，不能配置为开漏输出、下拉、模拟或外设复用，也不能被后续初始化覆盖。

## 5. 当前图像链路

- 正常视觉模式每帧将 `mt9v03x_image` 复制到 `work_image`。
- 正常视觉模式每帧将 `mt9v03x_image` 复制到 `display_image`。
- `main_cm7_1.c` 不写入 `mt9v03x_image`。
- `VISION_OBJECT_OVERLAY_ENABLE = 0U`。
- `VISION_DISPLAY_DENOISE_ENABLE = 0U`。
- `DEBUG_UART` 只用于 Seekfree Assistant / DAP 自动图传，没有混发普通文本。

## 6. 当前全局开关

```text
CAMERA_MINIMAL_RAW_TEST = 0U
CAMERA_DIAG_TEST_PATTERN = 0U
VISION_RAW_IMAGE_TEST = 0U
VISION_STATUS_BAR_ENABLE = 1U
VISION_OBJECT_OVERLAY_ENABLE = 0U
VISION_DISPLAY_DENOISE_ENABLE = 0U
ENABLE_FORCE_NEXT_KEY = 0U
```

## 7. 已知风险

- P20.1 松开电压约 2.34V，不是理想 3.3V。
- IAR 配置文件可能被自动修改，但不应提交。
- 未跟踪备份文件不应提交。
- 后续修改按键前必须先读 `docs/debug_notes/KEY_GPIO_DESIGN_SKILL.md`。
- 后续修改摄像头噪点前必须先读 `docs/debug_notes/MT9V034_camera_noise_error_note.md`；该文件当前存在。

## 8. 恢复方法

先查看提交历史：

```text
git log --oneline
```

只恢复关键代码或本检查点文档：

```text
git checkout <本次提交哈希> -- example/user/main_cm7_1.c
git checkout <本次提交哈希> -- docs/debug_notes/STABLE_CHECKPOINT_OLD_BOARD_KEY_TEST_WORKING.md
```

如果要让整个工作区回到该提交：

```text
git reset --hard <本次提交哈希>
```

`git reset --hard` 会丢弃所有未提交修改，使用前必须确认工作区中没有需要保留的内容。
