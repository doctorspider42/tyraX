"""Arm a PS2 boot capture, or export its complete CSV to HTML and Perfetto JSON.

No third-party Python dependencies. See docs/hardware-profiler.md.
"""
import argparse
import csv
import json
from pathlib import Path
from hardware_trace_analysis import read_capture as read_accounted_capture, account, compare_controls


def read_capture(path):
    rows, frames, _ = read_accounted_capture(path)
    return rows, frames


HTML = r'''<!doctype html><meta charset="utf-8"><title>PS2 hardware timeline</title>
<style>body{font:15px system-ui;background:#121723;color:#e8edf6;margin:24px}select{font:inherit;padding:6px}svg{width:100%;min-width:900px;background:#192233}text{fill:#e8edf6;font:12px system-ui}main{overflow:auto}p{max-width:1000px;line-height:1.5}table{border-collapse:collapse}td,th{padding:6px 20px;text-align:right;border-bottom:1px solid #354054}td:first-child,th:first-child{text-align:left}</style>
<h1>PS2 hardware timeline</h1><p>Measured EE scopes and waits overlap across rows. The exclusive ledger counts each interval once and exposes unaccounted time. VIF/GIF snapshots are instantaneous observations, not VU1/GS utilization. Job IDs identify recorded work and CPU presentation sources; N/N-1 alone does not establish pipeline age. Hover for raw values; select a frame below.</p>
<label>Frame <select id="frame"></select></label><main><svg id="chart"></svg></main>
<h2>Selected frame: exclusive EE ledger</h2><p>CPU spans include elapsed EE work, not solely arithmetic. Wait and pacing spans remain separate. Unaccounted and ambiguous intervals are explicit. Capture scaffolding and CPU scheduling change with observers: compare compiled-out, runtime-off, coarse and detailed arms with matched camera/replay/clock inputs and repeated A/B controls. Do not subtract one constant overhead.</p><table><tbody id="ledger"></tbody></table>
<h2>Scope hierarchy</h2><p>Inclusive parent and child durations overlap. Exclusive durations partition the measured frame; legacy equal or crossing spans remain ambiguous.</p><table><thead><tr><th>Scope / parent</th><th>Job / CPU present source</th><th>Inclusive ms</th><th>Exclusive ms</th></tr></thead><tbody id="totals"></tbody></table>
<script>const events=DATA,report=REPORT;
const select=document.querySelector('#frame'), svg=document.querySelector('#chart');
const frames=events.filter(e=>e.label==='Frame');
for(const f of frames){const o=document.createElement('option');o.value=f.frame;o.textContent=f.frame;select.append(o)}
function el(tag,attrs,text){const n=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const [k,v] of Object.entries(attrs))n.setAttribute(k,v);if(text!==undefined)n.textContent=text;return n}
function draw(){svg.replaceChildren();const rows=events.filter(e=>e.frame===+select.value), f=rows.find(e=>e.label==='Frame');const labels=[...new Set(rows.map(e=>e.label))].sort();labels.splice(labels.indexOf('Frame'),1);labels.unshift('Frame');const width=1200,left=190,scale=980/f.duration_ticks;svg.setAttribute('viewBox',`0 0 ${width} ${labels.length*26+45}`);
for(let i=0;i<=5;i++){const x=left+i*196;svg.append(el('line',{x1:x,x2:x,y1:25,y2:labels.length*26+30,stroke:'#344157'}));svg.append(el('text',{x,y:16},(f.duration_ticks*i/5/294912).toFixed(2)+' ms'))}
labels.forEach((l,i)=>svg.append(el('text',{x:8,y:45+i*26},l)));
const sums={};for(const e of rows){const y=30+labels.indexOf(e.label)*26,x=left+(e.start_ticks-f.start_ticks)*scale;const mark=e.duration_ticks===0;const n=el('rect',{x,y,width:Math.max(mark?2:0.1,e.duration_ticks*scale),height:18,fill:mark?'#dba44c':e.label.includes('wait')?'#e27b6c':'#55b4d5',opacity:.85});let detail=`${e.label}: ${(e.duration_ticks/294912).toFixed(4)} ms; value=${e.value} (0x${e.value.toString(16)})`;
if(e.label.startsWith('VIF1_')&&mark)detail+=`; VPS=${e.value&3}, VEW=${(e.value>>2)&1}`;
if(e.label==='GIF_STATE')detail+=`; APATH=${(e.value>>10)&3}, FIFO=${(e.value>>24)&31}`;
if(e.kind==='legacy')detail+='; INCLUSIVE ONLY: unknown hierarchy, excluded from exclusive ledger';
n.append(el('title',{},detail+`; event=${e.event_id}, parent=${e.parent_id}, kind=${e.kind}, record job=${e.job_id??'unknown'}, CPU present source=${e.display_source_id??'unknown'}, exclusive=${(e.exclusive_ticks/294912).toFixed(4)} ms`));svg.append(n)}
const selected=report.frames.find(e=>e.frame===f.frame),ledger=document.querySelector('#ledger');ledger.replaceChildren();for(const [name,t] of Object.entries(selected.ledger_ms)){const tr=document.createElement('tr');for(const v of [name,t.toFixed(4)+' ms']){const td=document.createElement('td');td.textContent=v;tr.append(td)}ledger.append(tr)}
const table=document.querySelector('#totals');table.replaceChildren();for(const e of [...rows].sort((a,b)=>a.start_ticks-b.start_ticks||b.duration_ticks-a.duration_ticks||a.event_id-b.event_id)){const tr=document.createElement('tr');for(const v of [`${e.label} (#${e.event_id}, parent #${e.parent_id})`,`${e.job_id??'unknown'} / ${e.display_source_id??'unknown'}`,(e.duration_ticks/294912).toFixed(4),e.kind==='legacy'?'unknown / inclusive only':(e.exclusive_ticks/294912).toFixed(4)]){const td=document.createElement('td');td.textContent=v;tr.append(td)}table.append(tr)}}select.onchange=draw;draw();</script>'''


