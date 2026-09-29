#include "audio.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s_std.h>
#include <driver/i2s_tdm.h>
#include <esp_afe_config.h>
#include <esp_afe_sr_iface.h>
#include <esp_afe_sr_models.h>
#include <freertos/idf_additions.h>
#include <freertos/stream_buffer.h>

#include "config.h"

// Modelled on Waveshare's XiaoZhi firmware for this board (main/audio/codecs/box_audio_codec.cc and
// main/audio/processors/afe_audio_processor.cc):
//
//   ES8311 (speaker) <- I2S standard stereo
//   ES7210 (mics)    -> I2S TDM, 4 slots. Slot 0 is the microphone; slot 1 is a hardware loopback of
//                       the speaker signal, sample-aligned with the microphone.
//   ESP-SR AFE       <- "MR" (mic, reference), removes the speaker from the microphone signal.
//
// With the echo removed, the microphone stays open while the persona talks, so you can interrupt it.

namespace {

constexpr size_t FRAME = AUDIO_SAMPLE_RATE / 50;  // 20 ms, in samples
constexpr size_t PLAY_BUFFER = 2 * 1024 * 1024;   // ~65 s. Gemini sends audio faster than real time.
constexpr size_t MIC_BUFFER = 64 * 1024;
// Buffer this much before starting playback, to ride out network jitter.
constexpr size_t PREBUFFER_BYTES = AUDIO_SAMPLE_RATE * 2 * 150 / 1000;
constexpr int TDM_SLOTS = 4;

i2s_chan_handle_t txChannel;
i2s_chan_handle_t rxChannel;
const esp_afe_sr_iface_t *afe;
esp_afe_sr_data_t *afeData;
StreamBufferHandle_t playBuffer;
StreamBufferHandle_t micBuffer;
TaskHandle_t playTask, recordTask, afeTask;

volatile bool running;
volatile bool flushRequested;
volatile bool playing;
volatile float level;

// Codec registers. The sequences follow Espressif's esp_codec_dev drivers (es8311.c and es7210.c,
// Apache-2.0), which XiaoZhi uses: slave mode, MCLK = 256 x sample rate, 16-bit samples.

void writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, (uint8_t)1) != 1) return 0;
  return Wire.read();
}

void updateReg(uint8_t addr, uint8_t reg, uint8_t mask, uint8_t value) {
  writeReg(addr, reg, (readReg(addr, reg) & ~mask) | (value & mask));
}

bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool es8311Begin() {
  constexpr uint8_t A = ES8311_ADDR;
  if (!present(A)) return false;

  // open(): slave mode, clock from the MCLK pin.
  writeReg(A, 0x44, 0x08);  // Better I2C noise immunity. Written twice; the first write may fail.
  writeReg(A, 0x44, 0x08);
  writeReg(A, 0x01, 0x30);
  writeReg(A, 0x02, 0x00);
  writeReg(A, 0x03, 0x10);
  writeReg(A, 0x16, 0x24);
  writeReg(A, 0x04, 0x10);
  writeReg(A, 0x05, 0x00);
  writeReg(A, 0x0B, 0x00);
  writeReg(A, 0x0C, 0x00);
  writeReg(A, 0x10, 0x1F);
  writeReg(A, 0x11, 0x7F);
  writeReg(A, 0x00, 0x80);  // Slave mode
  writeReg(A, 0x01, 0x3F);  // Use MCLK, not inverted
  updateReg(A, 0x06, 0x20, 0x00);  // SCLK not inverted
  writeReg(A, 0x13, 0x10);
  writeReg(A, 0x1B, 0x0A);
  writeReg(A, 0x1C, 0x6A);

  // set_fs(): 16 bits, I2S format, 16 kHz with MCLK 4.096 MHz
  // (coeff_div row {4096000, 16000, 1, 1, 1, 1, 0, 0x00, 0xff, 4, 0x10, 0x20}).
  updateReg(A, 0x09, 0x1F, 0x0C);
  updateReg(A, 0x0A, 0x1F, 0x0C);
  updateReg(A, 0x02, 0xF8, 0x00);  // pre_div 1, pre_multi 1
  writeReg(A, 0x05, 0x00);         // adc_div 1, dac_div 1
  updateReg(A, 0x03, 0x7F, 0x10);  // single speed, adc osr 0x10
  updateReg(A, 0x04, 0x7F, 0x20);  // dac osr 0x20
  updateReg(A, 0x07, 0x3F, 0x00);  // lrck_h
  writeReg(A, 0x08, 0xFF);         // lrck_l
  updateReg(A, 0x06, 0x1F, 0x03);  // bclk_div 4

  // start(): DAC only.
  updateReg(A, 0x09, 0x40, 0x00);  // Unmute DAC input
  updateReg(A, 0x0A, 0x40, 0x40);  // Keep the unused ADC output muted
  writeReg(A, 0x17, 0xBF);
  writeReg(A, 0x0E, 0x02);
  writeReg(A, 0x12, 0x00);
  writeReg(A, 0x14, 0x1A);
  writeReg(A, 0x0D, 0x01);
  writeReg(A, 0x15, 0x40);
  writeReg(A, 0x37, 0x08);
  writeReg(A, 0x45, 0x00);

  writeReg(A, 0x32, SPEAKER_VOLUME);
  updateReg(A, 0x31, 0x60, 0x00);  // Unmute
  return true;
}

