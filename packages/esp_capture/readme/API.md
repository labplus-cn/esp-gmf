# esp_capture API 文档

本文件列出 `esp-gmf/packages/esp_capture` 的对外接口、结构和常量，以便开发者参考。
接口分为基础操作、输出（sink）管理、进阶控制等部分。

---

## 通用类型和错误

* **`esp_capture_handle_t`** – 捕获系统的 opaque 句柄，调用 `esp_capture_open` 后返回。后续对该系统的任何操作都必须传入此句柄。
* **`esp_capture_err_t`** – 错误码枚举，定义在 `esp_capture_types.h`。常见值：
  * `ESP_CAPTURE_ERR_OK` 成功。
  * `ESP_CAPTURE_ERR_INVALID_ARG` 参数错误。
  * `ESP_CAPTURE_ERR_NO_MEM` 内存不足。
  * `ESP_CAPTURE_ERR_NO_RESOURCES` 资源不足。
  * `ESP_CAPTURE_ERR_NOT_SUPPORTED` 功能未实现或状态不允许。
  * 其他值请参考头文件。
* **`esp_capture_stream_type_t`** – 流类型标识：
  * `ESP_CAPTURE_STREAM_TYPE_AUDIO` 音频流。
  * `ESP_CAPTURE_STREAM_TYPE_VIDEO` 视频流。
* **`esp_capture_sync_mode_t`** – 同步模式，决定如何计算和跟踪 PTS。

---

## 事件与回调

```c
typedef enum {
    ESP_CAPTURE_EVENT_NONE = 0,
    ESP_CAPTURE_EVENT_STARTED = 1,
    ESP_CAPTURE_EVENT_STOPPED = 2,
    ESP_CAPTURE_EVENT_ERROR = 3,
    ESP_CAPTURE_EVENT_AUDIO_PIPELINE_BUILT = 4,
    ESP_CAPTURE_EVENT_VIDEO_PIPELINE_BUILT = 5,
} esp_capture_event_t;

typedef esp_capture_err_t (*esp_capture_event_cb_t)(esp_capture_event_t event, void *ctx);
```

* **`ESP_CAPTURE_EVENT_STARTED`** – 捕获系统已成功启动。
* **`ESP_CAPTURE_EVENT_STOPPED`** – 捕获系统已停止。
* **`ESP_CAPTURE_EVENT_ERROR`** – 发生错误；调用者可通过上下文查明原因。
* **`ESP_CAPTURE_EVENT_AUDIO_PIPELINE_BUILT`** / **`VIDEO_PIPELINE_BUILT`** – GMF 路径建立好音/视频处理管线，用户可在此时添加或配置元素。

回调通过 `esp_capture_set_event_cb()` 注册，参数：`event` 指示事件类型，`ctx` 为用户提供的上下文指针。返回值可用来影响后续行为（例如错误中断）。

---

## 基础 API

下面的函数构成了 `esp_capture` 的核心生命周期和配置接口。

### `esp_capture_set_thread_scheduler`
```c
esp_capture_err_t esp_capture_set_thread_scheduler(esp_capture_thread_scheduler_cb_t cb);
```
将用户提供的调度回调注册到捕获系统。该回调在每个内部线程创建前调用一次，允许应用调整堆栈大小、优先级、运行核心等参数。应在 `esp_capture_start` 之前调用，否则设置无效。

### `esp_capture_open`
```c
esp_capture_err_t esp_capture_open(esp_capture_cfg_t *cfg, esp_capture_handle_t *capture);
```
创建并初始化捕获系统实例。
- `cfg`：捕获配置，包括同步模式及音/视频源接口。
- `capture`：输出参数，返回系统句柄。

可能返回的错误：`INVALID_ARG`（空指针），`NO_MEM`，`NO_RESOURCES`。

### `esp_capture_set_event_cb`
```c
esp_capture_err_t esp_capture_set_event_cb(esp_capture_handle_t capture, esp_capture_event_cb_t cb, void *ctx);
```
为指定的捕获句柄设置事件回调。`cb` 不可为空。

### `esp_capture_start`
```c
esp_capture_err_t esp_capture_start(esp_capture_handle_t capture);
```
启动捕获流程。对所有已配置的 sink、muxer、overlay 等进行初始化并创建内部线程。如果已启动，将返回 `INVALID_STATE`。

### `esp_capture_stop`
```c
esp_capture_err_t esp_capture_stop(esp_capture_handle_t capture);
```
停止正在运行的捕获。所有路径被通知并终止，资源（队列、线程）被释放。可在停止后再次 `start`。

