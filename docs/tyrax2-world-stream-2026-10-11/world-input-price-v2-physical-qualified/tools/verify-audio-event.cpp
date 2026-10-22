#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
struct ee_sema_t {int count=0,max_count=0,init_count=0,wait_threads=0;unsigned attr=0,option=0;};
static std::mutex lock;
static std::condition_variable changed;
static int tokens=0,waiters=0;
int CreateSema(ee_sema_t* s){assert(s->max_count==1 && s->init_count==0);return 7;}
int SignalSema(int id){assert(id==7);std::lock_guard<std::mutex> g(lock);if(tokens)return -1;tokens=1;changed.notify_all();return 0;}
int WaitSema(int id){
  assert(id==7);std::unique_lock<std::mutex> g(lock);++waiters;changed.notify_all();
  changed.wait(g,[]{return tokens!=0;});--waiters;tokens=0;changed.notify_all();return 0;
}
int ReferSemaStatus(int id,ee_sema_t* s){assert(id==7);std::lock_guard<std::mutex> g(lock);s->wait_threads=waiters;s->count=tokens;return 0;}
#define TYRA_ASSERT(condition,...) assert(condition)
#define AUDIO_EVENT_HOST
#include "tyrax2-audio-event.hpp"
static void blocked(){
  std::unique_lock<std::mutex> g(lock);
  assert(changed.wait_for(g,std::chrono::seconds(2),[]{return waiters==1;}));
}
int main(){
  AudioIdle::init();AudioIdle::init();
  unsigned checks=0;
  for(unsigned i=0;i<64;++i){
    AudioIdle::notify();AudioIdle::notify();AudioIdle::waitEmpty();
    {std::lock_guard<std::mutex> g(lock);assert(tokens==0 && waiters==0);++checks;}
    auto task=std::async(std::launch::async,[]{AudioIdle::waitEmpty();});
    blocked();AudioIdle::notify();
    assert(task.wait_for(std::chrono::seconds(2))==std::future_status::ready);task.get();
    assert(!AudioIdle::waiting);++checks;
    AudioIdle::set(true);AudioIdle::waitEmpty();
    task=std::async(std::launch::async,[]{AudioIdle::waitEmpty();});blocked();
    AudioIdle::set(false);
    assert(task.wait_for(std::chrono::seconds(2))==std::future_status::ready);task.get();
    assert(!AudioIdle::enabled && !AudioIdle::waiting);++checks;
    std::atomic<bool> playing{false};
    task=std::async(std::launch::async,[&]{AudioIdle::waitEmpty();assert(playing);});
    blocked();playing=true;AudioIdle::notify();
    assert(task.wait_for(std::chrono::seconds(2))==std::future_status::ready);task.get();++checks;
  }
  std::printf("AUDIOEVENTHOST checks=%u rounds=64 kernel=mock actualHeader=1\n",checks);
}
