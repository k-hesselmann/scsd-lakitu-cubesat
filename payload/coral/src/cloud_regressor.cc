// Cloud cover regression firmware for Coral Dev Micro.
// Derived from the coralmicro classify_images example (Apache 2.0, Google LLC).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "libs/base/check.h"
#include "libs/base/filesystem.h"
#include "libs/base/gpio.h"
#include "libs/base/led.h"
#include "libs/base/pwm.h"
#include "libs/base/reset.h"
#include "libs/camera/camera.h"
#include "libs/nxp/rt1176-sdk/clock_config.h"
#include "libs/rpc/rpc_http_server.h"
#include "libs/rpc/rpc_utils.h"
#include "libs/tensorflow/utils.h"
#include "libs/tpu/edgetpu_manager.h"
#include "libs/tpu/edgetpu_op.h"
#include "third_party/freertos_kernel/include/FreeRTOS.h"
#include "third_party/freertos_kernel/include/task.h"
#include "third_party/nxp/rt1176-sdk/devices/MIMXRT1176/drivers/fsl_clock.h"
#include "third_party/nxp/rt1176-sdk/devices/MIMXRT1176/drivers/fsl_iomuxc.h"
#include "third_party/nxp/rt1176-sdk/devices/MIMXRT1176/drivers/fsl_lpuart.h"
#include "third_party/tflite-micro/tensorflow/lite/micro/micro_error_reporter.h"
#include "third_party/tflite-micro/tensorflow/lite/micro/micro_interpreter.h"
#include "third_party/tflite-micro/tensorflow/lite/micro/micro_mutable_op_resolver.h"

// Compile-time switch for bench-only debug tooling: the last-image / burst RPC
// endpoints and the button-hold burst-capture ring. Flight builds leave this at
// 0 so none of it is compiled in (saves the ~400 KB burst ring and trims the
// main loop); bench builds pass -DCLOUD_DEBUG=1 (see CMakeLists.txt).
#ifndef CLOUD_DEBUG
#define CLOUD_DEBUG 0
#endif