### `esp_capture_close`
```c
esp_capture_err_t esp_capture_close(esp_capture_handle_t capture);
```
销毁捕获实例，释放所有关联资源，包括注册的元素和路径。调用后句柄失效。

### `esp_capture_enable_perf_monitor`
```c
void esp_capture_enable_perf_monitor(bool enable);
```
启用或禁用性能监测模块，仅用于调试。 当 `enable` 为 `false` 时会打印收集到的数据。

### 配置结构

```c
typedef struct {
    esp_capture_sync_mode_t sync_mode;       /*!< 同步方案 */
    esp_capture_audio_src_if_t *audio_src;   /*!< 音频源接口 */
    esp_capture_video_src_if_t *video_src;   /*!< 视频源接口 */
} esp_capture_cfg_t;
```

| 字段 | 说明 |
|------|------|
| `sync_mode` | 同步模式，决定如何计算 PTS，见 `esp_capture_sync_mode_t` |
| `audio_src`/`video_src` | 指向实现了源接口的结构体，用于从硬件或其他组件获取帧 |

线程调度类型：

```c
typedef struct {
    uint32_t stack_size;        /*!< 堆栈大小（字节）*/
    uint8_t priority;           /*!< 优先级 */
    uint8_t core_id : 4;        /*!< 执行核心 */
    uint8_t stack_in_ext : 1;   /*!< 是否放在外部 RAM */
} esp_capture_thread_schedule_cfg_t;

typedef void (*esp_capture_thread_scheduler_cb_t)(const char *name, esp_capture_thread_schedule_cfg_t *schedule_cfg);
```

调度回调接收线程名称并填充上述结构。

---

---

## Sink（输出）API

捕获系统通过“sink”将音视频帧交给用户或者内部模块（如复用器）。同一个 capture 可以包含多个 sink，每个 sink 可以独立配置音/视频参数。

### `esp_capture_sink_setup`
```c
esp_capture_err_t esp_capture_sink_setup(esp_capture_handle_t capture,
                                         uint8_t sink_idx,
                                         esp_capture_sink_cfg_t *sink_info,
                                         esp_capture_sink_handle_t *sink_handle);
```
配置或添加一个新的 sink。
- `capture`：捕获句柄。
- `sink_idx`：0‑based 索引；如果已存在则返回该 sink。
- `sink_info`：包含 `audio_info` 和/或 `video_info`，设置采集的格式、分辨率等。
- `sink_handle`：返回该 sink 的句柄。

错误码：`INVALID_ARG`(空指针)、`NO_MEM`、`INVALID_STATE`(索引已存在)、`NOT_SUPPORTED`(路径管理器不提供接口)。

### `esp_capture_sink_add_muxer`
```c
esp_capture_err_t esp_capture_sink_add_muxer(esp_capture_sink_handle_t sink,
                                             esp_capture_muxer_cfg_t *muxer_cfg);
```
给 sink 附加复用器。必须在 `esp_capture_start` 之前调用。
- `muxer_cfg`：复用器配置，包含 `base_config` 和 `muxer_mask`。

### `esp_capture_sink_add_overlay`
```c
esp_capture_err_t esp_capture_sink_add_overlay(esp_capture_sink_handle_t sink,
                                              esp_capture_overlay_if_t *overlay);
```
设置视频叠加接口。可以在运行时启用/禁用。

### 启用/禁用控制

* `esp_capture_sink_enable_muxer(sink, enable)` 开关复用功能。
* `esp_capture_sink_enable_overlay(sink, enable)` 开关叠加。
* `esp_capture_sink_enable(sink, run_type)` 设置运行模式：
  * `DISABLE` 停用 sink。
  * `ALWAYS` 持续运行。
  * `ONESHOT` 运行一次（例如抓图）。

这些函数均支持在 capture 启动后调用。

### 输出流控制

* `esp_capture_sink_disable_stream(sink, stream_type)` 在启动前禁止某一路流的输出。
  一旦禁用，必须重新配置 sink 才能恢复。
* `esp_capture_sink_set_bitrate(h, stream_type, bitrate)` 动态设置比特率（编码器路径支持时生效）。

### 数据获取

```c
esp_capture_err_t esp_capture_sink_acquire_frame(esp_capture_sink_handle_t sink,
                                                 esp_capture_stream_frame_t *frame,
                                                 bool no_wait);

esp_capture_err_t esp_capture_sink_release_frame(esp_capture_sink_handle_t sink,
                                                 esp_capture_stream_frame_t *frame);
```

- `frame` 由捕获系统填充，包含 `data`、`size`、`pts` 等。
- `no_wait` 为真时如果队列空则立即返回 `ESP_CAPTURE_ERR_NOT_SUPPORTED`。
- 释放时必须使用同一 sink 并确保 `stream_type` 匹配，否则返回 `INVALID_ARG` 或 `NOT_SUPPORTED`。

