#pragma once

#include <stddef.h>
#include <stdint.h>

// Speaker and microphones for the voice screen. PCM16 mono at AUDIO_SAMPLE_RATE in both directions.
// Background tasks keep I2S running: one plays queued audio, one records. Recorded audio goes through
// ESP-SR's echo canceller, using the ES7210's hardware loopback of the speaker as the reference, so the
// microphone can stay open while audio plays.

bool audioBegin();
void audioEnd();

// Queue audio for playback. Returns false if the queue is full.
bool audioPlay(const uint8_t *pcm, size_t bytes);
// Stop playback and drop everything queued.
void audioFlush();
bool audioPlaying();
// Running totals of audio queued and audio played or dropped, in bytes, which wrap around. Compare
// them to know when something that was queued at a certain point is heard.
uint32_t audioQueuedBytes();
uint32_t audioPlayedBytes();
// Loudness of what is playing right now, 0..1. Used to move the mouth.
float audioLevel();

// Copy up to `bytes` of recorded audio into `pcm`. Returns the number of bytes copied.
size_t audioRecord(uint8_t *pcm, size_t bytes);