namespace coralmicro {
namespace {

const std::string kModelPath =
    "/models/cloud_regressor_qat_int8_edgetpu.tflite";
constexpr int kTensorArenaSize = 8 * 1024 * 1024;
STATIC_TENSOR_ARENA_IN_SDRAM(tensor_arena, kTensorArenaSize);

// ---------------------------------------------------------------------------
// UART6 (TX = GPIO_EMC_B1_40, RX = GPIO_EMC_B1_41, left header)
//
// Polled I/O only — no interrupts. The LPUART IRQ vectors are owned by the
// NXP serial-manager adapter (fsl_adapter_lpuart.c), which dispatches into
// per-instance state that is null for LPUART6 now that the debug console
// lives on LPUART7. Any LPUART6 interrupt would hard-fault inside that
// handler, so the interrupt-driven LPUART_RTOS driver must not be used here.
// ---------------------------------------------------------------------------

// Inference interval (ms). Written by OBC command task, read by main loop.
static volatile uint32_t g_inference_interval_ms = 10000;

// Sleep flag. Set by the OBC SLEEP command, cleared by WAKE. While true the main
// loop suspends all inference (periodic, OBC TRIGGER, and button) and blocks
// until woken. Camera and TPU stay powered — this suspends the inference work
// (the dominant power draw), not a full peripheral power-down.
static volatile bool g_sleeping = false;

// Dead-man auto-wake. The payload stays asleep for at most this long per SLEEP;
// if no WAKE (or a refreshing SLEEP) arrives within the window it resumes
// inference on its own, so a dead or reset OBC cannot leave the payload
// suspended for the rest of the flight. Each SLEEP re-arms this timer, and the
// OBC must re-send SLEEP to suspend again after an auto-wake.
static constexpr uint32_t kMaxSleepMs = 5 * 60 * 1000;  // 5 minutes

// Handle to main task so OBCCommandTask can unblock it for a trigger.
static TaskHandle_t g_main_task_handle = nullptr;

#if CLOUD_DEBUG
// Last captured frame — served via the debug RPC endpoint (bench builds only).
static std::vector<uint8_t> g_last_image;
static float g_last_fraction = 0.0f;
static uint16_t g_last_width = 0, g_last_height = 0;

// Burst-capture ring for motion-blur inspection: holding the user button
// fills this with the most recent frames (no UART send), which the host then
// pulls by index over RPC.
struct BurstFrame {
    std::vector<uint8_t> pixels;
    float    fraction = 0.0f;
    uint16_t width = 0, height = 0;
};
static constexpr int kBurstRingSize = 8;
static BurstFrame g_burst_ring[kBurstRingSize];
static int g_burst_count = 0;  // valid frames, capped at ring size
static int g_burst_head  = 0;  // next slot to write
#endif  // CLOUD_DEBUG

// Persisted sequence counter so SEQ does not collide across reboots.
const char kSeqPath[] = "/cloud_seq";

// On an unrecoverable init failure, reboot so a transient fault self-recovers
// (no one can press reset in orbit). The delay keeps a persistent failure from
// becoming a tight reboot loop that hammers the power rail.
[[noreturn]] static void FatalRestart(const char* what) {
    printf("FATAL: %s — restarting in 5 s\r\n", what);
    vTaskDelay(pdMS_TO_TICKS(5000));
    ResetToFlash();
    while (true) {}  // ResetToFlash does not return
}

static uint32_t LoadSeq() {
    uint32_t seq = 0;  // stays 0 if the file is absent or short
    LfsReadFile(kSeqPath, reinterpret_cast<uint8_t*>(&seq), sizeof(seq));
    return seq;
}

static void SaveSeq(uint32_t seq) {
    LfsWriteFile(kSeqPath, reinterpret_cast<const uint8_t*>(&seq), sizeof(seq));
}

// ---------------------------------------------------------------------------
// On-board flash backup
//
// Independent of the UART link to the OBC: every successful inference gets a
// one-line CSV record (seq,fraction), and every kBackupImageEveryN-th also
// gets its full 224x224 image saved under /backup. This is a dumb backup, not
// a downlink path -- there is no OBC command for it. Retrieval is manual:
// after the flight, connect the board over USB and pull files via the
// list_backup_files / get_backup_file RPCs (see tools/fetch_backup.py).
// Images are the same headerless 224x224 Y8 format the OBC's SD card gets, so
// tools/raw_to_png.py converts them too.
//
// The littlefs partition needs the coralmicro fork patch
// patches/coralmicro-littlefs-full-partition.patch, which widens it from the
// upstream 64 MiB to the chip's full 128 MiB (see that patch's comment in
// filesystem.cc for why upstream only used half). After the model (~2.8 MiB)
// that leaves ~123.7 MiB free. Once kBackupMaxImages is reached, further
// images are just skipped -- NOT wrapped/overwritten -- so a long flight
// fills up and stops rather than erasing the early (most interesting:
// ascent) frames.
//
// Sized against the mission profile: flights run at most 6 h at the 10 s
// inference cadence the OBC actually configures (coral.c's SET_INTERVAL
// 10000), i.e. <= 2160 inferences/flight. Saving every frame (no
// downsampling) needs:
//   2160 images * 50176 B = ~103.4 MB, vs. ~123.7 MB free (~16% margin) --
// only fits because of the full-partition patch above; on the stock 64 MiB
// partition this same setting would run out after ~3.5 h. kBackupMaxImages
// is 2200, a little above the 2160 a full 6 h flight actually produces, so
// the cap is a backstop (e.g. against a shorter SET_INTERVAL) rather than
// the thing that ends up truncating a nominal flight.
// ---------------------------------------------------------------------------
const char kBackupDir[]     = "/backup";
const char kBackupLogPath[] = "/backup/log.csv";
constexpr int kBackupImageEveryN  = 1;
constexpr int kBackupMaxImages    = 2200;
// Backstop only (not sized against a real budget): stop growing the CSV log
// past this size. At one ~12-byte line per inference this is many years of
// continuous 10 s-cadence logging, so it should never actually bind.
constexpr size_t kBackupLogMaxBytes = 2 * 1024 * 1024;

static int g_backup_img_count = 0;
static bool g_backup_img_cap_logged = false;

// Counts existing backup images at boot (rather than a separate persisted
// counter) so a mid-flight reboot resumes the cap correctly without adding
// another small file that gets rewritten -- and worn -- every capture.
static int CountBackupImages() {
    lfs_dir_t dir;
    if (lfs_dir_open(Lfs(), &dir, kBackupDir) < 0) return 0;
    int count = 0;
    lfs_info info;
    while (lfs_dir_read(Lfs(), &dir, &info) > 0) {
        if (info.type == LFS_TYPE_REG) ++count;
    }
    lfs_dir_close(Lfs(), &dir);
    return count;
}

static void BackupInit() {
    LfsMakeDirs(kBackupDir);
    g_backup_img_count = CountBackupImages();
    printf("Backup: %d image(s) already on flash (cap %d).\r\n",
           g_backup_img_count, kBackupMaxImages);
}

// Appends one "seq,fraction\n" line. Cheap enough (~12 bytes) to do on every
// inference regardless of the image cap below.
static void BackupAppendLog(uint32_t seq, float fraction) {
    auto size = LfsSize(kBackupLogPath);
    if (size < 0) size = 0;
    if (static_cast<size_t>(size) >= kBackupLogMaxBytes) return;

    char line[32];
    int n = snprintf(line, sizeof(line), "%lu,%.4f\n",
                     (unsigned long)seq, fraction);
    if (n <= 0) return;

    lfs_file_t file;
    if (lfs_file_open(Lfs(), &file, kBackupLogPath,
                      LFS_O_RDWR | LFS_O_CREAT) < 0) {
        return;
    }
    lfs_file_seek(Lfs(), &file, size, LFS_SEEK_SET);
    lfs_file_write(Lfs(), &file, line, static_cast<lfs_size_t>(n));
    lfs_file_close(Lfs(), &file);
}

// Saves one full image, unless the cap has already been reached -- in which
// case this is a no-op (see banner comment: stop, don't overwrite).
static void BackupSaveImage(uint32_t seq, const std::vector<uint8_t>& pixels) {
    if (g_backup_img_count >= kBackupMaxImages) {
        if (!g_backup_img_cap_logged) {
            printf("Backup: image cap (%d) reached, no further images saved "
                   "to flash.\r\n", kBackupMaxImages);
            g_backup_img_cap_logged = true;
        }
        return;
    }
    char path[48];
    snprintf(path, sizeof(path), "%s/img_%08lu.raw", kBackupDir,
             (unsigned long)seq);
    if (LfsWriteFile(path, pixels.data(), pixels.size())) {
        ++g_backup_img_count;
    } else {
        printf("Backup: write to %s failed (flash full?) -- stopping image "
               "backups.\r\n", path);
        g_backup_img_count = kBackupMaxImages;  // don't keep retrying
    }
}

// ---------------------------------------------------------------------------
// CRC-16/CCITT-FALSE  (poly 0x1021, init 0xFFFF, no reflection)
// ---------------------------------------------------------------------------
static uint16_t Crc16Update(uint16_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
static uint16_t Crc16(const uint8_t* data, size_t len) {
    return Crc16Update(0xFFFF, data, len);
}

// Drive the white Edge TPU LED to a defined level.
//
// LedSet(Led::kTpu,false) only calls PWM_StopTimer, which latches the LED pin
// at whatever level it held when the timer stopped — usually high, so the LED
// stays on. Instead we keep the PWM timer running and set the duty cycle:
// 0% = constant low = LED truly off, 100% = constant high = LED on.
// (FLEXPWM1 submodule 1, pin A — "used internally for TPU LED" per datasheet.)
static void TpuLedSet(bool on) {
    static bool inited = false;
    if (!inited) { PwmInit(); inited = true; }
    PwmPinConfig cfg;
    cfg.duty_cycle  = on ? 100 : 0;
    cfg.frequency   = 1000;
    cfg.pin_setting = PwmPinSettingFor(PwmPin::k10);
    cfg.pin_setting.sub_module = kPWM_Module_1;
    PwmEnable({cfg});
}

// Hold the TPU LED off this long on each side of the exposure.
static constexpr int kLedBlankMarginMs = 100;

// Auto-exposure warm-up. In trigger mode the sensor only exposes when we
// trigger it, so AE can't track a changing scene between captures — and
// DiscardFrames() does NOT work here (it never triggers, so it spins forever).
// Instead WarmUpAe() drives the trigger itself for N throwaway frames: a
// cold-start pass at boot, then a short pre-roll before each periodic capture
// so AE re-converges to the current scene. (The button burst path needs none —
// its back-to-back frames keep AE running on their own.)
static constexpr int kAeWarmupFrames  = 100;  // one-time, at boot
static constexpr int kAePrerollFrames = 30;   // before each periodic capture

// ---------------------------------------------------------------------------
// Inference
// ---------------------------------------------------------------------------
bool CloudFractionFromCamera(tflite::MicroInterpreter* interpreter,
                             int model_width, int model_height,
                             float* fraction_out,
                             std::vector<uint8_t>* gray_image) {
    CHECK(fraction_out != nullptr);
    CHECK(gray_image != nullptr);
    auto* input_tensor = interpreter->input_tensor(0);

    CameraFrameFormat fmt{CameraFormat::kY8,
                          CameraFilterMethod::kBilinear,
                          CameraRotation::k270,
                          model_width,
                          model_height,
                          /*preserve_ratio=*/false,
                          gray_image->data()};
    // The TPU LED is blanked by the caller across the whole capture window
    // (warm-up frames included) — see CloudConsole — so nothing to toggle here.
    CameraTask::GetSingleton()->Trigger();
    bool ok = CameraTask::GetSingleton()->GetFrame({fmt});
    if (!ok) return false;

    // Preprocessing: [0,255] -> [-1,1] to match MobileNetV2 training pipeline.
    const float in_scale = input_tensor->params.scale;
    const int   in_zp    = input_tensor->params.zero_point;
    int8_t* in = tflite::GetTensorData<int8_t>(input_tensor);
    const int num_pixels = model_width * model_height;
    for (int i = 0; i < num_pixels; ++i) {
        float real = (*gray_image)[i] / 127.5f - 1.0f;
        int q = static_cast<int>(std::lround(real / in_scale)) + in_zp;
        if (q < -128) q = -128;
        if (q > 127)  q =  127;
        int8_t qv = static_cast<int8_t>(q);
        in[i * 3 + 0] = qv;
        in[i * 3 + 1] = qv;
        in[i * 3 + 2] = qv;
    }

    if (interpreter->Invoke() != kTfLiteOk) return false;

    auto* output_tensor = interpreter->output_tensor(0);
    const float out_scale = output_tensor->params.scale;
    const int   out_zp    = output_tensor->params.zero_point;
    int8_t raw = tflite::GetTensorData<int8_t>(output_tensor)[0];
    float frac = (raw - out_zp) * out_scale;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    *fraction_out = frac;
    return true;
}

// Trigger-driven AE warm-up (see kAeWarmupFrames / kAePrerollFrames). Captures
// `frames` throwaway frames into `scratch` so auto-exposure converges; unlike
// DiscardFrames(), this drives the trigger so it works in trigger mode.
static void WarmUpAe(int model_width, int model_height,
                     std::vector<uint8_t>* scratch, int frames) {
    CameraFrameFormat fmt{CameraFormat::kY8,
                          CameraFilterMethod::kBilinear,
                          CameraRotation::k270,
                          model_width,
                          model_height,
                          /*preserve_ratio=*/false,
                          scratch->data()};
    for (int i = 0; i < frames; ++i) {
        CameraTask::GetSingleton()->Trigger();
        CameraTask::GetSingleton()->GetFrame({fmt});
    }
}

// ---------------------------------------------------------------------------
// UART packet transmit
//
// Frame layout (all multi-byte fields big-endian):
//
//  Field       Bytes  Description
//  ──────────  ─────  ─────────────────────────────────────────────────────
//  SOF         2      0xAA 0x55  (not covered by CRC)
//  TYPE        1      0x01                                \
//  SEQ         4      uint32 sequence number               |
//  LEN         4      uint32 payload length in bytes       |  CRC covers
//  FRAC        2      uint16 cloud fraction × 65535        |  all of these
//  WIDTH       2      uint16 image width  (224)            |
//  HEIGHT      2      uint16 image height (224)            |
//  FORMAT      1      0x01 = Y8 grayscale                  |
//  PIXELS    W×H      raw uint8 pixel data                /
//  CRC16       2      CRC-16/CCITT-FALSE over TYPE..PIXELS
//
//  Total for 224×224: 2+1+4+4+2+2+2+1+50176+2 = 50196 bytes
// ---------------------------------------------------------------------------
static void SendImagePacket(uint32_t seq, float fraction,
                            const std::vector<uint8_t>& pixels,
                            uint16_t width, uint16_t height) {
    constexpr uint8_t kType  = 0x01;
    constexpr uint8_t kFmtY8 = 0x01;
    const uint16_t frac_u16  = static_cast<uint16_t>(fraction * 65535.0f);
    const uint32_t payload_len =
        sizeof(frac_u16) + sizeof(width) + sizeof(height) + 1u + pixels.size();

    // 16-byte header block that follows SOF — this is what the CRC covers (with pixels).
    uint8_t hdr[16];
    hdr[0]  = kType;
    hdr[1]  = (seq >> 24) & 0xFF;
    hdr[2]  = (seq >> 16) & 0xFF;
    hdr[3]  = (seq >>  8) & 0xFF;
    hdr[4]  = (seq      ) & 0xFF;
    hdr[5]  = (payload_len >> 24) & 0xFF;
    hdr[6]  = (payload_len >> 16) & 0xFF;
    hdr[7]  = (payload_len >>  8) & 0xFF;
    hdr[8]  = (payload_len      ) & 0xFF;
    hdr[9]  = (frac_u16 >> 8) & 0xFF;
    hdr[10] = (frac_u16     ) & 0xFF;
    hdr[11] = (width  >> 8) & 0xFF;
    hdr[12] = (width      ) & 0xFF;
    hdr[13] = (height >> 8) & 0xFF;
    hdr[14] = (height     ) & 0xFF;
    hdr[15] = kFmtY8;

    // CRC over header + pixels in two passes (no heap copy needed).
    uint16_t crc = Crc16(hdr, sizeof(hdr));
    crc = Crc16Update(crc, pixels.data(), pixels.size());
    const uint8_t crc_bytes[2] = {(uint8_t)(crc >> 8), (uint8_t)(crc & 0xFF)};

    uint8_t sof[2] = {0xAA, 0x55};
    LPUART_WriteBlocking(LPUART6, sof, sizeof(sof));
    LPUART_WriteBlocking(LPUART6, hdr, sizeof(hdr));
    LPUART_WriteBlocking(LPUART6, pixels.data(), pixels.size());
    LPUART_WriteBlocking(LPUART6, crc_bytes, sizeof(crc_bytes));
}

// ---------------------------------------------------------------------------
// OBC command receive task
//
// Commands (OBC -> Coral, big-endian):
//
//  Name          Bytes  Layout
//  ────────────  ─────  ──────────────────────────────────
//  TRIGGER       3      [0x10][CRC16_HI][CRC16_LO]
//  SET_INTERVAL  7      [0x11][ms_B3][ms_B2][ms_B1][ms_B0][CRC16_HI][CRC16_LO]
//  SLEEP         3      [0x17][CRC16_HI][CRC16_LO]   suspend inference until WAKE
//  WAKE          3      [0x18][CRC16_HI][CRC16_LO]   resume inference
//
//  CRC covers all bytes before the CRC field (1 byte for the 3-byte commands,
//  5 for SET_INTERVAL).
//
// Responses (Coral -> OBC):
//   0x06  ACK — command accepted
//   0x15  NAK — bad CRC or unknown command
// ---------------------------------------------------------------------------
// Polled read of one byte. Returns false if no byte arrived within timeout.
// Clears RX overrun so the receiver can't wedge after a burst (e.g. while a
// command arrives during the multi-second image transmit).
static bool UartReadByte(uint8_t* byte, TickType_t timeout_ticks) {
    const TickType_t deadline = xTaskGetTickCount() + timeout_ticks;
    while (true) {
        const uint32_t flags = LPUART_GetStatusFlags(LPUART6);
        if (flags & (uint32_t)kLPUART_RxOverrunFlag) {
            LPUART_ClearStatusFlags(LPUART6, kLPUART_RxOverrunFlag);
        }
        if (flags & (uint32_t)kLPUART_RxDataRegFullFlag) {
            *byte = LPUART_ReadByte(LPUART6);
            return true;
        }
        if (xTaskGetTickCount() >= deadline) return false;
        vTaskDelay(1);
    }
}

static bool UartReadBytes(uint8_t* dst, size_t len, TickType_t timeout_ticks) {
    for (size_t i = 0; i < len; ++i)
        if (!UartReadByte(dst + i, timeout_ticks)) return false;
    return true;
}

static void OBCCommandTask(void* param) {
    auto main_task = static_cast<TaskHandle_t>(param);
    uint8_t buf[7];
    uint8_t ack = 0x06;
    uint8_t nak = 0x15;
    // Once a command byte arrives, the rest must follow promptly.
    const TickType_t kInterByteTimeout = pdMS_TO_TICKS(100);

    while (true) {
        if (!UartReadByte(buf, portMAX_DELAY)) continue;
        const uint8_t cmd = buf[0];

        if (cmd == 0x10) {
            // TRIGGER: 2-byte CRC follows.
            if (!UartReadBytes(buf + 1, 2, kInterByteTimeout) ||
                (((uint16_t)buf[1] << 8) | buf[2]) != Crc16(buf, 1)) {
                LPUART_WriteBlocking(LPUART6, &nak, 1);
                continue;
            }
            xTaskNotifyGive(main_task);
            LPUART_WriteBlocking(LPUART6, &ack, 1);

        } else if (cmd == 0x11) {
            // SET_INTERVAL: 4-byte interval + 2-byte CRC follow.
            if (!UartReadBytes(buf + 1, 6, kInterByteTimeout) ||
                (((uint16_t)buf[5] << 8) | buf[6]) != Crc16(buf, 5)) {
                LPUART_WriteBlocking(LPUART6, &nak, 1);
                continue;
            }
            g_inference_interval_ms =
                ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) |
                ((uint32_t)buf[3] <<  8) | ((uint32_t)buf[4]      );
            LPUART_WriteBlocking(LPUART6, &ack, 1);

        } else if (cmd == 0x17) {
            // SLEEP: 2-byte CRC follows. Suspend inference until WAKE.
            if (!UartReadBytes(buf + 1, 2, kInterByteTimeout) ||
                (((uint16_t)buf[1] << 8) | buf[2]) != Crc16(buf, 1)) {
                LPUART_WriteBlocking(LPUART6, &nak, 1);
                continue;
            }
            // Always notify: a fresh SLEEP (re)arms the dead-man window, so an
            // already-sleeping loop unblocks and re-blocks with a new timeout.
            g_sleeping = true;
            xTaskNotifyGive(main_task);
            LPUART_WriteBlocking(LPUART6, &ack, 1);
            printf("OBC SLEEP: suspending inference (auto-wake in <= %u ms).\r\n",
                   (unsigned)kMaxSleepMs);

        } else if (cmd == 0x18) {
            // WAKE: 2-byte CRC follows. Resume inference.
            if (!UartReadBytes(buf + 1, 2, kInterByteTimeout) ||
                (((uint16_t)buf[1] << 8) | buf[2]) != Crc16(buf, 1)) {
                LPUART_WriteBlocking(LPUART6, &nak, 1);
                continue;
            }
            // Only unblock/capture if we were actually asleep — a WAKE while
            // already awake is a true no-op (no spurious extra capture).
            if (g_sleeping) {
                g_sleeping = false;
                xTaskNotifyGive(main_task);  // unblock the loop from its sleep wait
            }
            LPUART_WriteBlocking(LPUART6, &ack, 1);
            printf("OBC WAKE: resuming inference.\r\n");

        } else {
            LPUART_WriteBlocking(LPUART6, &nak, 1);
        }
    }
}

// ---------------------------------------------------------------------------
// Infer + transmit one frame
// ---------------------------------------------------------------------------
void CloudConsole(tflite::MicroInterpreter* interpreter, uint32_t seq) {
    auto* input_tensor = interpreter->input_tensor(0);
    int model_height = input_tensor->dims->data[1];
    int model_width  = input_tensor->dims->data[2];
    std::vector<uint8_t> gray_image(model_width * model_height);
    float fraction;
    // Blank the white TPU LED across the ENTIRE capture window — the AE warm-up
    // included, not just the final exposure. The LED is bright enough to reflect
    // off the case glass into the lens; if it is lit while auto-exposure
    // converges, AE locks onto the reflection and the real capture comes out
    // mis-exposed. Turn it off before the pre-roll, restore it after read-out.
    TpuLedSet(false);
    vTaskDelay(pdMS_TO_TICKS(kLedBlankMarginMs));
    // Let AE re-converge to the current (LED-free) scene before the real
    // capture. The warm-up frames land in gray_image and are overwritten.
    WarmUpAe(model_width, model_height, &gray_image, kAePrerollFrames);
    bool ok = CloudFractionFromCamera(interpreter, model_width, model_height,
                                      &fraction, &gray_image);
    vTaskDelay(pdMS_TO_TICKS(kLedBlankMarginMs));
    TpuLedSet(true);
    if (ok) {
        printf("cloud cover: %.1f%% (seq=%u)\r\n",
               fraction * 100.0f, (unsigned)seq);
#if CLOUD_DEBUG
        g_last_image    = gray_image;
        g_last_fraction = fraction;
        g_last_width    = (uint16_t)model_width;
        g_last_height   = (uint16_t)model_height;
#endif
        SendImagePacket(seq, fraction, gray_image,
                        (uint16_t)model_width, (uint16_t)model_height);
        BackupAppendLog(seq, fraction);
        if (seq % kBackupImageEveryN == 0) {
            BackupSaveImage(seq, gray_image);
        }
    } else {
        printf("Failed to read cloud cover from camera.\r\n");
    }
}

// ---------------------------------------------------------------------------
// Backup retrieval RPCs -- always exported (flight build included). This is
// the "connect over USB after landing" read-out path for the /backup files
// written by BackupAppendLog / BackupSaveImage above; there is no OBC/UART
// equivalent. See tools/fetch_backup.py for the host-side puller.
// ---------------------------------------------------------------------------

// Lists /backup contents as JSON: [{"name":..., "size":...}, ...].
// Call: POST http://10.10.10.1/jsonrpc {"method":"list_backup_files"}
void ListBackupFilesRpc(struct jsonrpc_request* r) {
    lfs_dir_t dir;
    if (lfs_dir_open(Lfs(), &dir, kBackupDir) < 0) {
        jsonrpc_return_success(r, "[]");  // no backups written yet
        return;
    }
    std::string json = "[";
    lfs_info info;
    bool first = true;
    while (lfs_dir_read(Lfs(), &dir, &info) > 0) {
        if (info.type != LFS_TYPE_REG) continue;
        if (!first) json += ",";
        first = false;
        json += "{\"name\":\"" + std::string(info.name) +
                "\",\"size\":" + std::to_string(info.size) + "}";
    }
    lfs_dir_close(Lfs(), &dir);
    json += "]";
    jsonrpc_return_success(r, "%s", json.c_str());
}

// Returns one /backup file's raw bytes, base64-encoded.
// Call with params {"name": "img_00003360.raw"} (or "log.csv").
void GetBackupFileRpc(struct jsonrpc_request* r) {
    // Read "name" directly as $.name — matches fetch_backup.py's {"name": ...}
    // object params (and the $.index style used by the burst RPC below).
    // Do NOT use coralmicro's JsonRpcGetStringParam here: it expects $[0].name
    // (array-wrapped params) so it never matched, AND it emits its own error
    // before returning false — combined with the second error this handler used
    // to send, that produced a malformed two-object JSON reply that wedged the
    // RPC server (hung the board). One param read, one error path, one reply.
    char name[64] = {0};
    int name_len = mjson_get_string(r->params, r->params_len, "$.name",
                                    name, sizeof(name));
    if (name_len <= 0 || strchr(name, '/') != nullptr) {
        jsonrpc_return_error(r, -1, "missing/invalid 'name' param", nullptr);
        return;
    }
    std::vector<uint8_t> data;
    std::string path = std::string(kBackupDir) + "/" + name;
    if (!LfsReadFile(path.c_str(), &data)) {
        jsonrpc_return_error(r, -1, "not found", nullptr);
        return;
    }
    jsonrpc_return_success(r, "{%Q:%V}", "data", data.size(), data.data());
}

#if CLOUD_DEBUG
// Bench-only (CLOUD_DEBUG). Returns the last captured frame + cloud cover over
// HTTP/RPC so the host can display it.
// Call: POST http://10.10.10.1/jsonrpc {"method":"get_last_image",...}
void GetLastImageRpc(struct jsonrpc_request* r) {
    if (g_last_image.empty()) {
        jsonrpc_return_error(r, -1, "No image captured yet.", nullptr);
        return;
    }
    jsonrpc_return_success(r, "{%Q:%d,%Q:%d,%Q:%g,%Q:%V}",
                           "width",    g_last_width,
                           "height",   g_last_height,
                           "fraction", g_last_fraction,
                           "pixels",   g_last_image.size(),
                           g_last_image.data());
}

// Bench-only (CLOUD_DEBUG). Stores one frame into the burst ring.
static void BurstStoreFrame(const std::vector<uint8_t>& pixels, float fraction,
                            uint16_t width, uint16_t height) {
    BurstFrame& slot = g_burst_ring[g_burst_head];
    slot.pixels   = pixels;
    slot.fraction = fraction;
    slot.width    = width;
    slot.height   = height;
    g_burst_head  = (g_burst_head + 1) % kBurstRingSize;
    if (g_burst_count < kBurstRingSize) ++g_burst_count;
}

// Bench-only (CLOUD_DEBUG).
// Captures frames as fast as the camera allows for as long as the user button
// is held, keeping the most recent kBurstRingSize in RAM. No UART send.
static void BurstCaptureWhileHeld(tflite::MicroInterpreter* interpreter) {
    auto* input_tensor = interpreter->input_tensor(0);
    int model_height = input_tensor->dims->data[1];
    int model_width  = input_tensor->dims->data[2];
    std::vector<uint8_t> gray_image(model_width * model_height);
    float fraction;

    g_burst_count = 0;  // start a fresh burst
    g_burst_head  = 0;
    int n = 0;
    // Keep the TPU LED blanked for the whole burst too, so these frames match
    // what the flight path captures (no LED reflection off the case glass).
    TpuLedSet(false);
    while (!GpioGet(Gpio::kUserButton)) {  // active-low: low == pressed
        if (CloudFractionFromCamera(interpreter, model_width, model_height,
                                    &fraction, &gray_image)) {
            BurstStoreFrame(gray_image, fraction,
                            (uint16_t)model_width, (uint16_t)model_height);
            ++n;
        }
        vTaskDelay(1);  // yield so lower-priority tasks (USB) still run
    }
    TpuLedSet(true);
    printf("burst: %d frames captured, ring holds last %d\r\n",
           n, g_burst_count);
}

// Bench-only (CLOUD_DEBUG). Number of frames in the burst ring.
void GetBurstCountRpc(struct jsonrpc_request* r) {
    jsonrpc_return_success(r, "{%Q:%d}", "count", g_burst_count);
}

// Bench-only (CLOUD_DEBUG). Returns burst frame at "index"
// (0 = oldest .. count-1 = newest). Call with params {"index": N}.
void GetBurstFrameRpc(struct jsonrpc_request* r) {
    double idx_d;
    if (mjson_get_number(r->params, r->params_len, "$.index", &idx_d) == 0) {
        jsonrpc_return_error(r, -1, "missing 'index' param", nullptr);
        return;
    }
    int i = static_cast<int>(idx_d);
    if (i < 0 || i >= g_burst_count) {
        jsonrpc_return_error(r, -1, "index out of range", nullptr);
        return;
    }
    // Oldest valid frame sits g_burst_count slots behind the write head.
    int slot = (g_burst_head + kBurstRingSize - g_burst_count + i) %
               kBurstRingSize;
    const BurstFrame& f = g_burst_ring[slot];
    jsonrpc_return_success(r, "{%Q:%d,%Q:%d,%Q:%g,%Q:%V}",
                           "width",    f.width,
                           "height",   f.height,
                           "fraction", f.fraction,
                           "pixels",   f.pixels.size(),
                           f.pixels.data());
}
#endif  // CLOUD_DEBUG

// ---------------------------------------------------------------------------
// UART6 init
// ---------------------------------------------------------------------------
static bool UartInit() {
    CLOCK_EnableClock(kCLOCK_Lpuart6);

    // Pad config uses the RT1176 EMC_B1 field layout:
    //   bit1 PDRV (1=normal drive), bits[3:2] PULL (01=up, 11=none), bit4 ODE.
    // TX: GPIO_EMC_B1_40 -> LPUART6_TXD (ALT3). Normal drive, no pull.
    IOMUXC_SetPinMux(IOMUXC_GPIO_EMC_B1_40_LPUART6_TXD, 0);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_EMC_B1_40_LPUART6_TXD, 0x0EU);