def export(path, output):
    rows, frames, metadata = read_accounted_capture(path)
    report = account(rows, frames, metadata)
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(rows).replace('<', '\\u003c')
    output.with_suffix('.html').write_text(HTML.replace('DATA', payload).replace('REPORT', json.dumps(report).replace('<', '\\u003c')), encoding='utf-8')
    labels = sorted({x['label'] for x in rows})
    trace = [{'ph': 'M', 'name': 'thread_name', 'pid': 1, 'tid': i,
              'args': {'name': label}} for i, label in enumerate(labels)]
    for x in sorted(rows, key=lambda row:(row['start_ticks'],-row['duration_ticks'],row['event_id'])):
        event = {'name': x['label'], 'pid': 1, 'tid': labels.index(x['label']),
                 'ts': x['start_ticks']/294.912, 'args': {'frame': x['frame'], 'value': x['value'], 'event_id':x['event_id'],'parent_id':x['parent_id'],'kind':x['kind'],'record_job':x['job_id'],'display_source':x['display_source_id'],'exclusive_ticks':x['exclusive_ticks']}}
        if x['duration_ticks']:
            event.update(ph='X', dur=x['duration_ticks']/294.912)
        else:
            event.update(ph='i', s='t')
        trace.append(event)
    output.with_suffix('.json').write_text(json.dumps({'traceEvents':trace}), encoding='utf-8')
    summary = {}
    for label in labels:
        rs = [x for x in rows if x['label']==label]
        summary[label] = {'count': len(rs), 'mean_ms_per_frame':
                         sum(x['duration_ticks'] for x in rs)/294912/len(frames)}
    report['inclusive_label_totals']=summary
    output.with_suffix('.summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'{len(rows)} events, {len(frames)} complete frames -> {output.with_suffix(".html")}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    arm = sub.add_parser('arm', help='Configure the next boot; does not reset the console')
    arm.add_argument('project', type=Path)
    arm.add_argument('--start', type=int, default=120)
    arm.add_argument('--frames', type=int, default=4)
    arm.add_argument('--no-states', action='store_true')
    arm.add_argument('--detailed', action='store_true', help='Request detailed scopes; coarse capture is the default')
    arm.add_argument('--capacity', type=int, default=8192, help='Bounded RAM event slots (128..32768); detailed captures may need a shorter window')
    disarm = sub.add_parser('disarm')
    disarm.add_argument('project', type=Path)
    exp = sub.add_parser('export')
    exp.add_argument('csv', type=Path)
    exp.add_argument('-o', '--output', type=Path, required=True)
    controls = sub.add_parser('controls', help='Review explicit repeated observer A/B/A controls')
    controls.add_argument('manifest', type=Path)
    controls.add_argument('-o', '--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'export':
        export(args.csv, args.output)
    elif args.command == 'controls':
        result=compare_controls(json.loads(args.manifest.read_text(encoding='utf-8')))
        args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    else:
        cfg = args.project/'bin/hardware-trace.cfg'
        if not cfg.parent.is_dir():
            parser.error('Project bin directory does not exist; build the game first')
        if args.command == 'disarm':
            cfg.unlink(missing_ok=True)
        else:
            if not 0 <= args.start <= 1000000 or not 1 <= args.frames <= 32:
                parser.error('start must be 0..1000000; frames must be 1..32')
            if not 128 <= args.capacity <= 32768:
                parser.error('capacity must be 128..32768')
            cfg.write_text(f'{args.start} {args.frames} {int(not args.no_states)} {int(args.detailed)} {args.capacity}\n', encoding='ascii')
            print(f'Armed next boot: {cfg}. Existing CSV is not proof of a fresh capture.')


if __name__ == '__main__':
    main()
