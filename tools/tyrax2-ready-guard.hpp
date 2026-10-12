#pragma once
namespace Tyra {
struct ReadyRecordingSuspend {
  bool &recording,&protect,&hud;
  unsigned &pending;
  bool oldRecording,oldProtect,oldHud;
  unsigned oldPending;
  ReadyRecordingSuspend(bool& r,bool& p,bool& h,unsigned& s)
      :recording(r),protect(p),hud(h),pending(s),
       oldRecording(r),oldProtect(p),oldHud(h),oldPending(s){
    recording=false;protect=true;hud=false;pending=0;
  }
  ~ReadyRecordingSuspend(){
    pending=oldPending;hud=oldHud;protect=oldProtect;recording=oldRecording;
  }
  ReadyRecordingSuspend(const ReadyRecordingSuspend&)=delete;
  ReadyRecordingSuspend& operator=(const ReadyRecordingSuspend&)=delete;
};
}