    // RX: GPIO_EMC_B1_41 -> LPUART6_RXD (ALT3). Internal pull-up keeps the
    // line at UART idle when the OBC is disconnected or unpowered — a
    // floating RX generates a storm of false start-bit interrupts.
    IOMUXC_SetPinMux(IOMUXC_GPIO_EMC_B1_41_LPUART6_RXD, 1);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_EMC_B1_41_LPUART6_RXD, 0x06U);

    // Plain polled driver — interrupts stay disabled (see comment at top of
    // the UART section). FIFOs absorb short RX bursts between polls.
    lpuart_config_t config;
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = 115200;
    config.enableTx     = true;
    config.enableRx     = true;

    return LPUART_Init(LPUART6, &config,
                       CLOCK_GetRootClockFreq(kCLOCK_Root_Lpuart6)) ==
           kStatus_Success;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
[[noreturn]] void Main() {
    LedSet(Led::kStatus, true);
    printf("Cloud Cover Estimator!\r\n");

    // The JSON-RPC/HTTP server is brought up unconditionally: it also enumerates
    // the USB CDC console that printf logging relies on. Flight builds export
    // only the backup read-out endpoints (list_backup_files/get_backup_file);
    // bench builds (CLOUD_DEBUG) additionally export the image/burst
    // inspection endpoints.
    jsonrpc_init(nullptr, nullptr);
    jsonrpc_export("list_backup_files", ListBackupFilesRpc);
    jsonrpc_export("get_backup_file", GetBackupFileRpc);
#if CLOUD_DEBUG
    jsonrpc_export("get_last_image", GetLastImageRpc);
    jsonrpc_export("get_burst_count", GetBurstCountRpc);
    jsonrpc_export("get_burst_frame", GetBurstFrameRpc);
#endif
    UseHttpServer(new JsonRpcHttpServer);

    std::vector<uint8_t> model;
    if (!LfsReadFile(kModelPath.c_str(), &model)) {
        FatalRestart("failed to load model");
    }
    printf("Model loaded (%u bytes)\r\n", (unsigned)model.size());
    BackupInit();

    auto tpu_context = EdgeTpuManager::GetSingleton()->OpenDevice();
    if (!tpu_context) {
        FatalRestart("Edge TPU open failed");
    }

    tflite::MicroErrorReporter error_reporter;
    tflite::MicroMutableOpResolver<1> resolver;
    resolver.AddCustom(kCustomOp, RegisterCustomOp());

    tflite::MicroInterpreter interpreter(tflite::GetModel(model.data()),
                                         resolver, tensor_arena,
                                         kTensorArenaSize, &error_reporter);
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        FatalRestart("AllocateTensors failed");
    }
    if (interpreter.inputs().size() != 1) {
        FatalRestart("model has unexpected input count");
    }

