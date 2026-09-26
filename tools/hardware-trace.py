"""Arm a PS2 boot capture, or export its complete CSV to HTML and Perfetto JSON.

No third-party Python dependencies. See docs/hardware-profiler.md.
"""
import argparse
import csv
import json
from pathlib import Path


def read_capture(path):
    with path.open(newline='', encoding='utf-8') as f:
        rows = list(csv.DictReader(f))
    if not rows or rows[-1]['label'] != 'END':
        raise ValueError('Incomplete capture: missing END footer')
    footer = rows.pop()
    count, dropped, frames, first = (int(footer[k]) for k in
        ['start_ticks', 'duration_ticks', 'frame', 'value'])
    if dropped:
        raise ValueError(f'Capture overflow: {dropped} events dropped; request fewer frames')
    if len(rows) != count or not 1 <= frames <= 32:
        raise ValueError('Invalid event count or frame count')
    for row in rows:
        for key in ['start_ticks', 'duration_ticks', 'frame', 'value']:
            row[key] = int(row[key])
            if not 0 <= row[key] <= 0xffffffff:
                raise ValueError('Invalid unsigned event field')
    frame_rows = {x['frame']: x for x in rows if x['label'] == 'Frame'}
    if len(frame_rows) != frames or set(frame_rows) != set(range(first, first+frames)):
        raise ValueError('Missing or duplicate frame range')
    if sum(x['label'] == 'Frame' for x in rows) != frames:
        raise ValueError('Duplicate frame event')
    for x in rows:
        if x['frame'] not in frame_rows:
            raise ValueError('Event outside requested frames')
        f = frame_rows[x['frame']]
        if not (f['start_ticks'] <= x['start_ticks'] and
                x['start_ticks']+x['duration_ticks'] <= f['start_ticks']+f['duration_ticks']):
            raise ValueError('Event outside frame time bounds')
    return rows, frame_rows


HTML = r'''<!doctype html><meta charset="utf-8"><title>PS2 hardware timeline</title>
<style>body{font:15px system-ui;background:#121723;color:#e8edf6;margin:24px}select{font:inherit;padding:6px}svg{width:100%;min-width:900px;background:#192233}text{fill:#e8edf6;font:12px system-ui}main{overflow:auto}p{max-width:1000px;line-height:1.5}table{border-collapse:collapse}td,th{padding:6px 20px;text-align:right;border-bottom:1px solid #354054}td:first-child,th:first-child{text-align:left}</style>
<h1>PS2 hardware timeline</h1><p>Measured EE scopes and waits overlap across rows. VIF/GIF markers are instantaneous register observations, not VU1/GS utilization. No per-draw barriers were added. Export and file I/O occur after the capture. Hover for raw values; select a frame below.</p>
<label>Frame <select id="frame"></select></label><main><svg id="chart"></svg></main>
<h2>Selected frame: inclusive scope totals</h2><p>Do not sum nested rows. Frame includes pad, game and info work. Present includes buffer-flip waits, not only VSync. Short captures are diagnostics; use longer controls to establish performance.</p><table><thead><tr><th>Scope</th><th>Count</th><th>Total ms</th></tr></thead><tbody id="totals"></tbody></table>
<script>const events=DATA;
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
n.append(el('title',{},detail));svg.append(n);if(!mark){const a=sums[e.label]??=[0,0];a[0]++;a[1]+=e.duration_ticks}}
const table=document.querySelector('#totals');table.replaceChildren();for(const [name,[n,t]] of Object.entries(sums).sort((a,b)=>b[1][1]-a[1][1])){const tr=document.createElement('tr');for(const v of [name,n,(t/294912).toFixed(4)]){const td=document.createElement('td');td.textContent=v;tr.append(td)}table.append(tr)}}select.onchange=draw;draw();</script>'''


def export(path, output):
    rows, frames = read_capture(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(rows).replace('<', '\\u003c')
    output.with_suffix('.html').write_text(HTML.replace('DATA', payload), encoding='utf-8')
    labels = sorted({x['label'] for x in rows})
    trace = [{'ph': 'M', 'name': 'thread_name', 'pid': 1, 'tid': i,
              'args': {'name': label}} for i, label in enumerate(labels)]
    for x in rows:
        event = {'name': x['label'], 'pid': 1, 'tid': labels.index(x['label']),
                 'ts': x['start_ticks']/294.912, 'args': {'frame': x['frame'], 'value': x['value']}}
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
    output.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print(f'{len(rows)} events, {len(frames)} complete frames -> {output.with_suffix(".html")}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    arm = sub.add_parser('arm', help='Configure the next boot; does not reset the console')
    arm.add_argument('project', type=Path)
    arm.add_argument('--start', type=int, default=120)
    arm.add_argument('--frames', type=int, default=4)
    arm.add_argument('--no-states', action='store_true')
    disarm = sub.add_parser('disarm')
    disarm.add_argument('project', type=Path)
    exp = sub.add_parser('export')
    exp.add_argument('csv', type=Path)
    exp.add_argument('-o', '--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'export':
        export(args.csv, args.output)
    else:
        cfg = args.project/'bin/hardware-trace.cfg'
        if not cfg.parent.is_dir():
            parser.error('Project bin directory does not exist; build the game first')
        if args.command == 'disarm':
            cfg.unlink(missing_ok=True)
        else:
            if not 0 <= args.start <= 1000000 or not 1 <= args.frames <= 32:
                parser.error('start must be 0..1000000; frames must be 1..32')
            cfg.write_text(f'{args.start} {args.frames} {int(not args.no_states)}\n', encoding='ascii')
            print(f'Armed next boot: {cfg}. Existing CSV is not proof of a fresh capture.')


if __name__ == '__main__':
    main()
