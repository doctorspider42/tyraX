"""Exercise the actual generated owner reader and its admission negatives."""
import ast
import re
from pathlib import Path
path=Path(__file__).with_name('tyrax2-world-runtime-analyze.py')
text=path.read_text();finish="exec(compile(source,str(__file__),'exec'))"
assert text.rstrip().endswith(finish)
scope={'__file__':str(path)}
exec(compile(text.rstrip()[:-len(finish)],str(path),'exec'),scope)
tree=ast.parse(scope['source'])
functions=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('cold','movingOwner')]
env={'re':re,'count':6,'ownerContracts':scope['ownerContracts'],'report':{'negativeControls':[]}}
exec(compile(ast.Module(body=functions,type_ignores=[]),str(path),'exec'),env)
lines=[];env['phases']=[]
for phase,mode in enumerate([0,7,0]*2):
    env['phases'].append(dict(mode=mode,witnessFrames=4))
    lines.append(f'WORLDWITNESS phase={phase} mode={mode} frames=4 draws={15 if mode==7 else 0} qwords={2000 if mode==7 else 0}')
    for arm in range(5):
        prefix=f' phase={phase} arm={arm}'
        for name,value in (
            ('WORLDOWNER',f'sources=7 draws={7 if arm==1 else 0} triangles=1942 hash=48575764'),
            ('CONSUMERINPUT','calls=2 verts=6 hash=12'),('HEADERORACLE','checks=1'),
            ('COARSESOURCE','packages=1 triangles=2 vertices=6 hash=12'),
            ('COARSEDELIVERY','jobs=1 triangles=2 vertices=6 hash=12'),
            ('COARSERESIDUAL','triangles=0 vertices=0 hash=2166136261'),
            ('RINGCONTRACT','calls=1 triangles=2')):lines.append(name+prefix+' '+value)
env['text']='\n'.join(lines)
assert len(env['cold'](env['text'])[0])==30
assert len(env['movingOwner'](env['text']))==6
negatives=[n for n in tree.body if isinstance(n,ast.For) and (
    ast.unparse(n.target)=='name' and 'missing=text.replace' in ast.unparse(n).replace(' ','') or
    ast.unparse(n.target)=='(label, bad)')]
assert len(negatives)==3,len(negatives)
for node in negatives:exec(compile(ast.Module(body=[node],type_ignores=[]),str(path),'exec'),env)
assert len(env['report']['negativeControls'])==21,env['report']
flags=[n for n in tree.body if isinstance(n,ast.Assign) and any(
    ast.unparse(t) in ("report['consumerInputEqual']","report['performanceCandidate']") for t in n.targets)]
exec(compile(ast.Module(body=flags,type_ignores=[]),str(path),'exec'),env)
assert env['report']['consumerInputEqual'] is False and env['report']['performanceCandidate'] is True
print('PASS actual A31 owner reader positives=2 controls=21 report-flags=1')