    auto* in_t  = interpreter.input_tensor(0);
    auto* out_t = interpreter.output_tensor(0);
    printf("input  %dx%dx%d  scale=%f zp=%d\r\n",
           in_t->dims->data[1], in_t->dims->data[2], in_t->dims->data[3],
           in_t->params.scale, in_t->params.zero_point);
    printf("output scale=%f zp=%d\r\n",
           out_t->params.scale, out_t->params.zero_point);

    if (!UartInit()) {
        FatalRestart("UART6 init failed");
    }
    printf("UART6 ready at 115200 baud (TX=EMC_B1_40, RX=EMC_B1_41)\r\n");

    LedSet(Led::kUser, true);

    CameraTask::GetSingleton()->SetPower(true);
    CameraTask::GetSingleton()->Enable(CameraMode::kTrigger);
    // Cold-start AE convergence (see kAeWarmupFrames). Each periodic capture
    // additionally pre-rolls a few frames to track the changing scene.
    // Blank the TPU LED across this boot warm-up too — it IS exposure
    // adjustment, so a lit LED makes AE converge to its glass reflection (same
    // reason as the per-capture blanking in CloudConsole). The LED stays on
    // through boot/init as an alive indicator, then drops just before warm-up.
    {
        int wh = in_t->dims->data[1], ww = in_t->dims->data[2];
        std::vector<uint8_t> warmup(ww * wh);
        TpuLedSet(false);
        vTaskDelay(pdMS_TO_TICKS(kLedBlankMarginMs));
        WarmUpAe(ww, wh, &warmup, kAeWarmupFrames);
        TpuLedSet(true);
    }
    printf("Camera ready. Inferring every %u ms or on button/OBC trigger.\r\n",
           (unsigned)g_inference_interval_ms);

