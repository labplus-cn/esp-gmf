# esp_capture 实现说明

以下文档概述了 `esp_capture` 组件的目录结构、核心概念和关键实现文件，便于开发者快速理解该模块的内部运作。

---

## 📁 目录结构

```
esp-gmf/packages/esp_capture/
├── include/                 ← 公共头文件
│   ├── esp_capture.h
│   ├── esp_capture_sink.h
│   ├── esp_capture_advance.h
│   └── …                         // 类型、path_mngr 接口等
├── src/                     ← 核心源码
│   ├── esp_capture.c          ← 主状态机
│   ├── esp_capture_sync.c     ← PTS/同步功能
│   ├── capture_muxer.c        ← 可选的复用器线程
│   ├── utils/…                ← msg_q、data_queue、share_q、thread 等
│   └── capture_path.h         ← 不透明的路径句柄与辅助函数
├── private_inc/             ← 内部头文件/OS 封装
└── impl/capture_gmf_path/   ← 基于 GMF 的音视频路径实现
    ├── include/*.h
    └── src/*.c
```

---

## 🔧 核心概念

| 概念 | 说明 | 关键文件/符号 |
|------|------|--------------|
| **Capture 句柄** | 由 `esp_capture_open` 返回的 opaque 指针 | `esp_capture_open()`、`capture_t`（在 `esp_capture.c`） |
| **路径（Path）** | 一个 capture 实例最多包含 3 条路径（音频、视频、复用器）。每条路径对应一个 `capture_path_t` 结构，通过 `capture_get_path_by_index()` 管理。 | `capture_path_t`（参见 `esp_capture.c` 文件顶部） |
| **Sink（输出端）** | 用户提供队列或回调以接收帧。配置信息保存在 `capture_path_t::sink_cfg` 中。 | `esp_capture_sink_cfg_t` 头文件，`prepare_*_share_queue()` 等函数 |
| **Share queue** | 一个轻量通用队列（`share_q`），用于将帧分发给应用、复用器等。释放回调把缓冲返回给来源。 | `src/utils/share_q.c`，`esp_capture.c` 中的使用 |
| **事件回调** | `esp_capture_event_cb_t` 在启动/停止、错误，及 GMF 管道构建完成时触发。 | `esp_capture_set_event_cb` / `capture_path_event_reached()` |
| **线程调度器** | 可选回调用于调整任务参数。 | `esp_capture_set_thread_scheduler()` |
| **同步** | 简单的时间基 PTS 跟踪，仅在 `sync_mode` 非 NONE 时启用。 | `esp_capture_sync.c` （所有 `esp_capture_sync_*` API） |
| **性能监测** | 在关键位置添加日志，当 `CONFIG_ESP_CAPTURE_ENABLE_PERF_MON` 打开时记录。 | `capture_perf_mon.*` |

---

## ⚙️ `esp_capture.c` – 主功能文件

这个约 1300 行的文件包含：

1. **初始化/销毁** – `esp_capture_open()`、`esp_capture_close()` 分配 `capture_t`、创建 sync handle 等。
2. **启动/停止** – `esp_capture_start()` 遍历所有路径，利用路径管理接口构建管道、创建共享队列、启动线程。
3. **帧传递** – 源接口调用的回调 `capture_frame_avail()` 将帧推到对应的 share_q 或直接丢弃。
4. **路径事件处理** – `capture_path_event_reached()` 标记路径错误/完成并通知用户。
5. **Sink 帮助函数** – `video_sink_get_q_data_ptr()`、`audio_sink_release_frame()` 等，供 share_q 使用。
6. **路径辅助** – 通过 `capture_path.h` 暴露的函数（`capture_path_get_sink_cfg`、`capture_path_release_share` 等），被复用器和 GMF 路径模块调用。

---

## 🧩 复用器支持（`capture_muxer.c`）

`capture_muxer_open()` 新建 `capture_muxer_path_t`。
专用线程 `muxer_thread()` 从消息队列读取帧，通过 esp‑muxer API 打包成 MP4/TS/FLV，并可将流式数据经 data queue 转发。

关键函数：

* `calc_muxer_cache_size()` – 根据格式/分辨率计算 RAM 缓存。
* `open_muxer()` / `prepare_muxer_stream()` – 配置流和 muxer。
* `muxer_data_reached()` – esp‑muxer 在直播输出时调用。

复用器配置作为 `esp_capture_sink_cfg_t` 的一部分， muxer 线程与 capture 同步启动/停止。

---

## 📐 路径管理（GMF 实现）

包内提供了两个 GMF 实现的 `esp_capture_path_mngr_if_t`：

* `impl/capture_gmf_path/src/gmf_capture_audio_pipeline.c`（音频）
* `impl/capture_gmf_path/src/gmf_capture_video_pipeline.c`（视频）
* 还有 `gmf_capture_auto_video_pipeline.c`（自动视频管线，含编码器/ISP 等）。

头 `impl/capture_gmf_path/include/capture_gmf_mngr.h` 定义配置结构及工厂函数：

```c
esp_capture_audio_path_mngr_if_t *esp_capture_new_gmf_audio_mngr(
    esp_capture_audio_path_mngr_cfg_t *cfg);
esp_capture_video_path_mngr_if_t *esp_capture_new_gmf_video_mngr(
    esp_capture_video_path_mngr_cfg_t *cfg);
```

每个管理器通过回调向 `esp_capture` 报告事件，例如管线构建完成。

---

## 🧠 工具 & OS 抽象

Supporting code 在 `src/utils` 和 `private_inc`：

* `msg_q.c` – 固定大小消息队列。
* `data_queue.c` – 复用器流式输出的循环缓冲。
* `capture_thread.c` / `capture_os.h` – FreeRTOS 线程、计时器、互斥锁的薄封装。
* `capture_mem.h` – 分配宏 (`capture_calloc`、`capture_free`)。

这些工具同样被其他 esp‑gmf 包复用。

---

## 📖 帧流走向概览

1. 调用 `esp_capture_start()` → 路径管理器建立 GMF 管线。
2. 源（比如基于 `esp32-camera` 的视频）通过路径管理接口注册的 `frame_avail` 回调。
3. `capture_frame_avail()` 将帧放入 `share_q`。
4. 应用从 `esp_capture_sink_cfg_t` 中配置的队列读取，或调用 `esp_capture_get_frame()`。
5. 处理完毕后，应用调用 `capture_path_release_share()`（实现于 `esp_capture.c`），帧缓冲返回给源。

---

## 📝 示例 & 测试

`esp-gmf/packages/esp_capture/examples` 有录制文件、直播、音频捕获等示例，可参考调用方式。

> ⚠️ **提示**：若需自定义音/视频处理，可在捕获开始前通过捕获事件拦截 `ESP_CAPTURE_EVENT_*_PIPELINE_BUILT`，在 GMF 管道内注册元素。

---

以上文档总结了 `esp_capture` 的实现细节。阅读这些文件可以帮助你深入理解和扩展该模块。