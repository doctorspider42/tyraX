"""Strict host-side accounting for measured EE intervals, never VU utilization."""
import csv
import hashlib
import math
import statistics
from pathlib import Path

U32=1<<32
LEGACY=['label','start_ticks','duration_ticks','frame','value']
V2=LEGACY+['event_id','parent_id','kind','record_job','present_job','sequence','context','requested','effective','clock_epoch']
SNAPSHOTS={'GIF_STATE','VIF1_FRAME_START','VIF1_WAIT_START','VIF1_WAIT_END'}
WAIT_LABELS={'VIF1_DMA_wait','GIF_DMA_wait','GS_FINISH_wait'}
def _unsigned(text,bits=32):
    if not isinstance(text,str) or not text.isascii() or not text.isdecimal():
        raise ValueError('Expected an unsigned decimal field')
    value=int(text)
    if value >= 1<<bits: raise ValueError('Unsigned field overflow')
    return value

def _union(intervals):
    total=0;end=None
    for lo,hi in sorted(intervals):
        if end is None or lo>end:total+=hi-lo;end=hi
        elif hi>end:total+=hi-end;end=hi
    return total

def read_legacy(path):
    """Keep the established CSV interface, with explicit bounded wrap recovery."""
    with Path(path).open(newline='',encoding='utf-8') as handle:
        reader=csv.DictReader(handle)
        if reader.fieldnames!=LEGACY:raise ValueError('Unknown CSV schema')
        rows=list(reader)
    if not rows or rows[-1].get('label')!='END':raise ValueError('Incomplete capture: missing END footer')
    if any(set(row)!=set(LEGACY) or any(v is None for v in row.values()) for row in rows):raise ValueError('Malformed CSV row')
    footer=rows.pop()
    count,dropped,n,first=(_unsigned(footer[k]) for k in LEGACY[1:])
    if dropped:raise ValueError(f'Capture overflow: {dropped} events dropped')
    if len(rows)!=count or not 1<=n<=32 or first>1000000:raise ValueError('Invalid event/footer count')
    for index,row in enumerate(rows,1):
        if not row['label'] or row['label']=='END':raise ValueError('Invalid event label or premature footer')
        for field in LEGACY[1:]:row[field]=_unsigned(row[field])
        row['event_id']=index
        row['kind']='snapshot' if row['label'] in SNAPSHOTS else 'instant' if row['duration_ticks']==0 else 'wait' if row['label'] in WAIT_LABELS else 'pacing' if row['label']=='Present' else 'cpu'
        row['job_id']=None;row['display_source_id']=None
    roots=[row for row in rows if row['label']=='Frame']
    if len(roots)!=n or sorted(row['frame'] for row in roots)!=list(range(first,first+n)):raise ValueError('Missing/duplicate frame range')
    frames={row['frame']:row for row in roots}
    epoch=0;prior_start=None;prior_end=None
    for frame in range(first,first+n):
        root=frames[frame];raw_start=root['start_ticks'];duration=root['duration_ticks']
        if duration==0 or duration>=U32//2:raise ValueError('Zero or ambiguous half-wrap frame duration')
        if prior_start is not None and raw_start<prior_start:epoch+=U32
        absolute=epoch+raw_start
        if prior_end is not None and absolute<prior_end:raise ValueError('Overlapping/out-of-order frames')
        root['absolute_start']=absolute;prior_start=raw_start;prior_end=absolute+duration
    if frames[first]['start_ticks']!=0:raise ValueError('Legacy capture origin must be first frame start')
    grouped={number:[] for number in frames}
    for row in rows:
        if row['frame'] not in grouped:raise ValueError('Event outside requested frame range')
        grouped[row['frame']].append(row)
    for row in rows:
        if row['frame'] not in frames:raise ValueError('Event outside requested frame range')
        root=frames[row['frame']]
        offset=(row['start_ticks']-root['start_ticks'])%U32
        if offset+row['duration_ticks']>root['duration_ticks']:raise ValueError('Event outside frame time bounds')
        row['absolute_start']=root['absolute_start']+offset
        row['start_ticks']=row['absolute_start']
    # Types/parents are inferred, not source annotations. Equal/crossing spans
    # are accounted as ambiguity, rather than forced into a fictional tree.
    for number,group in grouped.items():
        root=frames[number];active=[]
        for row in sorted(group,key=lambda item:(item['start_ticks'],-item['duration_ticks'])):
            row['parent_id']=0 if row is root else root['event_id']
            if row is root:continue
            lo=row['start_ticks'];hi=lo+row['duration_ticks']
            active=[candidate for candidate in active if candidate['start_ticks']+candidate['duration_ticks']>=lo]
            containers=[candidate for candidate in active if candidate['duration_ticks']>row['duration_ticks'] and candidate['start_ticks']+candidate['duration_ticks']>=hi]
            if containers:
                smallest=min(candidate['duration_ticks'] for candidate in containers)
                nearest=[candidate for candidate in containers if candidate['duration_ticks']==smallest]
                if len(nearest)==1:row['parent_id']=nearest[0]['event_id']
            if row['duration_ticks']:active.append(row)
    metadata={'schema':'legacy','count':count,'dropped':dropped,'frames':n,'first':first,'hierarchy':'inferred by strict interval containment; equality/crossing is ambiguous','wrap':'frame-relative modular reconstruction; half-wrap frames rejected; multiple wraps within an event cannot be established','capture_sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest()}
    return rows,frames,metadata

def read_v2(path):
    with Path(path).open(newline='',encoding='utf-8') as handle:
        reader=csv.DictReader(handle)
        if reader.fieldnames!=V2:raise ValueError('Unknown v2 CSV schema')
        rows=list(reader)
    if not rows or rows[-1].get('label')!='END':raise ValueError('Incomplete v2 capture: missing END')
    if any(set(row)!=set(V2) or any(v is None for v in row.values()) for row in rows):raise ValueError('Malformed v2 CSV row')
    footer=rows.pop()
    for field in V2:
        if field not in ['label','kind']:footer[field]=_unsigned(footer[field])
    count,dropped,n,first=(footer[field] for field in LEGACY[1:])
    terminal={'event_id':0,'parent_id':0,'kind':'end','record_job':0,'present_job':0,'sequence':0,'context':U32-1,'requested':0,'effective':0,'clock_epoch':0}
    if footer['parent_id']:raise ValueError('Engine reported invalid trace hierarchy')
    if any(footer[field]!=value for field,value in terminal.items()):raise ValueError('Invalid v2 terminal metadata')
    if dropped:raise ValueError('v2 capture overflow/dropped events')
    if len(rows)!=count or not 1<=n<=32 or first>1000000:raise ValueError('Invalid v2 footer count')
    ids={}
    for row in rows:
        if not row['label'] or row['label']=='END':raise ValueError('Invalid v2 event label')
        if row['kind'] not in ['span','wait','pacing','snapshot','marker','legacy']:raise ValueError('Unknown event kind')
        row['source_kind']=row['kind'];row['kind']='cpu' if row['kind']=='span' else row['kind']
        for field in V2:
            if field not in ['label','kind']:row[field]=_unsigned(row[field])
        if row['context'] not in [0,1,2,U32-1] or row['requested'] not in [0,1] or row['effective'] not in [0,1]:raise ValueError('Invalid renderer state metadata')
        eid=row['event_id']
        if not eid or eid in ids:raise ValueError('Duplicate/zero event ID')
        if row['kind'] in ['snapshot','marker'] and row['duration_ticks']:raise ValueError('Instant event has duration')
        if row['duration_ticks']>=U32//2:raise ValueError('Ambiguous half-wrap v2 duration')
        row['start_ticks']+=row['clock_epoch']*U32
        row['job_id']=row['record_job'] or None
        row['display_source_id']=row['present_job'] or None
        ids[eid]=row
    if set(ids)!=set(range(1,count+1)):raise ValueError('Missing v2 event IDs')
    roots=[row for row in rows if row['label']=='Frame']
    if len(roots)!=n or sorted(row['frame'] for row in roots)!=list(range(first,first+n)):raise ValueError('Missing/duplicate v2 frame range')
    frames={row['frame']:row for row in roots};previous_end=None
    for number,root in sorted(frames.items()):
        if root['parent_id']!=0 or root['kind']!='cpu' or not root['duration_ticks']:raise ValueError('Invalid v2 Frame root')
        if previous_end is not None and root['start_ticks']<previous_end:raise ValueError('Overlapping/out-of-order v2 frames')
        previous_end=root['start_ticks']+root['duration_ticks']
    if frames[first]['start_ticks']!=0:raise ValueError('v2 origin must be first Frame start')
    for row in rows:
        root=frames.get(row['frame'])
        if root is None:raise ValueError('v2 event outside requested frame range')
        if row is root:row['depth']=0;continue
        if row['kind']=='legacy':
            if row['parent_id']!=U32-1 or not row['duration_ticks']:raise ValueError('Legacy interval must declare unknown parent')
            if not(root['start_ticks']<=row['start_ticks'] and row['start_ticks']+row['duration_ticks']<=root['start_ticks']+root['duration_ticks']):raise ValueError('Legacy interval outside Frame')
            row['depth']=None;continue
        parent=ids.get(row['parent_id'])
        if parent is None or parent['frame']!=row['frame'] or parent['kind'] in ['snapshot','marker','legacy']:raise ValueError('Invalid/missing v2 parent')
        if not(parent['start_ticks']<=row['start_ticks'] and row['start_ticks']+row['duration_ticks']<=parent['start_ticks']+parent['duration_ticks']):raise ValueError('v2 child outside parent interval')
        seen={row['event_id']};ancestor=parent;depth=1
        while ancestor is not root:
            if ancestor['event_id'] in seen:raise ValueError('v2 parent cycle')
            seen.add(ancestor['event_id']);ancestor=ids.get(ancestor['parent_id']);depth+=1
            if ancestor is None or ancestor['kind']=='legacy':raise ValueError('v2 parent does not reach Frame root')
        row['depth']=depth
    children_by_parent={}
    for row in rows:
        if row['duration_ticks']:children_by_parent.setdefault(row['parent_id'],[]).append(row)
    for parent in ids.values():
        children=sorted(children_by_parent.get(parent['event_id'],[]),key=lambda row:row['start_ticks'])
        for left,right in zip(children,children[1:]):
            if left['start_ticks']+left['duration_ticks']>right['start_ticks']:raise ValueError('v2 CPU sibling intervals overlap')
    metadata={'schema':'v2','count':count,'dropped':dropped,'frames':n,'first':first,'hierarchy':'explicit source-reserved parent/event IDs','clock':'Explicit start clock_epoch; durations shorter than half-wrap required; hidden multiple wraps cannot be reconstructed.','footer_metadata':{field:footer[field] for field in V2[5:]},'capture_sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest()}
    return rows,frames,metadata

def read_capture(path):
    with Path(path).open(newline='',encoding='utf-8') as handle:header=next(csv.reader(handle),None)
    if header==LEGACY:return read_legacy(path)
    if header==V2:return read_v2(path)
    raise ValueError('Unknown hardware trace CSV schema')

def account(rows,frames,metadata):
    """Partition each frame once. Nested inclusive totals remain separate."""
    result=[]
    for number,root in sorted(frames.items()):
        group=[row for row in rows if row['frame']==number]
        spans=[row for row in group if row['duration_ticks'] and row is not root and row['kind']!='legacy']
        lo=root['start_ticks'];hi=lo+root['duration_ticks']
        points=sorted({lo,hi}|{p for row in spans for p in [row['start_ticks'],row['start_ticks']+row['duration_ticks']]})
        starts={};ends={};active_ids={}
        for row in spans:
            starts.setdefault(row['start_ticks'],[]).append(row)
            ends.setdefault(row['start_ticks']+row['duration_ticks'],[]).append(row['event_id'])
        exclusive={row['event_id']:0 for row in group}
        ledger={'cpu':0,'wait':0,'pacing':0,'unaccounted_frame':0,'ambiguous':0}
        jobs={};ambiguities=[]
        for start,end in zip(points,points[1:]):
            for eid in ends.get(start,[]):active_ids.pop(eid,None)
            for row in starts.get(start,[]):active_ids[row['event_id']]=row
            active=list(active_ids.values())
            if not active:ledger['unaccounted_frame']+=end-start;exclusive[root['event_id']]+=end-start;continue
            # Only a unique strict leaf is an exclusive owner. A crossing peer
            # remains ambiguous even when its interval is longer.
            if metadata['schema']=='v2':
                deepest=max(row['depth'] for row in active);leaves=[row for row in active if row['depth']==deepest]
            else:leaves=[row for row in active if not any(other is not row and other['duration_ticks']<row['duration_ticks'] and other['start_ticks']>=row['start_ticks'] and other['start_ticks']+other['duration_ticks']<=row['start_ticks']+row['duration_ticks'] for other in active)]
            if len(leaves)!=1:
                ledger['ambiguous']+=end-start;ambiguities.append({'start_ticks':start,'duration_ticks':end-start,'event_ids':[row['event_id'] for row in leaves]});continue
            owner=leaves[0];exclusive[owner['event_id']]+=end-start
            category=owner['kind'] if owner['kind'] in ['cpu','wait','pacing'] else 'cpu'
            ledger[category]+=end-start
            job=owner.get('job_id')
            if job is not None:
                key=str(job);jobs[key]=jobs.get(key,0)+end-start
        if sum(ledger.values())!=root['duration_ticks']:raise ValueError('Internal exclusive partition mismatch')
        for row in group:row['exclusive_ticks']=exclusive[row['event_id']]
        labels={}
        for row in group:
            label=labels.setdefault(row['label'],{'count':0,'inclusive_ticks':0,'exclusive_ticks':0})
            label['count']+=1;label['inclusive_ticks']+=row['duration_ticks'];label['exclusive_ticks']+=row['exclusive_ticks']
        identities=sorted({(row.get('record_job'),row.get('present_job'),row.get('sequence'),row.get('context'),row.get('requested'),row.get('effective')) for row in group},key=str)
        result.append({'frame':number,'duration_ticks':root['duration_ticks'],'ledger_ticks':ledger,'ledger_ms':{key:value/294912 for key,value in ledger.items()},'scope_totals':labels,'job_exclusive_ticks':jobs,'source_identities':[dict(zip(['record_job','present_job','sequence','context','requested','effective'],identity)) for identity in identities],'unaccounted_ee_ticks':ledger['unaccounted_frame']+ledger['ambiguous'],'ambiguities':ambiguities,'inclusive_only_legacy_event_ids':[row['event_id'] for row in group if row['kind']=='legacy'],'instant_snapshots':[{'label':row['label'],'value':row['value'],'start_ticks':row['start_ticks']} for row in group if row['kind']=='snapshot']})
    return {'capture':metadata,'frames':result,'interpretation':['Measured EE elapsed critical-path intervals, not pure useful CPU arithmetic.','Inclusive parents and children overlap; only exclusive frame ledger is additive.','Frame gaps and ambiguous overlap are explicit unaccounted EE/frame time.','Register snapshots are instants; no VU/GS utilization or consumer duration inferred.','CPU job IDs and present_job identify CPU recording/presentation sources; N versus N-1 is not an inferred pipeline age. Triple buffering may queue a source for a later uninstrumented ISR display; no TV/display-latch latency is measured.','Present includes presentation/buffer-flip pacing; legacy classification is inferred.','Observer controls need compiled-out, runtime-off, coarse and detailed builds/captures with matched camera/replay/clock inputs, repeated A/B; layout and warm-state differences can remain.','Do not subtract one constant overhead or promise zero observer impact.']}

def compare_controls(manifest):
    """Review an explicit matched control matrix; never silently calibrate ticks."""
    arms=['compiled_out','runtime_off','coarse','detailed']
    inputs=manifest.get('matched_inputs')
    if not isinstance(inputs,dict) or any(not inputs.get(key) for key in ['camera','replay','clock','scene','pass_order','sample_window']):raise ValueError('Missing matched camera/replay/clock/scene/pass/window identities')
    runs=manifest.get('runs')
    if not isinstance(runs,list) or not runs:raise ValueError('Missing control runs')
    if not isinstance(manifest.get('layout_notes'),str) or not manifest['layout_notes'].strip():raise ValueError('Declare code-layout control limitations')
    grouped={arm:[] for arm in arms};seen=set();instrumented=set()
    for run in runs:
        arm=run.get('arm');identity=run.get('run_id')
        if arm not in grouped or not identity or identity in seen:raise ValueError('Unknown/duplicate control run')
        seen.add(identity)
        if run.get('matched_inputs')!=inputs:raise ValueError('Control camera/replay/clock or window mismatch')
        digest=run.get('elf_sha256','')
        if len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest):raise ValueError('Missing exact ELF hash')
        if arm!='compiled_out':instrumented.add(digest)
        values=run.get('work_ms');periods=run.get('period_ms')
        if not isinstance(values,list) or not isinstance(periods,list) or len(values)<2 or len(values)!=len(periods):raise ValueError('Need matched work/period samples')
        if any(type(v) not in [int,float] or not math.isfinite(v) or v<0 for v in values) or any(type(v) not in [int,float] or not math.isfinite(v) or v<=0 for v in periods):raise ValueError('Invalid control work/period values')
        grouped[arm].append({'run_id':identity,'elf_sha256':digest,'mean_work_ms':statistics.mean(values),'mean_period_ms':statistics.mean(periods),'samples':len(values)})
    if any(len(grouped[arm])<2 for arm in arms):raise ValueError('Need repeated compiled-out/runtime-off/coarse/detailed controls')
    if len(instrumented)!=1:raise ValueError('Runtime-off/coarse/detailed must use the same instrumented ELF')
    # Explicit bracket associations prevent a pile of unrelated run averages
    # being mistaken for an A/B control matrix.
    brackets=manifest.get('brackets')
    if not isinstance(brackets,list) or not brackets:raise ValueError('Need explicit repeated A/B/A brackets')
    by_id={run['run_id']:run for group in grouped.values() for run in group}
    arm_by_id={run['run_id']:arm for arm,group in grouped.items() for run in group}
    order={run['run_id']:index for index,run in enumerate(runs)}
    differences=[];covered=set()
    for bracket in brackets:
        if not isinstance(bracket,list) or len(bracket)!=3 or any(identity not in by_id for identity in bracket):raise ValueError('Invalid A/B/A bracket')
        before,middle,after=bracket
        if not order[before]<order[middle]<order[after]:raise ValueError('A/B/A order must match chronological runs')
        if before==after or arm_by_id[before]!=arm_by_id[after] or arm_by_id[before]==arm_by_id[middle]:raise ValueError('A/B/A requires distinct repeated same-arm controls')
        covered.add(arm_by_id[middle]);b=by_id[middle]
        differences.append({'runs':bracket,'middle_arm':arm_by_id[middle],'work_delta_vs_controls_ms':[b['mean_work_ms']-by_id[identity]['mean_work_ms'] for identity in [before,after]],'control_spread_ms':abs(by_id[before]['mean_work_ms']-by_id[after]['mean_work_ms'])})
    if not {'runtime_off','coarse','detailed'}<=covered:raise ValueError('Bracket every observer level including runtime-off scaffolding')
    return {'matched_inputs':inputs,'arms':grouped,'brackets':differences,'layout_notes':manifest['layout_notes'],'interpretation':'Observed differences are ranges against repeated matched controls, not a universal overhead constant. Compiled-out versus instrumented code layout, temporal/warm state, pacing and observer scheduling effects remain; no automatic subtraction, zero-overhead claim or production speed acceptance.'}
