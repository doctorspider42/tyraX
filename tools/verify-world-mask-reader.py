"""Run the actual transformed all-plane evidence gate and its mutations."""
import ast
import re
from pathlib import Path
path=Path(__file__).with_name('tyrax2-world-mask-analyze.py')
text=path.read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert text.rstrip().endswith(finish)
scope={'__file__':str(path)}
exec(compile(text.rstrip()[:-len(finish)],str(path),'exec'),scope)
tree=ast.parse(scope['source'])
gate=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='maskEvidence')
negative=next(n for n in tree.body if isinstance(n,ast.For) and 'maskEvidence(bad)' in ast.unparse(n))
env={'re':re,'count':6,'report':{'negativeControls':[]}}
exec(compile(ast.Module(body=[gate],type_ignores=[]),str(path),'exec'),env)
env['text']='\n'.join(f'WORLDMASK phase={phase} arm={arm} changed={7 if arm==1 else 0} planes=63 coldOnly=1'
                      for phase in range(6) for arm in range(5))
assert len(env['maskEvidence'](env['text']))==30
exec(compile(ast.Module(body=[negative],type_ignores=[]),str(path),'exec'),env)
assert len(env['report']['negativeControls'])==4
print('PASS actual A31 all-plane reader positive=1 controls=4')
