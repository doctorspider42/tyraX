#pragma once
#include <tyra>
#include "debug/audio_idle.hpp"
#include "thread/threading.hpp"
namespace AudioPcmLife {
inline unsigned ticks(){unsigned value;asm volatile("mfc0 %0,$9":"=r"(value));return value;}
inline void pause(){const timespec delay={0,1000000};Tyra::Threading::sleep(delay);}
inline unsigned waiters(){ee_sema_t state{};TYRA_ASSERT(ReferSemaStatus(AudioIdle::wake,&state)>=0,"PCM semaphore status failed");return state.wait_threads;}
template<class Predicate> inline void until(Predicate predicate){
  const unsigned start=ticks();
  while(!predicate()){TYRA_ASSERT(ticks()-start<294912000U*3U,"PCM lifecycle timeout");pause();}
}
struct Listener:Tyra::AudioListener {
  volatile unsigned feeds=0,first=0;
  void onAudioTick() override {if(!first)first=ticks();++feeds;}
  void onAudioFinish() override {}
};
inline Listener listener;
inline void run(Tyra::AudioSong& song){
  TYRA_ASSERT(AudioIdle::wake>=0,"PCM wake gate not initialized");
  song.load("host:pcm-event.wav");TYRA_ASSERT(song.isLoaded(),"PCM test WAV failed to load");
  song.inLoop=true;song.setVolume(20);
  const auto id=song.addListener(&listener);
  std::printf("PCMLIFEBEGIN rounds=6 wav=pcm-event.wav\n");std::fflush(stdout);
  for(unsigned round=0;round<6;++round){
    const bool mode=round>=3;AudioIdle::set(mode);song.stop();
    const unsigned idleStart=ticks();
    until([&]{return !song.isPlaying() && (mode?AudioIdle::waiting && waiters()==1:!AudioIdle::waiting && waiters()==0) && ticks()-idleStart>29491200U;});
    const unsigned before=listener.feeds;listener.first=0;const unsigned start=ticks();
    song.play();
    until([&]{return listener.feeds-before>=3 && song.isPlaying() && !AudioIdle::waiting && waiters()==0;});
    const unsigned fed=listener.feeds-before,first=listener.first-start;
    song.stop();
    until([&]{return !song.isPlaying() && (mode?AudioIdle::waiting && waiters()==1:!AudioIdle::waiting && waiters()==0);});
    std::printf("PCMLIFE round=%u event=%u feeds=%u firstTicks=%u stopped=1 waiting=%u waiters=%u starts=%u\n",
      round,unsigned(mode),fed,first,unsigned(AudioIdle::waiting),waiters(),unsigned(AudioIdle::pcmStarts));std::fflush(stdout);
  }
  AudioIdle::set(true);
  until([&]{return AudioIdle::waiting && waiters()==1;});
  song.removeListener(id);
  std::printf("PCMLIFEDONE valid=1 rounds=6 starts=%u waiter=1\n",unsigned(AudioIdle::pcmStarts));std::fflush(stdout);
}
}
