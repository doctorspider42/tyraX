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
 * SPU2 hardware reverb.
 *
 * The SPU2 has ONE reverb unit per core and TWO cores, so there are exactly
 * two reverb buses - and which one a sound is heard through is decided by
 * which core its VOICE lives on, not by anything this class does. The forked
 * audsrv numbers ADPCM channels 0-23 on core 1 and 24-47 on core 0
 * (AUDSRV_ADPCM_CH_CORE), so `Bus` below is the same split seen from here.
 *
 * That is what a room CROSS-FADE needs: two rooms can be live at once, each on
 * its own bus, and the game moves new sounds to the other bus and ramps the
 * two depths past each other. Sounds already playing keep the room they
 * started in until they end, which is also what a real room does.
 *
 * Changing the DEPTH is smooth and cheap; changing the PRESET zeroes a work
 * area of up to 96 KB in SPU2 RAM and is audible unless the depth is at 0.
 *
 * Everything here is a register poke on the IOP, reached over the ps2snd RPC
 * (libsd). The mixing itself costs the EE nothing.
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

  /**
   * Which reverb unit. A = SPU2 core 1 (ADPCM channels 0-23, the music, and
   * everything that existed before the fork); B = core 0 (channels 24-47).
   */
  enum Bus {
    BusA = 0,
    BusB = 1,
    BusCount = 2,
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
   * Turn both cores' effect bits on. Run AFTER audsrv init (audsrv's own
   * sceSdInit() resets the core attributes, including those bits).
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
   * Select the reverb algorithm on one bus. Zeroes that unit's work area, so
   * call it with the bus's depth at 0 unless a click is acceptable.
   */
  void setPreset(const Bus& t_bus, const Preset& t_preset);

  /**
   * Wet return level of one bus, per side. 0 = dry, 0x7FFF = full.
   * Cheap enough to write every frame - this is what a room transition
   * should ramp, and what a cross-fade ramps in opposite directions.
   */
  void setDepth(const Bus& t_bus, const s16& t_left, const s16& t_right);

  /**
   * Echo/delay time, 0-127. Only the Echo and Delay presets read it;
   * applying it re-sends the preset, so do not call it per frame.
   */
  void setDelay(const Bus& t_bus, const u8& t_delay);

  /** Echo feedback, 0-127. Same caveat as setDelay(). */
  void setFeedback(const Bus& t_bus, const u8& t_feedback);

  /**
   * Route one ADPCM channel into its bus's reverb. The send is a single bit
   * per voice in hardware - there is no per-voice wet amount, only the bus's
   * depth - and the bus is implied by the channel: 0-23 feed A, 24-47 feed B.
   * @param t_ch Channel (0-47).
   */
  void setChannelSend(const s8& t_ch, const bool& t_on);

  /** Route a whole bus's 24 channels at once (bit N = that bus's voice N). */
  void setSendMask(const Bus& t_bus, const u32& t_mask);

  /**
   * Whether the streamed music is sent to a reverb. The music is a core 1
   * input, so only BusA can carry it. Default: no.
   */
  void setMusicSend(const bool& t_on);

  /** The bus an ADPCM channel plays through. Mirrors the fork's numbering. */
  static Bus busOfChannel(const s8& t_ch) { return t_ch < 24 ? BusA : BusB; }

  const Preset& getPreset(const Bus& t_bus) const { return preset[t_bus]; }

 private:
  bool isAvailable;
  bool isEnabled;
  Preset preset[BusCount];
  u8 delay[BusCount];
  u8 feedback[BusCount];
  u32 sendMask[BusCount];
  bool musicSend;

  void applyPreset(const Bus& t_bus, const bool& t_clearWorkArea);
  void applySendMask(const Bus& t_bus);
};

}  // namespace Tyra
