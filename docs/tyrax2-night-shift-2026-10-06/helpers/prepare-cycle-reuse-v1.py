from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');base=b/'paused-clock-physical-v1';out=b/'cycle-reuse-physical-v1';assert not out.exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((base/'target-source-manifest.json').read_text())
for n,d in m['files'].items():
 assert sha(base/n)==d;p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/n,p)
for n in ('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n)
def edit(n,old,new):
 p=out/n;s=p.read_text(encoding='utf8');assert s.count(old)==1,(n,old);p.write_bytes(s.replace(old,new).encode())
for n in ('night_plan.hpp','night_runtime.hpp'):
 p=out/'tyra/engine/inc/debug'/n;p.write_bytes(p.read_text().replace('kind==20','kind==21').encode())
n='game/src/scripts/district_mood.cpp';edit(n,'daynight::setPaused(NightAblation::producerEnabled);','daynight::setPaused(true);')
n='game/inc/daynight.gen.hpp'
helpers='''// PRIVATE exact evaluation-output reuse. Every mutable output is restored.
struct CycleState { float v[37]; };
inline CycleState cycleSaved{};
inline unsigned cycleInputBits=0;
inline int cycleScene=-1;
inline bool cycleValid=false;
inline void cycleState(CycleState& s,bool load) {
  float* a[]={&g_hour,g_sun,g_moon,g_light,g_sky,g_top,g_fog,&g_stars,
    &g_sunRad,&g_moonRad,&g_shadowFade,&g_moonRoll,g_gain,g_lift,g_mixColor,
    &g_mixAmount,g_comp};
  const int sizes[]={1,3,3,3,3,3,3,1,1,1,1,1,3,3,3,1,3};
  int at=0;for(int j=0;j<17;++j)for(int i=0;i<sizes[j];++i){
    if(load)a[j][i]=s.v[at];else s.v[at]=a[j][i];++at;
  }
}
inline void cycleEvaluate(int scene,float hour) {
  unsigned bits;memcpy(&bits,&hour,4);
  const bool reuse=NightAblation::producerEnabled&&cycleValid&&scene==cycleScene&&bits==cycleInputBits;
  if(reuse)cycleState(cycleSaved,true);
  else {evaluate(scene,hour);if(NightAblation::producerEnabled){
    cycleState(cycleSaved,false);cycleScene=scene;cycleInputBits=bits;cycleValid=true;
  }}
  if(NightAblation::producerSelected&&NightAblation::collectCounters){
    CycleState actual{},reference{};cycleState(actual,false);evaluate(scene,hour);cycleState(reference,false);
    unsigned mismatches=0;for(unsigned i=0;i<37;++i)if(memcmp(actual.v+i,reference.v+i,4))++mismatches;
    cycleState(actual,true);NightAblation::producerCount(0,0);
    NightAblation::producerCount(0,1,reuse?1:0);NightAblation::producerCount(0,2,37);
    NightAblation::producerCount(0,3,mismatches);if(mismatches)NightAblation::valid=false;
    printf("LOG: NIGHTEVAL scene=%d reused=%u compared=37 mismatches=%u\\n",scene,unsigned(reuse),mismatches);fflush(stdout);
  }
}

'''
edit(n,'inline void setHour(float hour) {',helpers+'inline void setHour(float hour) {')
edit(n,'  g_paused = false;\n  if (!active(scene)) return;','  g_paused = false;cycleValid=false;\n  if (!active(scene)) return;')
edit(n,'    NightAblation::producerCount(0,0);if(g_paused)NightAblation::producerCount(0,1);if(a!=z)NightAblation::producerCount(0,2);','    // Clock witnesses are common to both always-paused arms.')
edit(n,'  evaluate(scene, g_hour);\n}', '  cycleEvaluate(scene, g_hour);\n}')
for n in m['files']:m['files'][n]=sha(out/n)
(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());original=json.loads((b/'corona24-pricing-physical-v1/target-source-manifest.json').read_text());changes=sorted(n for n,d in m['files'].items()if d!=original['files'][n]);(out/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=501,changes=changes,baseManifestSha256=sha(b/'corona24-pricing-physical-v1/target-source-manifest.json'),candidate='Exact 37-float output reuse keyed by input hour bits and scene. Full output restoration, common fixed-hour pause and sky retint; cold original-evaluate oracle only750/1155. No new clocks.',commonCodeFootprintUnpriced=True,existingWaitsRetained=True),indent=2)+'\n').encode())
for old,new in [('paused-clock-build-native-v1.ps1','cycle-reuse-build-native-v1.ps1'),('compile-paused-clock-abi-root-v1.py','compile-cycle-reuse-abi-root-v1.py'),('audit-paused-clock-native-root-v1.py','audit-cycle-reuse-native-root-v1.py')]:
 s=(b/old).read_text().replace('paused-clock-physical-v1','cycle-reuse-physical-v1').replace('night-ablation-native-v47','night-ablation-native-v48');(b/new).write_bytes(s.encode())
print('Frozen kind21 exact evaluation reuse',len(changes))
