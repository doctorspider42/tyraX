/*
# Modified by TyraX - the song player reads the WAV
# header instead of assuming 16bit/22050Hz/stereo with samples at 0x30.
# Based on the original by Sandro Sobczynski (h4570/tyra), Apache License 2.0.
#
# The original setSongFormat() hardcoded the audsrv format and
# rewindSongToStart() hardcoded fseek(0x30), so any WAV that was not exactly
# 16bit/22050Hz/stereo with a 4-byte-padded canonical header played as noise
# (files with LIST/INFO metadata chunks - most DAW/converter output - fed the
# metadata bytes straight into audsrv). This patch walks the RIFF chunks at
# load(): the fmt chunk configures audsrv (mono and stereo, 8/16 bit, any
# rate audsrv accepts - it resamples to 48kHz on the IOP) and the data chunk
# gives the real sample offset and byte count, so playback starts at the
# samples and stops before any trailing metadata.
#
# Also added: a dedicated streamer thread between the file and the audsrv
# feed. Over a network host: (ps2link dev deploys) every fread is a round
# trip with jitter; any read on the audio thread races the audsrv ring,
# which holds under ~1/9th s of audio - the fillbuf callback fires when the
# ring is nearly EMPTY, so a slow read at that moment is a guaranteed
# audible underrun. The streamer keeps a 96 KB ring topped up in the
# background (single-producer/single-consumer, EE is one core so aligned
# u32 counter writes are atomic); the audio thread only memcpys from
# memory and never blocks on the network. Repositioning (load / rewind /
# unload) hands the file to the streamer through a generation handshake -
# the streamer owns all FILE access while active.
*/

#include "audio/audio_song.hpp"
#include "debug/debug.hpp"
#include "thread/threading.hpp"
#include <tamtypes.h>
#include <kernel.h>
#include <malloc.h>
#include <cstdlib>
#include <cstring>
#include <audsrv.h>

extern void* _gp;

