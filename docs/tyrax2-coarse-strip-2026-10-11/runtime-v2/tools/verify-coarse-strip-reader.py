"""Exercise actual generated A30 cold reader without waiting for an ELF run."""
import ast
import re
from pathlib import Path

path=Path(__file__).with_name('tyrax2-coarse-strip-analyze.py')
text=path.read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert text.count(finish)==1
scope={'__file__':str(path)}
exec(compile(text.replace(finish,''),str(path),'exec'),scope)
tree=ast.parse(scope['source'])
cold=next(node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name=='cold')
negative=next(node for node in tree.body if isinstance(node,ast.For) and
              isinstance(node.target,ast.Tuple) and ast.unparse(node.target)=='(name, bad)')
env={'re':re,'count':6,'plan':{},'report':{'negativeControls':[]}}
exec(compile(ast.Module(body=[cold],type_ignores=[]),str(path),'exec'),env)
lines=[]
for phase in range(6):
    for arm in range(5):
        candidate=arm==1
        lines+= [
            f'CONSUMERINPUT phase={phase} arm={arm} calls={500 if candidate else 200} verts={1500 if candidate else 600} hash={13 if candidate else 11}',
            f'HEADERORACLE phase={phase} arm={arm} checks={40 if candidate else 80}',
            f'COARSESOURCE phase={phase} arm={arm} packages=8 triangles=500 vertices=1500 hash=17',
            f'COARSEDELIVERY phase={phase} arm={arm} jobs={8 if candidate else 0} triangles={500 if candidate else 0} vertices={1500 if candidate else 0} hash={17 if candidate else 2166136261}',
            f'COARSERESIDUAL phase={phase} arm={arm} triangles=50 vertices=150 hash=19',
            f'RINGCONTRACT phase={phase} arm={arm} calls=8 triangles={500 if candidate else 200}',
        ]
env['text']='\n'.join(lines)
inputs,checks=env['cold'](env['text'])
assert len(inputs)==len(checks)==30
exec(compile(ast.Module(body=[negative],type_ignores=[]),str(path),'exec'),env)
assert len(env['report']['negativeControls'])==12
flags=[node for node in tree.body if isinstance(node,ast.Assign) and
       any(ast.unparse(target)=="report['consumerInputEqual']" for target in node.targets)]
assert flags
exec(compile(ast.Module(body=flags,type_ignores=[]),str(path),'exec'),env)
assert env['report']['consumerInputEqual'] is False
# Delivery hashes alone are not enough: enforce counts and all source arms.
for marker in ('COARSESOURCE phase=0 arm=2 packages=8 triangles=500',
               'COARSEDELIVERY phase=0 arm=1 jobs=8 triangles=500'):
    bad=env['text'].replace(marker,marker.replace('triangles=500','triangles=499'))
    try:env['cold'](bad)
    except AssertionError:pass
    else:raise AssertionError('Count control escaped')
print('PASS actual A30 cold reader positive=1 controls=14 report-flags=1')
