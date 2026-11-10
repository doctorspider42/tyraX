"""game/mute.py ARM - mute TYRA_LOG and the engine's periodic printf
(FRAMEARENA/NATIVEVIF/...) for frames 200..1299 (the KF window) so the run's
own telemetry does not cost frames; the KF dump unmutes."""
import sys
p = sys.argv[1] + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'void TerrainGame::loop() {\n'
assert s.count(a) == 1
s = s.replace(a, a + '  TyraProbe::muteLog = g_frameStamp >= 200 && g_frameStamp < 1300;\n'
                     '  Tyra::FrameProfile::probeMuteEngineLog = TyraProbe::muteLog;\n')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('mute ok')