void es8311End() {
  constexpr uint8_t A = ES8311_ADDR;
  writeReg(A, 0x32, 0x00);
  writeReg(A, 0x17, 0x00);
  writeReg(A, 0x0E, 0xFF);
  writeReg(A, 0x12, 0x02);
  writeReg(A, 0x14, 0x00);
  writeReg(A, 0x0D, 0xFA);
  writeReg(A, 0x15, 0x00);
  writeReg(A, 0x02, 0x10);
  writeReg(A, 0x00, 0x00);
  writeReg(A, 0x00, 0x1F);
  writeReg(A, 0x01, 0x30);
  writeReg(A, 0x01, 0x00);
  writeReg(A, 0x45, 0x00);
  writeReg(A, 0x0D, 0xFC);
  writeReg(A, 0x02, 0x00);
}

// All four inputs, as XiaoZhi selects them, which puts the ES7210 in TDM mode.
void es7210SelectMics() {
  constexpr uint8_t A = ES7210_ADDR;
  for (uint8_t reg = 0x43; reg <= 0x46; reg++) updateReg(A, reg, 0x10, 0x00);
  writeReg(A, 0x4B, 0xFF);
  writeReg(A, 0x4C, 0xFF);
  updateReg(A, 0x01, 0x0B, 0x00);  // MIC1 and MIC2 clocks on
  writeReg(A, 0x4B, 0x00);
  updateReg(A, 0x01, 0x15, 0x00);  // MIC3 and MIC4 clocks on
  writeReg(A, 0x4C, 0x00);
  for (uint8_t reg = 0x43; reg <= 0x46; reg++) updateReg(A, reg, 0x1F, 0x10 | MIC_GAIN);
  writeReg(A, 0x12, 0x02);  // TDM on
}

bool es7210Begin() {
  constexpr uint8_t A = ES7210_ADDR;
  if (!present(A)) return false;

  // open()
  writeReg(A, 0x00, 0xFF);
  writeReg(A, 0x00, 0x41);
  writeReg(A, 0x01, 0x3F);
  writeReg(A, 0x09, 0x30);
  writeReg(A, 0x0A, 0x30);
  writeReg(A, 0x23, 0x2A);
  writeReg(A, 0x22, 0x0A);
  writeReg(A, 0x20, 0x0A);
  writeReg(A, 0x21, 0x2A);
  updateReg(A, 0x08, 0x01, 0x00);  // Slave mode
  writeReg(A, 0x40, 0x43);
  writeReg(A, 0x41, 0x70);  // Mic bias 2.87 V
  writeReg(A, 0x42, 0x70);
  writeReg(A, 0x07, 0x20);
  writeReg(A, 0x02, 0xC1);
  es7210SelectMics();
  uint8_t clockOff = readReg(A, 0x01);

  // set_fs(): 16 bits per slot, I2S format. In slave mode the clocks follow MCLK and LRCK.
  updateReg(A, 0x11, 0xE3, 0x60);

  // start()
  writeReg(A, 0x01, clockOff);
  writeReg(A, 0x06, 0x00);
  writeReg(A, 0x40, 0x43);
  writeReg(A, 0x47, 0x08);
  writeReg(A, 0x48, 0x08);
  writeReg(A, 0x49, 0x08);
  writeReg(A, 0x4A, 0x08);
  es7210SelectMics();
  writeReg(A, 0x40, 0x43);
  writeReg(A, 0x00, 0x71);
  writeReg(A, 0x00, 0x41);
  return true;
}