### 相关结构和枚举

```c
// sink handle
typedef void *esp_capture_sink_handle_t;

// sink 配置
typedef struct {
    esp_capture_audio_info_t  audio_info;
    esp_capture_video_info_t  video_info;
} esp_capture_sink_cfg_t;

// 运行模式
typedef enum {
    ESP_CAPTURE_RUN_MODE_DISABLE = 0,
    ESP_CAPTURE_RUN_MODE_ALWAYS  = 1,
    ESP_CAPTURE_RUN_MODE_ONESHOT = 2
} esp_capture_run_mode_t;

// 复用掩码
typedef enum {
    ESP_CAPTURE_MUXER_MASK_ALL   = 0,
    ESP_CAPTURE_MUXER_MASK_AUDIO = 1,
    ESP_CAPTURE_MUXER_MASK_VIDEO = 2,
} esp_capture_muxer_mask_t;

// 复用器配置
typedef struct {
    esp_muxer_config_t       *base_config;
    uint32_t                  cfg_size;
    esp_capture_muxer_mask_t  muxer_mask;
} esp_capture_muxer_cfg_t;
```

---

## 进阶 API

For users who need to tweak internal GMF pipelines or supply custom path managers, the advanced API exposes element registration and manual pipeline building.

### `esp_capture_register_element`
```c
esp_capture_err_t esp_capture_register_element(esp_capture_handle_t capture,
                                               esp_capture_stream_type_t stream_type,
                                               esp_gmf_element_handle_t element);
```
把一个自定义 GMF 元素加入内部池，后续构建管线时可引用。
- `capture`：有效的捕获句柄，系统必须尚未启动。
- `stream_type`：指定音频或视频元素池。
- `element`：待注册的 GMF 元素句柄。

返回 `INVALID_STATE` 如果已经启动或元素池已冻结。
注册后捕获系统承担元素销毁工作，用户不需要手动释放。

### `esp_capture_sink_build_pipeline`
```c
esp_capture_err_t esp_capture_sink_build_pipeline(esp_capture_sink_handle_t sink,
                                                  esp_capture_stream_type_t stream_type,
                                                  const char **element_tags,
                                                  uint8_t element_num);
```
为指定 sink 给定顺序的元素标签列表构建处理管线。
- `element_tags`：元素名称数组，必须先通过注册或默认库存在。
- `element_num`：标签数量。

该函数必须在 `esp_capture_start` 前且仅调用一次。

### `esp_capture_sink_get_element_by_tag`
```c
esp_capture_err_t esp_capture_sink_get_element_by_tag(esp_capture_sink_handle_t sink,
                                                      esp_capture_stream_type_t stream_type,
                                                      const char *element_tag,
                                                      esp_gmf_element_handle_t *element);
```
获取一个处理元素的句柄用于直接配置，比如调整编码参数。

### `esp_capture_advance_open`
```c
esp_capture_err_t esp_capture_advance_open(esp_capture_advance_cfg_t *cfg,
                                          esp_capture_handle_t *capture);
```
以高级模式创建捕获实例。
- `cfg`：包含自定义音视频路径管理器的接口指针。
- `capture`：返回实例句柄。

高级模式允许用户提供完整的 `esp_capture_path_mngr_if_t` 实现，以及对应的 `esp_capture_pipeline_builder_if_t`，用于构建任意 GMF 管线。

### 高级配置结构

```c
typedef struct {
    esp_capture_sync_mode_t sync_mode;   /*!< 同步模式 */
    esp_capture_audio_path_mngr_if_t *audio_path; /*!< 自定义音频路径管理接口，或 NULL 使用默认 */
    esp_capture_video_path_mngr_if_t *video_path; /*!< 自定义视频路径管理接口，或 NULL 使用默认 */
} esp_capture_advance_cfg_t;
```

用户在实现自己的路径管理器时可调用 `esp_capture_new_gmf_audio_mngr` / `esp_capture_new_gmf_video_mngr` 来复用现有 GMF 代码。

---

## 说明与使用提示

* 所有 API 在捕获启动前设置路径/输出，启动后只能改变少数状态（如启用/禁用 overlay、sink）。
* 几乎所有函数返回 `ESP_CAPTURE_ERR_INVALID_ARG` 用于参数检查；`ESP_CAPTURE_ERR_NOT_SUPPORTED` 表示未在当前路径实现中提供该功能。
* `esp_capture_start/stop/close` 管理整个 capture 的生命周期。

---

以上即为 `esp_capture` 的公开 API 汇总。更多细节请参阅对应头文件和示例代码。