# X4 按键回归记录

**日期**：2026-10-18
**固件**：分支 `x4-c3`（`4b81841`，X4 冻结线；构建在独立 worktree 里做）
**方式**：真机按键操作 + 串口日志核对

## 结论：全部通过 ✅

| 操作 | 日志证据 | 结果 |
|---|---|---|
| 电源键唤醒 | `interactive session (25 s)` | ✅ |
| 行情页 上/下 | `crypto sort: 跌幅榜` → `crypto sort: 表序` | ✅ 排序切换 |
| 确认键 | `confirm: re-fetching` + 每轮 `7/7 coins`、`6 (Gate)` | ✅ 立刻重取 |
| 右键翻页到第 4 页 | `EAQI 38, PM2.5 12.2`（空气质量页取数） | ✅ |
| 返回键 | `deep sleep 30 min …` | ✅ 立刻睡（用户确认） |
| 电源键长按 2.5 s | `power key held: switching off` | ✅ 关机（用户确认） |
| 电池 / 行情 / 天气 | `3972 mV, 80%`、`BTC 81795 / ETH 2417.11 / XMR 592.86`、`19.7 C, code 3` | ✅ |

## 分支确认（关键）

烧入的固件必须来自 `x4-c3`，而不是 `main`。判定方法：下面这些**只存在于 main/PaperS3**
的日志在整段抓包里出现 **0 次**：

```
rtc ready          ← BM8563（PaperS3 才有）
layout scaled      ← 540x960 缩放画布
[settings]         ← 触屏设置界面
touch session      ← 触控会话（X4 是 interactive session）
```

另外两个直观标记：页码显示 `n/4`（`main` 是 `n/5`）、深睡日志写 `deep sleep 30 min`
（`main` 已改为 `deep sleep N s`）。

## 以后改 X4 的方式

```sh
git worktree add ../x4-weather-frozen x4-c3      # 一次性
cd ../x4-weather-frozen
PLATFORMIO_CORE_DIR=/Volumes/DataRaid0/work/.pio-core pio run -e x4weather
tools/flash_when_awake.sh /dev/cu.usbmodemXXXX x4weather
```

X4 冻结线**不含** 2026-10-17 起加入的 PaperS3 功能（新首页/猫/齿轮/关于页/RTC 准点/
手势轴向修正后逻辑坐标判定等）。这些功能全部包在 `#if FREEINK_DEVICE_PAPERS3` 里，
因此 `main` 上的 X4 构建渲染结果与冻结前**逐字节一致**（用 `cmp` 对渲染图验证过）。