void es7210End() {
  constexpr uint8_t A = ES7210_ADDR;
  for (uint8_t reg = 0x47; reg <= 0x4C; reg++) writeReg(A, reg, 0xFF);
  writeReg(A, 0x40, 0xC0);
  writeReg(A, 0x01, 0x7F);
  writeReg(A, 0x06, 0x07);
}

// One I2S port in full duplex: standard stereo out, 4-slot TDM in (BoxAudioCodec::CreateDuplexChannels).
bool i2sBegin() {
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = 6;
  channel.dma_frame_num = 240;
  channel.auto_clear = true;  // Play silence when we fall behind.
  if (i2s_new_channel(&channel, &txChannel, &rxChannel) != ESP_OK) return false;

  i2s_std_config_t std = {};
  std.clk_cfg.sample_rate_hz = AUDIO_SAMPLE_RATE;
  std.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std.gpio_cfg.mclk = (gpio_num_t)PIN_I2S_MCLK;
  std.gpio_cfg.bclk = (gpio_num_t)PIN_I2S_BCLK;
  std.gpio_cfg.ws = (gpio_num_t)PIN_I2S_WS;
  std.gpio_cfg.dout = (gpio_num_t)PIN_I2S_DOUT;
  std.gpio_cfg.din = I2S_GPIO_UNUSED;

  i2s_tdm_config_t tdm = {};
  tdm.clk_cfg.sample_rate_hz = AUDIO_SAMPLE_RATE;
  tdm.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  tdm.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  tdm.clk_cfg.bclk_div = 8;
  tdm.slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
      (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3));
  tdm.slot_cfg.ws_width = I2S_TDM_AUTO_WS_WIDTH;
  tdm.slot_cfg.total_slot = I2S_TDM_AUTO_SLOT_NUM;
  tdm.gpio_cfg.mclk = (gpio_num_t)PIN_I2S_MCLK;
  tdm.gpio_cfg.bclk = (gpio_num_t)PIN_I2S_BCLK;
  tdm.gpio_cfg.ws = (gpio_num_t)PIN_I2S_WS;
  tdm.gpio_cfg.dout = I2S_GPIO_UNUSED;
  tdm.gpio_cfg.din = (gpio_num_t)PIN_I2S_DIN;

  return i2s_channel_init_std_mode(txChannel, &std) == ESP_OK &&
         i2s_channel_init_tdm_mode(rxChannel, &tdm) == ESP_OK && i2s_channel_enable(txChannel) == ESP_OK &&
         i2s_channel_enable(rxChannel) == ESP_OK;
}

void i2sEnd() {
  for (i2s_chan_handle_t *channel : {&txChannel, &rxChannel}) {
    if (!*channel) continue;
    i2s_channel_disable(*channel);
    i2s_del_channel(*channel);
    *channel = nullptr;
  }
}

