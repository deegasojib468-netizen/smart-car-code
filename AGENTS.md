# Smart Car Project Rules

## 项目定位

这是一个智能车嵌入式工程，可能包含单片机代码、摄像头采集、图像处理、串口通信、核间通信、车辆控制、无线通信和参数调试内容。

本项目用于学习、调试和阶段性保存代码版本。修改前必须保证当前版本已经被 Git 保存，避免丢失可用代码。

## Codex 工作规则

### 1. 修改前必须先做版本检查

每次开始修改功能代码前，必须先执行：

```bash
git status
```

如果存在未提交修改，必须先询问用户是否提交，不允许直接覆盖。

如果用户要求“开始新功能”或“生成新版本”，必须先完成一次提交或确认当前工作区是干净的。

### 2. 每次重要修改都要写清楚版本说明

涉及以下内容时，必须更新 `docs/VERSION_LOG.md`：

- 摄像头采集
- 图像二值化
- 图像识别
- 串口传输
- 无线通信
- PID 控制
- 电机控制
- 核间通信
- 参数调节
- 工程结构调整

每次记录至少包含：

```text
版本号：
日期：
修改内容：
涉及文件：
验证结果：
已知问题：
下一步：
```

### 3. 不允许随意删除工程文件

不要删除以下类型文件，除非用户明确同意：

- `.c`
- `.h`
- `.s`
- `.uvprojx`
- `.uvoptx`
- `.ioc`
- `.ld`
- `.map`
- `.md`
- `.txt`
- 工程配置文件
- 用户原有说明文件

### 4. 修改代码必须说明位置

每次修改代码时，必须说明：

```text
修改了哪个文件
修改了哪个函数
为什么这样改
如何验证
```

### 5. 嵌入式工程注意事项

不得虚构：

- 引脚
- 宏定义
- 外设名称
- 函数名
- 文件路径
- 摄像头参数
- 串口号
- DMA 通道
- 中断函数
- 核间通信接口

如果资料不足，必须先列出缺少的文件或信息。

### 6. Git 提交规则

提交信息建议格式：

```text
v版本号: 简短说明
```

示例：

```text
v0.1-initial: 保存智能车工程初始版本
v0.2-camera-uart: 增加摄像头图像串口输出
v0.3-image-binary: 增加图像二值化处理
```

### 7. 推送规则

只有在用户要求上传或保存版本时，才执行：

```bash
git add .
git commit -m "提交说明"
git push
```

如果推送失败，不要重复乱试，必须报告错误信息。

## 推荐目录说明

```text
README.md              项目总说明
AGENTS.md              Codex 工作规则
docs/VERSION_LOG.md    版本记录
src/                   源代码目录
include/               头文件目录
project/               工程文件目录
tools/                 辅助脚本或工具
```

实际目录以用户当前工程为准，不要强行重构。

---

# General Coding Agent Instructions

## Operating Principles

Keep it simple. Simple is better than complex.
Assume the user is a principal engineer.
Make the smallest maintainable change that solves the actual request.
Prefer existing patterns over new abstractions.
Avoid broad refactors, speculative helpers, and clever architecture unless clearly justified.
Use judgment. Read enough surrounding code to understand the existing pattern, then avoid unnecessary exploration.
Optimize for correctness, speed, judgment, and token efficiency.
Correct the user when appropriate.
Prefer FAANG-level code quality: clear naming, strong types, simple control flow, minimal mutation, focused functions, pure functions/components where practical, and no unnecessary abstraction.

## Context Discipline

Protect context aggressively.

Answer the narrow question first. Inspect the smallest relevant file, symbol, route, component, diff, log, or test output.

Prefer targeted searches, focused file sections, nearby call sites, capped logs, and scoped validation. Avoid running validation commands like `npm run build`, `npm run test`, or `npm run lint` unless absolutely necessary. Use normal scoped commands like `rg`, with a byte cap when needed.

Avoid dumping full files, full logs, unrelated directories, broad repo searches, large diffs, or generated output after the relevant code is found.

Do not byte-cap instruction files, skill files, tool docs, or agent policy files. Read the whole relevant file unless it is unexpectedly huge.

## Command Output

Protect context usage. **Any command with unknown or potentially large output must be scoped and byte-capped.**

Byte-cap unknown or potentially large output. Line caps alone are unsafe because a single line can be huge.

```bash
COMMAND 2>&1 | head -c 4000
COMMAND 2>&1 | tail -c 4000
```

### Good Byte Capping Examples

```bash
rg -n -m 20 'functionName|ComponentName|routeName' src 2>&1 | head -c 200
bash -o pipefail -c 'npm run type-check 2>&1 | tail -c 500'
bash -o pipefail -c 'npm run test 2>&1 | tail -c 2000'
bash -o pipefail -c 'npm run build 2>&1 | tail -c 500'
rg -l "SEARCH_TERM" src 2>&1 | head -c 4000
```

Do not rely on `head -n`, `tail -n`, or `sed -n` as the only cap.

Scope before printing content: list files first, search specific paths, count matches when useful, and avoid reading generated, binary, minified, database, or huge JSON/JSONL files unless required.

Preserve exit codes when needed:

```bash
tmp="$(mktemp)"
COMMAND >"$tmp" 2>&1
status=$?
tail -c 5000 "$tmp"
rm -f "$tmp"
exit "$status"
```

Avoid unbounded `cat`, broad `rg`, `find`, `ls -R`, `git diff`, tests, builds, and `select *`.

If capped output is insufficient, narrow the command before increasing the cap.

## Code Changes

Prefer direct edits with the available patch tool.
Patch the narrow failing path first.
Avoid unrelated cleanup.
Do not add helpers, wrappers, maps, files, abstractions, or validation layers unless they clearly reduce complexity.

## Patterns to Avoid

Avoid single-use abstractions.

Prefer inline types and direct logic when a helper, wrapper, map, or named type is used only once.

Avoid wrapper functions that simply call another function.

## Validation

Match validation to risk.

Skip validation for low-risk changes and say so plainly.
Use the cheapest useful check for risky changes.
Do not run full test suites or full builds unless risk justifies it or the user asks.

## Subagents

Use subagents only when they save context, save time, or materially improve output quality.

For research, review, and exploration tasks, avoid confirmation bias. Do not pass a preferred conclusion. Ask the subagent to investigate, compare, or verify, and require evidence, tradeoffs, uncertainty, and better alternatives.

Prefer subagents for:

- documentation/API checks
- web research
- non-trivial copywriting/content generation

Avoid subagents for trivial work the main agent can finish faster.

When using a subagent, assign a narrow task and require:

- findings
- files inspected
- files changed, if any
- validation run, if any
- risks or uncertainty

You own final judgment and integration.

## Communication

Before editing, state the approach only for non-trivial tasks.

During complex work, keep updates short:

- what was found
- what changed
- what risk remains

After work, summarize:

- what changed
- files touched
- validation run, or why skipped
- remaining risk

Keep summaries short. Do not explain obvious edits.

Oververbosity:low
