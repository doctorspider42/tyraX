/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022-2022, Tyra - https://github.com/h4570/tyrav2
# Licensed under Apache License 2.0
# Added by TyraX: SPU2 hardware reverb for sound effects.
*/

#pragma once

#include <tamtypes.h>

namespace Tyra {

/**
 * SPU2 hardware reverb, on the core the sound effects play on.
 *
 * The SPU2 has ONE reverb unit per core and audsrv puts every ADPCM voice
 * (and the streamed music) on core 1 - so this is a single global effect
 * bus with a per-voice on/off send, not a per-sound effect. Changing the
 * DEPTH is smooth and free; changing the PRESET zeroes a work area of up to
 * 96 KB in SPU2 RAM and is audible if the depth is not at 0 first.
 *
 * Everything here is a register poke on the IOP, reached over the ps2snd
 * RPC (libsd). The mixing itself costs the EE nothing.
 */
class AudioReverb {
 public:
  /** Reverb presets. Values ARE libsd's SD_EFFECT_MODE_*, do not renumber. */
  enum Preset {
    Off = 0,
    Room = 1,
    Studio1 = 2,
    Studio2 = 3,
    Studio3 = 4,
    Hall = 5,
    Space = 6,
    Echo = 7,
    Delay = 8,
    Pipe = 9,
  };

  AudioReverb();
  ~AudioReverb();

  /**
   * Bind the libsd RPC.
   *
   * MUST run BEFORE audsrv is initialized: this ends in sceSdInit(), which
   * clears libsd's transfer callbacks - the ones audsrv's streaming ring
   * installs. Calling it afterwards silences the music.
   */
  void init();

  /**
   * Turn the core's effect bit on. Run AFTER audsrv init (audsrv's own
   * sceSdInit() resets the core attributes, including that bit).
   *
   * The order also matters for a reason that is easy to miss: libsd only
   * clears the reverb work area when effects were ALREADY enabled at the
   * moment the preset is set, so a first setPreset() with the bit still off
   * leaves whatever was in SPU2 RAM circulating as noise.
   */
  void enable();

  /** False when the RPC never came up; every setter is then a no-op. */
  const bool& available() const { return isAvailable; }

  /**
   * Select the reverb algorithm. Zeroes the work area, so call it with the
   * depth already ramped to 0 unless a click is acceptable.
   * @param t_preset One of the Preset values.
   */
  void setPreset(const Preset& t_preset);

  /**
   * Wet return level, per side. 0 = dry, 0x7FFF = full.
   * Cheap enough to write every frame - this is what a room transition
   * should ramp.
   */
  void setDepth(const s16& t_left, const s16& t_right);

  /**
   * Echo/delay time, 0-127. Only the Echo, Delay and Pipe presets read it;
   * applying it re-sends the preset, so do not call it per frame.
   */
  void setDelay(const u8& t_delay);

  /** Echo feedback, 0-127. Same caveat as setDelay(). */
  void setFeedback(const u8& t_feedback);

  /**
   * Route one ADPCM channel into the reverb. The send is a single bit per
   * voice in hardware - there is no per-voice wet amount, only the global
   * depth.
   * @param t_ch Channel (0-23).
   */
  void setChannelSend(const s8& t_ch, const bool& t_on);

  /** Route all 24 channels at once (bit N = channel N). */
  void setSendMask(const u32& t_mask);

  /** Whether the streamed music is sent to the reverb too. Default: no. */
  void setMusicSend(const bool& t_on);

  const Preset& getPreset() const { return preset; }
  const u32& getSendMask() const { return sendMask; }

 private:
  bool isAvailable;
  bool isEnabled;
  Preset preset;
  u8 delay;
  u8 feedback;
  u32 sendMask;
  bool musicSend;

  void applyPreset(const bool& t_clearWorkArea);
  void applySendMask();
};

}  // namespace Tyra