    g_main_task_handle = xTaskGetCurrentTaskHandle();

    // Button wakes the main loop via task notification (same mechanism as OBC trigger).
    GpioConfigureInterrupt(
        Gpio::kUserButton, GpioInterruptMode::kIntModeFalling,
        [handle = xTaskGetCurrentTaskHandle()]() {
            BaseType_t woken = pdFALSE;
            vTaskNotifyGiveFromISR(handle, &woken);
            portYIELD_FROM_ISR(woken);
        },
        /*debounce_interval_us=*/50 * 1e3);

    xTaskCreate(OBCCommandTask, "obc_cmd",
                /*stack_depth=*/configMINIMAL_STACK_SIZE * 4,
                g_main_task_handle, tskIDLE_PRIORITY + 1, nullptr);

    uint32_t seq = LoadSeq();  // resume past any SEQs used before a reboot
    printf("Resuming at seq=%u\r\n", (unsigned)seq);
    while (true) {
        // Wait the configured interval, OR wake early on a button / OBC TRIGGER.
        // While asleep wait at most kMaxSleepMs, so a dead/reset OBC can't leave
        // us suspended forever (dead-man auto-wake below).
        uint32_t notified = ulTaskNotifyTake(
            pdTRUE, g_sleeping ? pdMS_TO_TICKS(kMaxSleepMs)
                               : pdMS_TO_TICKS(g_inference_interval_ms));
        if (g_sleeping) {
            // A WAKE clears g_sleeping (so we fall through and capture); any
            // other notify (stray TRIGGER/button, or a refreshing SLEEP) leaves
            // it set and we re-block with a fresh window.
            if (notified != 0) continue;
            // Timed out still asleep: dead-man auto-wake. The OBC must re-send
            // SLEEP if it still wants us suspended.
            g_sleeping = false;
            printf("Auto-wake: no OBC WAKE within %u ms; resuming inference. "
                   "Re-send SLEEP to suspend again.\r\n", (unsigned)kMaxSleepMs);
        }
#if CLOUD_DEBUG
        // Bench: holding the user button runs a motion-blur burst capture
        // (frames kept in RAM, no UART send) instead of the normal cycle.
        if (!GpioGet(Gpio::kUserButton)) {  // active-low: low == still held
            BurstCaptureWhileHeld(&interpreter);
            continue;
        }
#endif
        CloudConsole(&interpreter, seq);
        SaveSeq(++seq);  // persist the next SEQ so it survives a reboot
    }
}

}  // namespace
}  // namespace coralmicro

extern "C" void app_main(void* param) {
    (void)param;
    coralmicro::Main();
}
