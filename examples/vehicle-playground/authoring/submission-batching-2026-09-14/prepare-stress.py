from pathlib import Path
import shutil
r=Path(__file__).parent
src=r/'large'; dst=r/'stress'
shutil.copytree(src,dst,ignore=shutil.ignore_patterns('*.csv','*.log','*.err','livedbg.bin','hardware-trace.cfg','*.tga','obj'))
p=dst/'src/terrain_game.cpp';s=p.read_text(encoding='utf-8')
a='namespace districtMeasure {'
assert s.count(a)==1
s=s.replace(a,a+"""
struct EmptyPipeline : Tyra::Renderer3DPipeline {
  void setRenderer(Tyra::RendererCore*) override {}
  void onUse() override {}
  void onFrameEnd() override {}
  void onUseEnd() override {}
};
static EmptyPipeline emptyPipeline;
static unsigned stressEvictions=0, stressPendingFlushes=0, stressSwitches=0;
""")
a='  int impostorSwitchBudget = 4;'
assert s.count(a)==1
s=s.replace(a,'  unsigned stressDraws = 0;\n'+a)
a='        stapip.core.render(part.bag.get());'
# Only main Objects occurrence, between its markers.
start=s.index('  const u32 costObjectsStart=costStart();',s.index('void TerrainGame::renderScene()'))
end=s.index('  costEnd("Objects",-1,costObjectsStart);',start)
prefix,body,suffix=s[:start],s[start:end],s[end:]
assert body.count(a)==1
body=body.replace(a,a+"""
        if (districtMeasure::frame >= 1600 && districtMeasure::frame < 1840 &&
            ++stressDraws == 1 + (districtMeasure::frame % 12)) {
          stapip.core.takeTelemetry();
          engine->renderer.core.texture.evictAll();
          const auto flushed = stapip.core.takeTelemetry();
          ++districtMeasure::stressEvictions;
          if (flushed.submissionBatchedBags) ++districtMeasure::stressPendingFlushes;
        }
""")
a='  stapip.core.endSubmissionBatch();'
pos=body.rfind(a);assert pos>=0
body=body[:pos]+body[pos:].replace(a,a+"""
  if (districtMeasure::frame >= 1600 && districtMeasure::frame < 1840 &&
      districtMeasure::frame % 20 == 0) {
    engine->renderer.renderer3D.usePipeline(districtMeasure::emptyPipeline);
    engine->renderer.core.texture.evictAll();
    engine->renderer.renderer3D.usePipeline(stapip);
    ++districtMeasure::stressSwitches;
  }
  if (districtMeasure::frame == 1840) {
    FILE* f = fopen(Tyra::FileUtils::fromCwd("batch-stress.txt").c_str(), "w");
    if (f) { fprintf(f,"evictions=%u pending_flushes=%u pipeline_switches=%u reuploads=%u\\n",
      districtMeasure::stressEvictions,districtMeasure::stressPendingFlushes,
      districtMeasure::stressSwitches,engine->renderer.core.texture.stats.reuploads); fclose(f); }
  }
""",1)
s=prefix+body+suffix
p.write_bytes(s.encode('utf-8'))
(dst/'bin/district-benchmark-pose.txt').write_text('1',encoding='ascii')
