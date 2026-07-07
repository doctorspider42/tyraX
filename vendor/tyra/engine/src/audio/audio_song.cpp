/*
# Modified by tyra-editor - the song player reads the WAV
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
*/

#include "audio/audio_song.hpp"
#include "debug/debug.hpp"
#include <tamtypes.h>
#include <malloc.h>
#include <cstdlib>
#include <cstring>
#include <audsrv.h>

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

  initSema();
  initAUDSRV();
  setSongFormat();
}

void AudioSong::load(const char* t_path) {
  if (songLoaded) unloadSong();
  wav = fopen(t_path, "rb");
  TYRA_ASSERT(wav != nullptr, "Failed to open wav file!");

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
  TYRA_ASSERT(songLoaded, "Cant play song because was not loaded!");
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
  fclose(wav);
}

/** Fseek to the first sample of the data chunk. */
void AudioSong::rewindSongToStart() {
  if (wav != nullptr) fseek(wav, songDataStart, SEEK_SET);
  songBytesLeft = songDataBytes;
  chunkReadStatus = 0;
  songFinished = false;
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

  // Read only up to the end of the data chunk - trailing metadata chunks
  // (LIST/INFO/id3) must not be fed to the speakers.
  const u32 want =
      songBytesLeft < (u32)songChunkBytes ? songBytesLeft : (u32)songChunkBytes;
  chunkReadStatus = want > 0 ? fread(chunk, 1, want, wav) : 0;
  if (chunkReadStatus > 0) songBytesLeft -= (u32)chunkReadStatus;

  // 8-bit WAV stores unsigned samples (0x80 = silence) but audsrv mixes
  // them as signed - without this the waveform wraps at every zero
  // crossing and plays as harsh crackle.
  if (format.bits == 8 && chunkReadStatus > 0) {
    u8* samples = (u8*)chunk;
    for (s32 i = 0; i < chunkReadStatus; i++) samples[i] ^= 0x80;
  }

  if (chunkReadStatus < (s32)want || songBytesLeft == 0) songFinished = true;
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
