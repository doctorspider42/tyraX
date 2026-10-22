"""Prepare an isolated ordinary-clock quiet FPP fixture; no build or device actions."""
import argparse,hashlib,json,re,shutil,subprocess
from pathlib import Path

SUPPORT=Path(__file__).resolve().parent/'tyrax2-quiet'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def once(text,needle,replacement):
    if text.count(needle)!=1:raise ValueError('Expected one source anchor: '+needle)
    return text.replace(needle,replacement)
def body_span(text,method):
    starts=list(re.finditer(r'^\s*(?:void|bool)\s+'+re.escape(method)+r'\s*\(',text,re.M))
    if len(starts)!=1:raise ValueError('Expected one function definition: '+method)
    start=text.index('{',starts[0].end())
    masked=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m[0]),text)
    depth=0
    for i in range(start,len(masked)):
        if masked[i]=='{':depth+=1
        elif masked[i]=='}':
            depth-=1
            if depth==0:return start,i+1
    raise ValueError('Unclosed function: '+method)
def present_hooks(text):
    if 'QuietCadence::onPresent' in text:raise ValueError('Presentation hooks already present')
    text=once(text,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/quiet_cadence.hpp"')
    stamp='__asm__ volatile("mfc0 %0, $9" : "=r"(t1));'
    for method,kind,owner,reads in [('completePipelineFrame','Pipeline','pipelineSequence, pipelineContext',1),('endFrame','Synchronous','0, recordingContext',2),('presentWarpFrame','Synthetic','0, privateQuietContext',1)]:
        first,last=body_span(text,'RendererCore::'+method);body=text[first:last]
        flip='gs.flipBuffers(isFrameLimitOn, /*synthetic=*/true);' if kind=='Synthetic' else 'gs.flipBuffers(isFrameLimitOn);'
        if body.count(flip)!=1 or body.count(stamp)!=reads:raise ValueError('Changed flip/clock ownership: '+method)
        if kind=='Synthetic':body=once(body,flip,'const u32 privateQuietContext = gs.getRecordingContext();\n    '+flip)
        pos=body.rindex(stamp);body=body[:pos]+body[pos:].replace(stamp,stamp+'\n    QuietCadence::onPresent(t1, QuietCadence::PresentKind::'+kind+', '+owner+');',1)
        text=text[:first]+body+text[last:]
    return text
def engine_hooks(text):
    if 'QuietRuntime::begin' in text:raise ValueError('Engine hooks already present')
    text=once(text,'#include "engine.hpp"','#include "engine.hpp"\n#include "debug/quiet_runtime.hpp"')
    first,last=body_span(text,'Engine::realLoop');body=text[first:last]
    if body.count('HardwareTrace::beginFrame();')!=1 or body.count('HardwareTrace::endFrame();')!=1:raise ValueError('Changed engine-loop boundaries')
    body=once(body,'{','{\n  QuietRuntime::begin(renderer.core.getStallTotal());') if body.count('{')==1 else body[:1]+'\n  QuietRuntime::begin(renderer.core.getStallTotal());'+body[1:]
    body=once(body,'  HardwareTrace::endFrame();','  HardwareTrace::endFrame();\n  QuietRuntime::afterLoop(renderer.core.getStallTotal());')
    return text[:first]+body+text[last:]
CONTEXT='''
  // Private sparse ordinary-state context, outside measured/export windows.
  const unsigned qf=QuietCadence::frame,qo=qf%1800;
  if(qf<5400&&(qo==750||qo==1155)) {
    const auto& s=engine->renderer.core.getSettings();unsigned db,cb;memcpy(&db,&g_frameDt,4);memcpy(&cb,&g_vuClock,4);
    printf("LOG: QUIETCONTEXT phase=%u offset=%u scene=%d night=0 video=%u display=%u depth=%u width=%u height=%u refreshMilliHz=%u requested=%u dtBits=%08x clockBits=%08x rasterWidth=%u rasterHeight=%u scaleX=%d scaleY=%d ilChoice=%u ilProbing=%u ilBlock=%d ilFrame=%d ilAccepted=%d ilPipelined=%u frameYield=%u\\n",qf/1800,qo,g_activeScene,unsigned(s.getVideoMode()),unsigned(s.getDisplayMode()),unsigned(s.getColorDepth()),unsigned(s.getWidth()),unsigned(s.getHeight()),unsigned(s.getRefreshRate()*1000.0F+0.5F),unsigned(engine->renderer.core.getFramePipeline()),db,cb,s.getRasterWidthUI(),s.getRasterHeightUI(),s.getRasterScaleX(),s.getRasterScaleY(),unsigned(ilChoice),unsigned(ilProbing),ilBlock,ilFrame,ilAccepted,unsigned(ilPipelined),unsigned(engine->renderer.core.getFrameYield()));fflush(stdout);
  }
'''
def game_hooks(text):
    if 'QuietRuntime::init' in text:raise ValueError('Game hooks already present')
    text=once(text,'#include "game_runtime.gen.hpp"','#include "game_runtime.gen.hpp"\n#include "debug/quiet_runtime.hpp"\n#include <string.h>')
    text=once(text,'void TerrainGame::init() {','void TerrainGame::init() {\n  QuietRuntime::init("host:quiet-control.cfg", "host:quiet-cadence.log");')
    text=once(text,'  buildScene();','''  buildScene();
  const auto& qs=engine->renderer.core.getSettings();
  QuietRuntime::config(unsigned(qs.getVideoMode()),unsigned(qs.getDisplayMode()),unsigned(qs.getColorDepth()),unsigned(qs.getWidth()),unsigned(qs.getHeight()),unsigned(qs.getRefreshRate()*1000.0F+0.5F),unsigned(engine->renderer.core.getFramePipeline()),0,qs.getRasterWidthUI(),qs.getRasterHeightUI(),unsigned(qs.getRasterScaleX()),unsigned(qs.getRasterScaleY()));
  printf("LOG: QUIETWORKLOAD model=firstPerson nightKnown=0 authoredSaveValuesUnchanged=1 scriptModeOpaque=1\\n");
''')
    return once(text,'  updateFrameClock();  // real dt: frame drops slow the picture, not the game','  updateFrameClock();  // real dt: frame drops slow the picture, not the game'+CONTEXT)
def patch(path,transform):path.write_text(transform(path.read_text(encoding='utf-8-sig')),encoding='utf8',newline='\n')
def switch(path,name):
    def transform(text):
        matches=re.findall(r'^#define '+name+r' ([0-9]+)$',text,re.M)
        if len(matches)!=1:raise ValueError('Changed profiler definition '+name)
        return once(text,'#define '+name+' '+matches[0],'#define '+name+' 0')
    patch(path,transform)
def ignore(path,names):return [n for n in names if n.lower() in ('.git','obj','logs','screenshots','preview','build','cmakefiles') or n.lower().endswith(('.elf','.sym','.o','.a','.log','.csv')) or n.lower() in ('hardware-trace.cfg','ps2link.run','live-pad.dat','quiet-control.cfg','quiet-night.cfg')]
def validate_vu_framework(out):
    # Match native-build.sh: direct, lowercase *.cpp under src/vu or src/vu0.
    # Empty/name-only/nested source directories do not activate this recipe.
    game=out/'game';selected=[]
    for rel in ('src/vu','src/vu0'):
        tree=game/rel
        if tree.is_dir():selected.extend(p for p in tree.iterdir() if p.is_file() and p.suffix=='.cpp')
    if not selected:return
    framework=game/'vugen'
    implementation=list(framework.glob('*.cpp')) if framework.is_dir() else []
    implementation=[p for p in implementation if p.is_file() and p.suffix=='.cpp']
    if not implementation:raise ValueError('Active host VU recipe requires game/vugen/*.cpp')
    # Generated framework sources use literal local includes. Resolve the same
    # source-local and -I game/vugen paths before accepting this supported recipe.
    pending=selected+implementation;seen=set()
    while pending:
        source=pending.pop().resolve()
        if source in seen:continue
        seen.add(source)
        text=source.read_text(encoding='utf-8-sig')
        text=re.sub(r'/\*[\s\S]*?\*/|//[^\n]*','',text)
        for include in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"',text,re.M):
            candidates=(source.parent/include,framework/include)
            dependency=next((p.resolve() for p in candidates if p.is_file()),None)
            if dependency is None:raise ValueError('Missing host VU include '+include+' from '+str(source))
            roots=(game/'src',game/'inc',framework)
            if not any(dependency.is_relative_to(root.resolve()) for root in roots):raise ValueError('Host VU include outside source-manifest trees '+str(dependency))
            pending.append(dependency)

def source_inputs(out,manifest_name):
    # Hash every file under compiler/source/framework trees, independent of
    # extension: Makefile.base consumes .irx-em as well as VU/C++ inputs.
    # Keep generated obj/bin outputs outside this source identity.
    validate_vu_framework(out)
    required=('tyra/engine/src','tyra/engine/inc','game/src','game/inc')
    for rel in required:
        if not (out/rel).is_dir():raise ValueError('Missing source tree '+rel)
    paths=set()
    for rel in (*required,'tyra/engine/res','game/vugen'):
        tree=out/rel
        if tree.exists():
            paths.update(p for p in tree.rglob('*') if p.is_file() and not any(part.lower() in ('obj','bin','__pycache__') for part in p.relative_to(tree).parts))
    # Root build recipes/helpers and project data may participate in generation.
    for rel in ('tyra','tyra/engine','game'):
        paths.update(p for p in (out/rel).iterdir() if p.is_file())
    for rel in ('tyra/engine/Makefile','tyra/Makefile.base','game/Makefile','game/'+manifest_name):
        if not (out/rel).is_file():raise ValueError('Missing build input '+rel)
        paths.add(out/rel)
    return {str(p.relative_to(out)).replace('\\','/'):sha(p) for p in sorted(paths)}

def prepare(project,engine,editor,out,order,disable_control_apparatus=False):
    if not all(p.is_absolute() for p in (project,engine,editor,out)):raise ValueError('All input/output paths must be absolute')
    project=project.resolve();engine=engine.resolve();editor=editor.resolve();out=out.resolve()
    if out.exists():raise ValueError('New destination required')
    if not editor.is_file() or not (engine/'engine/Makefile').is_file():raise ValueError('Editor executable and complete Tyra engine required')
    manifests=[project] if project.is_file() else list(project.glob('*.tyra'))
    if len(manifests)!=1:raise ValueError('Exactly one .tyra project required')
    manifest=manifests[0];project=manifest.parent
    if any(out.is_relative_to(x) or x.is_relative_to(out) for x in (project,engine)):raise ValueError('Destination must be separate from sources')
    model=json.loads(manifest.read_text(encoding='utf-8-sig'))
    if model.get('template')!='fpp':raise ValueError('Only generated FPP TerrainGame projects are supported')
    original=model['settings'].copy();save_values=model.get('saveValues',[])
    for k in ('showFps','showMemory','showProfiler','liveLink','liveDebug','liveLogic','timeMachine'):model['settings'][k]=False
    if disable_control_apparatus:
        for k in ('remotePad','inputRecorder'):model['settings'][k]=False
    for k in ('liveLinkPollFrames','liveLogicPollFrames','liveDebugPollFrames','liveDebugSnapshotFrames','timeMachineFrames'):model['settings'][k]=0
    out.mkdir(parents=True)
    try:
        shutil.copytree(project,out/'game',ignore=ignore);shutil.copytree(engine,out/'tyra',ignore=ignore)
        (out/'game'/manifest.name).write_text(json.dumps(model,indent=2)+'\n',encoding='utf8')
        r=subprocess.run([str(editor),'--refresh-gen',str(out/'game')],capture_output=True,text=True);(out/'refresh-generation.log').write_text(r.stdout+r.stderr,encoding='utf8')
        if r.returncode:raise ValueError('Private generation failed')
        e=out/'tyra/engine';g=out/'game'
        for name in ('quiet_cadence.hpp','quiet_runtime.hpp'):shutil.copy2(SUPPORT/name,e/'inc/debug'/name)
        switch(e/'inc/debug/frame_profile.hpp','TYRA_FRAME_PROFILE');switch(e/'inc/debug/hardware_trace.hpp','TYRA_HARDWARE_TRACE')
        patch(e/'src/engine.cpp',engine_hooks);patch(e/'src/renderer/core/renderer_core.cpp',present_hooks);patch(g/'src/terrain_game.cpp',game_hooks)
        patch(e/'inc/debug/debug.hpp',lambda t:once(t,'#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")','#define TYRA_LOG(...) ((void)0) // Private quiet fixture'))
        patch(e/'src/renderer/core/paths/path1/vif1_queue.cpp',lambda t:once(t,'if (++frames % 120 == 0) {','if ((++frames % 120 == 0) && false) { // Private no periodic report'))
        (g/'bin').mkdir(exist_ok=True);(g/'bin/quiet-control.cfg').write_bytes(f'{order}\n'.encode())
        files=source_inputs(out,manifest.name)
        (out/'target-source-manifest.json').write_text(json.dumps({'files':files},indent=2)+'\n',encoding='utf8')
        assets={str(p.relative_to(project)).replace('\\','/'):sha(p) for root in (project/'res',project/'.res-baked') for p in root.rglob('*') if p.is_file()}
        record={'status':'ordinary_quiet_fixture_prepared','project':str(project),'engine':str(engine),'editor':str(editor),'editor_sha256':sha(editor),'order':order,'ordinary_clocks':True,'night_known':False,'authored_save_values_unchanged':model.get('saveValues',[])==save_values,'control_apparatus_disabled':disable_control_apparatus,'control_apparatus_keys':['remotePad','inputRecorder'] if disable_control_apparatus else [],'authored_settings_before':original,'settings':model['settings'],'source_files':len(files),'target_source_manifest_sha256':sha(out/'target-source-manifest.json'),'source_assets':assets,'build_or_device_actions':False,'limitations':['First-person generated model; sparse state assumes initial scene0.','Authored remote-pad/input-recorder/keyboard controls remain by default; they may retain their own polling cost.','Flip returns are queue boundaries under triple buffering, not TV scanout proof.','Adaptive selector/ordinary physics can differ between arms; no isolated observer bill or uniform correction.','Count chunks assume no entire missed wrap between64-loop endpoints.']}
        (out/'fixture.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf8');return record
    except Exception as exc:
        (out/'rejected-preparation.json').write_text(json.dumps({'status':'rejected','reason':str(exc)},indent=2)+'\n',encoding='utf8');raise
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('project','engine','editor','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--order',type=int,choices=[0,1],default=0);p.add_argument('--disable-control-apparatus',action='store_true',help='Explicit tax control: disable authored remotePad/inputRecorder in the private copy');a=p.parse_args()
    try:r=prepare(a.project,a.engine,a.editor,a.out,a.order,a.disable_control_apparatus)
    except (ValueError,OSError,KeyError) as e:p.error(str(e))
    print(r['status']);print(r['target_source_manifest_sha256'])
if __name__=='__main__':main()