// Echo cancellation, as AfeAudioProcessor with CONFIG_USE_DEVICE_AEC. No models are needed.
bool afeBegin() {
  afe_config_t *config = afe_config_init("MR", nullptr, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
  if (!config) return false;
  config->aec_init = true;
  config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
  config->vad_init = false;
  config->ns_init = false;
  config->agc_init = false;
  config->wakenet_init = false;
  config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
  afe = esp_afe_handle_from_config(config);
  afeData = afe ? afe->create_from_config(config) : nullptr;
  afe_config_free(config);
  return afeData != nullptr;
}

void playLoop(void *) {
  static int16_t mono[FRAME];
  static int16_t stereo[FRAME * 2];
  bool started = false;
  uint32_t waitingSince = 0;

  while (running) {
    if (flushRequested) {
      while (xStreamBufferReceive(playBuffer, mono, sizeof(mono), 0) > 0) {
      }
      flushRequested = false;
      started = false;
    }

    size_t available = xStreamBufferBytesAvailable(playBuffer);
    if (!started && available > 0) {
      if (!waitingSince) waitingSince = millis();
      started = available >= PREBUFFER_BYTES || millis() - waitingSince > 300;
    }

    size_t samples = 0;
    if (started) {
      samples = xStreamBufferReceive(playBuffer, mono, sizeof(mono), 0) / 2;
      if (samples == 0) {
        started = false;
        waitingSince = 0;
      }
    }

    float sum = 0;
    for (size_t i = 0; i < FRAME; i++) {
      int16_t value = i < samples ? mono[i] : 0;
      stereo[2 * i] = stereo[2 * i + 1] = value;
      sum += (float)value * value;
    }
    playing = started;
    level = min(1.0f, sqrtf(sum / FRAME) / 6000.0f);

    size_t written;  // Blocks until there is room, which paces the loop.
    i2s_channel_write(txChannel, stereo, sizeof(stereo), &written, 100);
  }
  playTask = nullptr;
  vTaskDelete(nullptr);
}

// Reads TDM frames and feeds (mic, reference) pairs to the AFE, as AudioService::ReadAudioData.
void recordLoop(void *) {
  const int chunk = afe->get_feed_chunksize(afeData);  // Samples per channel
  int16_t *tdm = (int16_t *)heap_caps_malloc(chunk * TDM_SLOTS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  int16_t *feed = (int16_t *)heap_caps_malloc(chunk * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);

  while (running && tdm && feed) {
    size_t bytes = 0;
    if (i2s_channel_read(rxChannel, tdm, chunk * TDM_SLOTS * sizeof(int16_t), &bytes, 100) != ESP_OK) continue;
    for (int i = 0; i < chunk; i++) {
      feed[2 * i] = tdm[TDM_SLOTS * i];          // Slot 0: microphone
      feed[2 * i + 1] = tdm[TDM_SLOTS * i + 1];  // Slot 1: speaker reference
    }
    afe->feed(afeData, feed);
  }
  heap_caps_free(tdm);
  heap_caps_free(feed);
  recordTask = nullptr;
  vTaskDelete(nullptr);
}

void afeLoop(void *) {
  while (running) {
    afe_fetch_result_t *result = afe->fetch_with_delay(afeData, pdMS_TO_TICKS(100));
    if (!result || result->ret_value == ESP_FAIL || !result->data) continue;
    xStreamBufferSend(micBuffer, result->data, result->data_size, 0);
  }
  afeTask = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace

bool audioBegin() {
  playBuffer = xStreamBufferCreateWithCaps(PLAY_BUFFER, 1, MALLOC_CAP_SPIRAM);
  micBuffer = xStreamBufferCreateWithCaps(MIC_BUFFER, 1, MALLOC_CAP_SPIRAM);
  if (!playBuffer || !micBuffer || !afeBegin()) return false;

  // Start I2S first: both codecs need MCLK before they are configured.
  if (!i2sBegin()) return false;
  delay(10);
  bool speakerOk = es8311Begin();
  bool micOk = es7210Begin();
  pinMode(PIN_SPEAKER_PA, OUTPUT);
  digitalWrite(PIN_SPEAKER_PA, HIGH);

  running = true;
  flushRequested = false;
  playing = false;
  xTaskCreatePinnedToCore(playLoop, "play", 4096, nullptr, 6, &playTask, 1);
  xTaskCreatePinnedToCore(recordLoop, "record", 4096, nullptr, 6, &recordTask, 1);
  xTaskCreatePinnedToCore(afeLoop, "afe", 4096, nullptr, 3, &afeTask, 0);
  return speakerOk && micOk;
}

void audioEnd() {
  running = false;
  for (int i = 0; i < 50 && (playTask || recordTask || afeTask); i++) delay(10);
  digitalWrite(PIN_SPEAKER_PA, LOW);
  es8311End();
  es7210End();
  i2sEnd();
  if (afeData) afe->destroy(afeData);
  afeData = nullptr;
  if (playBuffer) vStreamBufferDeleteWithCaps(playBuffer);
  if (micBuffer) vStreamBufferDeleteWithCaps(micBuffer);
  playBuffer = micBuffer = nullptr;
}

bool audioPlay(const uint8_t *pcm, size_t bytes) {
  return playBuffer && xStreamBufferSend(playBuffer, pcm, bytes, 0) == bytes;
}

void audioFlush() { flushRequested = true; }

bool audioPlaying() { return playing || (playBuffer && !xStreamBufferIsEmpty(playBuffer)); }

float audioLevel() { return playing ? level : 0.0f; }

size_t audioRecord(uint8_t *pcm, size_t bytes) {
  return micBuffer ? xStreamBufferReceive(micBuffer, pcm, bytes, 0) : 0;
}
