"""Exercise the actual transformed A31 execution gate and all mutations."""
import ast
import re
from pathlib import Path
path=Path(__file__).with_name('tyrax2-world-body-analyze.py')
text=path.read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert text.rstrip().endswith(finish)
scope={'__file__':str(path)}
exec(compile(text.rstrip()[:-len(finish)],str(path),'exec'),scope)
tree=ast.parse(scope['source'])
gate=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='worldEvidence')
negative=next(n for n in tree.body if isinstance(n,ast.For) and
              'worldEvidence(bad)' in ast.unparse(n))
env={'re':re,'count':6,'report':{'negativeControls':[]}}
exec(compile(ast.Module(body=[gate],type_ignores=[]),str(path),'exec'),env)
env['text']='\n'.join(f'WORLDCHAIN phase={phase} arm={arm} checks={7 if arm==1 else 0} words={900 if arm==1 else 0} liveHeads=1 coldOnly=1'
                      for phase in range(6) for arm in range(5))
assert len(env['worldEvidence'](env['text']))==30
exec(compile(ast.Module(body=[negative],type_ignores=[]),str(path),'exec'),env)
assert len(env['report']['negativeControls'])==6
flags=[node for node in tree.body if isinstance(node,ast.Assign) and
       any(ast.unparse(t)=="report['performanceCandidate']" for t in node.targets)]
assert flags
exec(compile(ast.Module(body=flags,type_ignores=[]),str(path),'exec'),env)
assert env['report']['performanceCandidate'] is False
print('PASS actual A31 compiled execution reader positive=1 controls=6 no-performance-flag=1')