namespace Tyra {

const u16 AudioSong::chunkSize = 4 * 1024;

namespace {

// WAV layout of the currently loaded song. File-scope state (instead of new
// class members) keeps the public audio_song.hpp header untouched; the
// engine has a single background song, so this is safe.
u32 songDataStart = 0x30;
u32 songDataBytes = 0xFFFFFFFF;
u32 songBytesLeft = 0xFFFFFFFF;

// Streaming granularity. audsrv sizes its ring buffer to ~1/9th of the
// byte rate, so low-rate mono formats (mono 22050 = 4700 bytes) starve when
// fed the default 4 KB chunks with a 4 KB fill-buffer threshold - the
// callback never fires and playback stays silent. Small formats stream in
// smaller chunks with a matching threshold instead.
s32 songChunkBytes = 4 * 1024;

// Streamer ring (see the header comment). Producer = the streamer thread
// (owns all FILE access while active), consumer = the audio thread's
// work(). produced/consumed are monotonically increasing byte counters
// (u32 wrap-safe subtraction); ring positions are counter % kStageSize.
constexpr u32 kStageSize = 96 * 1024;
constexpr u32 kReadBlock = 16 * 1024;
char* stageBuf = nullptr;
volatile u32 stageProduced = 0;
volatile u32 stageConsumed = 0;
// No more file bytes to pull (EOF, data chunk exhausted, or stream parked).
volatile bool fileEnd = true;
// Producer-owned while streaming: data bytes not yet pulled from the file.
// 0xFFFFFFFF = unknown (legacy headerless path) - read until a short fread.
u32 fileBytesLeft = 0;
FILE* streamWav = nullptr;
volatile bool streamActive = false;
// Reposition/park handshake: the control side fills seekPos/seekBytes and
// bumps seekReqGen; the streamer performs the fseek + ring reset between
// its own freads and acks. The control side polls for the ack, so at most
// one in-flight fread of stale data delays it (and gets discarded).
volatile u32 seekReqGen = 0;
volatile u32 seekAckGen = 0;
u32 seekPos = 0;
u32 seekBytes = 0;

void streamerStep() {
  if (seekAckGen != seekReqGen) {
    if (streamWav && streamActive) fseek(streamWav, seekPos, SEEK_SET);
    stageConsumed = stageProduced;  // consumer is parked awaiting the ack
    fileBytesLeft = seekBytes;
    fileEnd = !streamActive || fileBytesLeft == 0;
    seekAckGen = seekReqGen;
    return;
  }
  if (!streamActive || fileEnd || !streamWav || !stageBuf) return;
  if (stageProduced - stageConsumed >= kStageSize - kReadBlock) return;
  const u32 wpos = stageProduced % kStageSize;
  u32 room = kStageSize - wpos;  // contiguous bytes until the ring wraps
  if (room > kReadBlock) room = kReadBlock;
  if (fileBytesLeft != 0xFFFFFFFF && room > fileBytesLeft) room = fileBytesLeft;
  const u32 got = room > 0 ? (u32)fread(stageBuf + wpos, 1, room, streamWav) : 0;
  stageProduced += got;
  if (fileBytesLeft != 0xFFFFFFFF) fileBytesLeft -= got;
  if (got < room || fileBytesLeft == 0) fileEnd = true;
}

// Parks/repositions the streamer and waits until it acknowledged - after
// this the ring is empty and the FILE is untouched by the producer.
void requestStream(FILE* f, u32 pos, u32 bytes, bool active) {
  streamWav = f;
  streamActive = active;
  seekPos = pos;
  seekBytes = bytes;
  seekReqGen = seekReqGen + 1;
  while (seekAckGen != seekReqGen) Threading::sleep(1);
}

int streamerThreadId = -1;

}  // namespace

// Thread entry (plain function pointer for CreateThread, like audioThread).
static void songStreamerThread(void*) {
  while (true) {
    Threading::sleep(2);
    streamerStep();
  }
}

namespace {

u32 readU16(const u8* p) { return p[0] | (p[1] << 8); }
u32 readU32(const u8* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

/** Walks the RIFF chunks. Returns false when the file is not a parseable
 * PCM WAV - the caller then falls back to the legacy assumptions.
 * The header is read into memory with a single sequential fread and parsed
 * there: interleaving fseek and fread on a PS2 host-filesystem FILE* is
 * unreliable (the engine's proven pattern is one absolute seek + reads). */
bool parseWavHeader(FILE* wav, audsrv_fmt_t* outFormat, u32* outDataStart,
                    u32* outDataBytes) {
  static u8 head[4096];
  fseek(wav, 0, SEEK_SET);
  const u32 avail = (u32)fread(head, 1, sizeof(head), wav);
  if (avail < 12) return false;
  if (memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0)
    return false;

  bool haveFmt = false, haveData = false;
  u32 audioFormat = 0, channels = 0, freq = 0, bits = 0;

  u32 pos = 12;
  while ((!haveFmt || !haveData) && pos + 8 <= avail) {
    const u8* hdr = head + pos;
    const u32 size = readU32(hdr + 4);
    if (memcmp(hdr, "fmt ", 4) == 0 && size >= 16 && pos + 8 + 16 <= avail) {
      const u8* fmt = hdr + 8;
      audioFormat = readU16(fmt);
      channels = readU16(fmt + 2);
      freq = readU32(fmt + 4);
      bits = readU16(fmt + 14);
      // WAVE_FORMAT_EXTENSIBLE: the real format tag leads the sub-format GUID
      if (audioFormat == 0xFFFE && size >= 40 && pos + 8 + 26 <= avail)
        audioFormat = readU16(fmt + 24);
      haveFmt = true;
    } else if (memcmp(hdr, "data", 4) == 0) {
      *outDataStart = pos + 8;
      *outDataBytes = size;
      haveData = true;
    }
    pos += 8 + size + (size & 1);
  }

  // Headers bigger than the probe window (e.g. embedded cover art before
  // the data chunk) fall back to the legacy path.
  if (!haveFmt || !haveData) return false;
  if (audioFormat != 1) return false;  // PCM only (no float/ADPCM/MP3)
  if (channels != 1 && channels != 2) return false;
  if (bits != 8 && bits != 16) return false;

  outFormat->bits = bits;
  outFormat->freq = freq;
  outFormat->channels = channels;
  return true;
}

}  // namespace

AudioSong::AudioSong() {
  chunkReadStatus = 0;
  tyraVolume = 0;
  audsrvVolume = 0;

  songLoaded = false;
  songPlaying = false;
  inLoop = false;
  songFinished = false;

  chunk = nullptr;
}

AudioSong::~AudioSong() {
  if (chunk) delete[] chunk;
}

void AudioSong::init() {
  chunk = static_cast<char*>(memalign(sizeof(char), chunkSize));
  stageBuf = static_cast<char*>(memalign(64, kStageSize));

  // The streamer thread pulls file bytes into the ring in the background
  // so work() never blocks on (network) file IO. Priority sits just below
  // the audio thread (0x5); it spends its life blocked on IO anyway.
  static u8 streamerStack[16 * 1024] __attribute__((aligned(16)));
  ee_thread_t th;
  th.gp_reg = &_gp;
  th.func = reinterpret_cast<void*>(songStreamerThread);
  th.stack = streamerStack;
  th.stack_size = sizeof(streamerStack);
  th.initial_priority = 0x6;
  streamerThreadId = CreateThread(&th);
  TYRA_ASSERT(streamerThreadId >= 0, "Create song streamer thread failed!");
  StartThread(streamerThreadId, nullptr);

  initSema();
  initAUDSRV();
  setSongFormat();
}

void AudioSong::load(const char* t_path) {
  if (songLoaded) unloadSong();
  wav = fopen(t_path, "rb");
  // Modified by TyraX: a missing music file is no longer fatal - log it
  // (the editor surfaces it) and leave songLoaded false so play() is a no-op;
  // the game runs on in silence instead of crashing.
  if (wav == nullptr) {
    TYRA_SOFT_ERROR("Failed to open music file: ", t_path,
                    " (the game keeps running without this track)");
    return;
  }

  audsrv_fmt_t fileFormat;
  u32 dataStart = 0, dataBytes = 0;
  if (parseWavHeader(wav, &fileFormat, &dataStart, &dataBytes)) {
    songDataStart = dataStart;
    songDataBytes = dataBytes;
    format = fileFormat;
    if (audsrv_set_format(&format)) {
      TYRA_ERROR("AUDSRV returned error string: ", audsrv_get_error_string());
    }
    TYRA_LOG("Song WAV: ", (int)format.freq, "Hz ", (int)format.bits, "bit ",
             (int)format.channels, "ch, data at ", (int)songDataStart);
  } else {
    // Not a parseable PCM WAV - legacy behavior (may sound wrong)
    TYRA_ERROR("Song WAV header not recognized - expecting PCM 8/16 bit, ",
               "mono/stereo. Playing with the default format.");
    songDataStart = 0x30;
    songDataBytes = 0xFFFFFFFF;
    setSongFormat();
  }

  // Keep the chunk (and the audsrv callback threshold) well below the ring
  // buffer for low-byte-rate formats, or the stream starves silently.
  const u32 bytesPerSec = format.freq * format.channels * (format.bits / 8);
  s32 wantChunk = (s32)chunkSize;
  while (wantChunk > 1024 && (u32)wantChunk * 16 > bytesPerSec) wantChunk /= 2;
  if (wantChunk != songChunkBytes) {
    songChunkBytes = wantChunk;
    const int ret = audsrv_on_fillbuf(songChunkBytes, (audsrv_callback_t)iSignalSema,
                                      (void*)fillbufferSema);
    if (ret < 0) {
      TYRA_ERROR("AUDSRV returned error string: ", audsrv_get_error_string());
    }
  }

  rewindSongToStart();
  songLoaded = true;
}

void AudioSong::load(const std::string& t_path) { load(t_path.c_str()); }

void AudioSong::play() {
  // Modified by TyraX: play() is a no-op when no song loaded (e.g. the
  // file was missing - see load()), instead of asserting and killing the game.
  if (!songLoaded) return;
  if (songFinished) rewindSongToStart();
  tyraVolume = audsrvVolume;
  audsrv_set_volume(tyraVolume);
  songPlaying = true;
}

void AudioSong::stop() {
  tyraVolume = 0;
  audsrv_set_volume(tyraVolume);
  songPlaying = false;
}

const bool& AudioSong::isPlaying() const { return songPlaying; }

const bool& AudioSong::isLoaded() const { return songLoaded; }

const u8& AudioSong::getVolume() const { return tyraVolume; }

std::size_t AudioSong::getListenersCount() const {
  return songListeners.size();
}

void AudioSong::initAUDSRV() {
  int ret = audsrv_on_fillbuf(chunkSize, (audsrv_callback_t)iSignalSema,
                              (void*)fillbufferSema);

  TYRA_ASSERT(ret >= 0,
              "AUDSRV returned error string:", audsrv_get_error_string());
}

/** Initialize semaphore which will wait until chunk of the song is not
 * finished. */
void AudioSong::initSema() {
  TYRA_LOG("Creating audio semaphore");
  sema.init_count = 0;
  sema.max_count = 1;
  sema.option = 0;
  fillbufferSema = CreateSema(&sema);
  TYRA_LOG("AudioSong semaphore created");
}

/**
 * Close file.
 * Delete song path from memory.
 */
void AudioSong::unloadSong() {
  songLoaded = false;
  // Park the streamer first - it may be mid-fread on this FILE.
  requestStream(nullptr, 0, 0, false);
  fclose(wav);
}

/** Hand the file to the streamer, positioned at the first sample. */
void AudioSong::rewindSongToStart() {
  songBytesLeft = songDataBytes;
  chunkReadStatus = 0;
  songFinished = false;
  if (wav != nullptr) requestStream(wav, songDataStart, songDataBytes, true);
}

/** Default WAV format (16bit, 22050Hz, stereo) - used until a song is
 * loaded; load() reconfigures audsrv from the file header. */
void AudioSong::setSongFormat() {
  format.bits = 16;
  format.freq = 22050;
  format.channels = 2;

  if (audsrv_set_format(&format)) {
    TYRA_ERROR("AUDSRV returned error string: ", audsrv_get_error_string());
  }
}

void AudioSong::setVolume(const u8& t_vol) {
  audsrvVolume = t_vol;
  if (songPlaying) tyraVolume = t_vol;

  if (audsrv_set_volume(tyraVolume)) {
    TYRA_ERROR("AUDSRV returned error string: ", audsrv_get_error_string());
  }
}

void AudioSong::work() {
  if (!songPlaying || !songLoaded) return;
  if (songFinished) {
    if (inLoop) {
      for (u32 i = 0; i < getListenersCount(); i++)
        songListeners[i]->listener->onAudioFinish();
      rewindSongToStart();
    } else {
      stop();
      return;
    }
  }

  if (chunkReadStatus > 0) {
    WaitSema(fillbufferSema);  // wait until previous chunk wasn't finished
    audsrv_wait_audio(chunkReadStatus);
    audsrv_play_audio(chunk, chunkReadStatus);
    for (u32 i = 0; i < getListenersCount(); i++)
      songListeners[i]->listener->onAudioTick();
  }

  // Cut the next chunk from the streamer ring - pure memcpy, no file IO on
  // this thread. Feed only up to the end of the data chunk (trailing
  // LIST/INFO/id3 metadata must not reach the speakers).
  const u32 want =
      songBytesLeft < (u32)songChunkBytes ? songBytesLeft : (u32)songChunkBytes;
  const u32 avail = stageProduced - stageConsumed;
  u32 take = want < avail ? want : avail;
  if (take > 0) {
    const u32 rpos = stageConsumed % kStageSize;
    u32 first = kStageSize - rpos;
    if (first > take) first = take;
    memcpy(chunk, stageBuf + rpos, first);
    if (take > first) memcpy(chunk + first, stageBuf, take - first);
    stageConsumed = stageConsumed + take;
    songBytesLeft -= take;
  }
  chunkReadStatus = (s32)take;
  // take == 0 with the file not exhausted = transient starvation (the
  // streamer is mid-refill): chunkReadStatus stays 0, so the next work()
  // iteration skips the callback wait and retries the pop immediately -
  // playback resumes as soon as bytes land, without a lost fillbuf signal.

  // 8-bit WAV stores unsigned samples (0x80 = silence) but audsrv mixes
  // them as signed - without this the waveform wraps at every zero
  // crossing and plays as harsh crackle.
  if (format.bits == 8 && chunkReadStatus > 0) {
    u8* samples = (u8*)chunk;
    for (s32 i = 0; i < chunkReadStatus; i++) samples[i] ^= 0x80;
  }

  // Finished only on true end-of-data - a short pop with the streamer still
  // pulling bytes is transient starvation, not the end of the song.
  if (songBytesLeft == 0 ||
      (fileEnd && stageProduced == stageConsumed && chunkReadStatus < (s32)want))
    songFinished = true;
}

u32 AudioSong::addListener(AudioListener* t_listener) {
  AudioListenerRef* ref = new AudioListenerRef;
  ref->id = rand() % 1000000;
  ref->listener = t_listener;
  songListeners.push_back(ref);
  return ref->id;
}

void AudioSong::removeListener(const u32& t_id) {
  s32 index = -1;
  for (u32 i = 0; i < songListeners.size(); i++)
    if (songListeners[i]->id == t_id) {
      index = i;
      break;
    }

  TYRA_ASSERT(index != -1,
              "Cant remove listener because given id was not found!");

  delete songListeners[index];
  songListeners.erase(songListeners.begin() + index);
}

}  // namespace Tyra
