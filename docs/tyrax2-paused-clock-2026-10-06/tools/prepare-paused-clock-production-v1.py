from pathlib import Path
import hashlib,json
b=Path('F:/Projects/tyrax2-lab-20261001');g=b/'paused-clock-production-vehicle-v1/game'
p=g/'src/terrain_game.cpp';s=p.read_text(encoding='utf8');needle='void TerrainGame::init() {\n';assert s.count(needle)==1
s=s.replace(needle,needle+'''  // Private target lifecycle qualification; never a physical price fixture.
  TYRA_ASSERT(!daynight::g_paused, "Clock must start unpaused");
  daynight::setHour(25.0F); TYRA_ASSERT(daynight::g_hour == 1.0F, "Positive wrap");
  daynight::setHour(-1.0F); TYRA_ASSERT(daynight::g_hour == 23.0F, "Negative wrap");
  daynight::reset(0); TYRA_ASSERT(!daynight::g_paused, "Active reset clears pause");
  daynight::setHour(0.0F); daynight::setPaused(true);
  daynight::g_sky[0] = -123.0F; daynight::tick(0, 0.033F);
  TYRA_ASSERT(daynight::g_hour == 0.0F && daynight::g_sky[0] != -123.0F, "Pause preserves evaluation");
  daynight::setPaused(false); daynight::tick(0, 0.033F);
  TYRA_ASSERT(daynight::g_hour > 0.0F, "Resume advances");
  daynight::setPaused(true); daynight::reset(-1);
  TYRA_ASSERT(!daynight::g_paused, "Inactive reset clears pause");
  daynight::reset(0);
  TYRA_LOG("CLOCKAPI boot-contract=PASS");
''')
needle='  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);';assert s.count(needle)==1
s=s.replace(needle,needle+'''
  static unsigned clockFrame = 0;
  if (bootPhase == 2 && !menuActive && loadingFrames == 0) {
    ++clockFrame;
    if (clockFrame == 120 || clockFrame == 540 || clockFrame == 960) saveValues[0] = 1.0F;
    if (clockFrame == 300 || clockFrame == 720 || clockFrame == 1140) saveValues[0] = 0.0F;
    if (clockFrame == 420) scriptCtx.requestScene = 1;
    if (clockFrame == 840) scriptCtx.requestScene = 2;
    if (clockFrame == 1260) scriptCtx.requestScene = 0;
    if (clockFrame == 1440) TYRA_LOG("CLOCKAPI motion=COMPLETE");
  }
''');p.write_bytes(s.encode())
p=g/'src/gen/game_scene.gen.cpp';s=p.read_text(encoding='utf8');needle='  daynight::tick(currentScene, g_frameDt);';assert s.count(needle)==1
s=s.replace(needle,needle+'''
  static unsigned clockChecks = 0;
  ++clockChecks;
  const bool clockNight = saveValues[0] >= 0.5F;
  TYRA_ASSERT(daynight::g_paused && daynight::g_hour == (clockNight ? 0.0F : 12.0F), "Fixed mood hour/pause drift");
  TYRA_ASSERT(g_frameDt > 0.0F, "Physics dt must remain positive");
  if (clockChecks % 60 == 0) TYRA_LOG("CLOCKPOLICY scene=", currentScene, " night=", clockNight, " hour=", daynight::g_hour, " paused=", daynight::g_paused, " dt=", g_frameDt);
''');p.write_bytes(s.encode())
files={str(p.relative_to(g)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('src','inc') for p in sorted((g/folder).rglob('*')) if p.is_file()}
(b/'paused-clock-production-v1-source-manifest.json').write_text(json.dumps(files,indent=2)+'\n',encoding='utf8')
print('Private lifecycle instrumentation prepared',len(files))
