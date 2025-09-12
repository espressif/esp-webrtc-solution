# AV Render Test Application

This is a comprehensive audio-video render test application for **ESP32P4** and **ESP32S3** platforms, showcasing real-time media rendering capabilities.

## Overview

The application captures audio and video from hardware sources (camera and microphone) and renders them through an LCD display and I2S audio output, forming a complete end-to-end media pipeline test.

## Architecture

### System Components

The test application is divided into two main subsystems:

1. **Capture System** — Acquires media input (camera, microphone)
2. **Render System** — Handles media output (focus of this documentation)

### Render System Components

```c
typedef struct {
    audio_render_handle_t audio_render;  // I2S audio renderer
    video_render_handle_t video_render;  // LCD video renderer
    av_render_handle_t    player;        // Combined AV renderer
} player_system_t;
```

- **Audio Render**: I2S-based audio output, fixed clock mode, 6KB buffer
- **Video Render**: LCD-based video output, 500KB buffer, supports MJPEG
- **Player (av_render)**: Combines audio and video renderers, manages sync and buffer usage

## Key Functions

### `build_player_system()`

Initializes the render system components:

```c
static int build_player_system(void)
{
    // Create I2S audio renderer
    i2s_render_cfg_t i2s_cfg = {
        .fixed_clock = true,
        .play_handle = get_playback_handle(),
    };
    player_sys.audio_render = av_render_alloc_i2s_render(&i2s_cfg);

    // Create LCD video renderer
    lcd_render_cfg_t lcd_cfg = {
        .lcd_handle = board_get_lcd_handle(),
    };
    player_sys.video_render = av_render_alloc_lcd_render(&lcd_cfg);

    // Create combined AV renderer
    av_render_cfg_t render_cfg = {
        .audio_render = player_sys.audio_render,
        .video_render = player_sys.video_render,
        .audio_raw_fifo_size = 4096,
        .audio_render_fifo_size = 6 * 1024,
        .video_raw_fifo_size = 500 * 1024,
        .allow_drop_data = false,
    };
    player_sys.player = av_render_open(&render_cfg);
}
```

### `test_capture_to_player()`

Demonstrates capture-to-render pipeline:

- **Audio Flow**:
  G.711A frames (8kHz, mono) → `av_render_add_audio_data()` → I2S speakers/headphones

- **Video Flow**:
  MJPEG frames → `av_render_add_video_data()` → LCD display

## Render Configuration

### Audio
- Codec: G.711A
- Sample Rate: 8kHz
- Channels: Mono
- Buffers: 4KB raw + 6KB render FIFO

### Video
- Codec: MJPEG
- Resolution: Configurable (`VIDEO_WIDTH`, `VIDEO_HEIGHT`)
- Frame Rate: Configurable (`VIDEO_FPS`)
- Buffers: 500KB raw FIFO

## Performance Features

### Thread Scheduling

```c
static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    if (strcmp(thread_name, "venc_0") == 0) {
        schedule_cfg->priority = 10;
        #if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024;
        #endif
    }
    else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;  // Higher priority for audio
    }
}
```

- Audio prioritized for low latency
- Video allocated larger stack for processing

### Buffer Management
- **Audio**: Dual-FIFO (raw + render) prevents dropouts
- **Video**: 500KB buffer absorbs MJPEG frame variations
- **Drop Protection**: `allow_drop_data = false` guarantees frame integrity

## Supported Platforms

### ESP32P4
- Video Input: V4L2 (`/dev/video0`)
- Camera: MIPI CSI / DVP
- Processing: Hardware-accelerated

### ESP32S3
- Video Input: DVP
- Camera: DVP-based
- Processing: Software-only

## Usage

1. **Initialization**: `media_sys_buildup()`
2. **Run Test**: `test_capture_to_player()`
3. **Continuous Operation**: real-time capture → decode → render

## Quality Settings

Edit `settings.h` to adjust playback quality:

- `VIDEO_WIDTH`
- `VIDEO_HEIGHT`
- `VIDEO_FPS`

## Dependencies

- `av_render` — Core rendering library
- `esp_capture` — Capture library
- `codec_board` — Board-specific codec support
- `media_lib_sal` — Media library adaptation layer
