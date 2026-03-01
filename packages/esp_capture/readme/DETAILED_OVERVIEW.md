# esp_capture 包详细说明

此文档对 `esp-gmf/packages/esp_capture` 下的主要源文件和头文件进行注释解释，帮助开发者快速了解各部分功能。

---

## include/

### esp_capture.h
包含公共接口定义：
* `esp_capture_handle_t` 句柄
* 事件类型 `esp_capture_event_t` 与回调 `esp_capture_event_cb_t`
* 配置结构：`esp_capture_cfg_t`、线程调度器相关类型
* API 原型：`esp_capture_open`、`esp_capture_start`、`esp_capture_stop`、`esp_capture_close`、`esp_capture_enable_perf_monitor`。

### esp_capture_sink.h
输出端（sink）相关配置：
* `esp_capture_sink_cfg_t` 结构，包括 audio/video/muxer 设置、队列、回调等
* `esp_capture_muxer_cfg_t` 复用器子结构
* 其他辅助类型。

### esp_capture_advance.h
进阶功能 API，比如获取帧、控制路径、设置多路复用、调试接口。

另外还有若干 `esp_capture_types.h`、`esp_capture_path_mngr.h` 等文件定义基础类型与路径管理接口。

---

## src/

### esp_capture.c
核心实现，约 1300 行。地址所有状态管理、路径和队列控制。
* 结构 `capture_t` 和 `capture_path_t` 定义
* 启动/停止逻辑
* `capture_frame_avail()` 处理来自源的帧
* `capture_path_event_reached()` 处理路径事件
* 共享队列创建/配置 (`prepare_audio_share_queue`/`prepare_video_share_queue`)
* API 实现：open/close/start/stop/set_event_cb 等
* 通用 helpers：`capture_get_path_by_index`、`capture_path_release_share` 等。

### esp_capture_sync.c
同步模块，提供：
* `esp_capture_sync_create`/`destroy` 
* `esp_capture_sync_audio_update` 更新音频 PTS
* `esp_capture_sync_on`/`off` 开关
* `esp_capture_sync_get_current` 获取当前参考点

### capture_muxer.c
负责将音视频帧写到 MP4/TS/FLV，并支持流式输出。
* 结构 `capture_muxer_path_t`
* `capture_muxer_open`/`close` 等生命周期函数
* muxer 线程 `muxer_thread` 将收到的 frame 发送到 esp_muxer
* helper 函数：`calc_muxer_cache_size`、`open_muxer`、`prepare_muxer_stream`。

### utils/ 子目录
提供通用工具：
* `msg_q.c` – 简单消息队列
* `data_queue.c` – 数据缓冲队列
* `share_q.c` – 帧共享队列
* `capture_thread.c` – 线程创建销毁封装
* `capture_perf_mon.c` – 性能监控辅助

每个对应一个 `.h` 抽象在 `private_inc/` 中。

### capture_path.h
对 `capture_path_t *` 的不透明别名与几条导出的辅助函数:
```c
typedef struct capture_path_t *capture_path_handle_t;
const esp_capture_sink_cfg_t *capture_path_get_sink_cfg(capture_path_handle_t path);
uint8_t capture_path_get_path_type(capture_path_handle_t path);
esp_capture_err_t capture_path_release_share(capture_path_handle_t path,
                                            esp_capture_stream_frame_t *frame);
```
这些由 `esp_capture.c` 实现，供 muxer 和路径实现调用。

---

## impl/capture_gmf_path/

GMF 框架下的捕获路径实现，分音/视频两类：
* `gmf_capture_audio_pipeline.c` — 负责构建音频 GMF 管线、处理帧和错误，上报事件。
* `gmf_capture_video_pipeline.c` — 视频管线，类似逻辑。
* `gmf_capture_auto_video_pipeline.c` — 更复杂的自动视频构建，带编码/ISP/分辨率调整等。

头文件 `capture_gmf_mngr.h` 定义了两种管理器的配置结构和构造函数。
管理器通过 `esp_capture_path_mngr_if_t` 接口与 esp_capture 核心交互。

---

## private_inc/

包含内部的操作系统抽象和辅助宏：
* `capture_os.h` – FreeRTOS 包装（任务、信号量、计时器）
* `capture_mem.h` – 分配与零初始化宏
* `capture_utils.h` – 错误检查、日志宏
* `capture_perf_mon.h` – 性能监测宏
* `esp_capture_sync.h` – 同步资料头

这些文件只在包内部使用。

---

## README.md

包自带的 README 提供总体介绍和用例链接，并在根 README 中有引用。示例代码放在 `examples/` 下。

---

以上注释覆盖了`esp_capture`包的主要组成部分，更多细节请直接打开相应源文件查看具体实现。