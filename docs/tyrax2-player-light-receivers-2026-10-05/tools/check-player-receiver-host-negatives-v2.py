from pathlib import Path
import json,importlib.util
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'player-light-receivers-controls-v2';sp=importlib.util.spec_from_file_location('analysis',c/'analyze-night.py');mod=importlib.util.module_from_spec(sp);sp.loader.exec_module(mod);e=b/'night-ablation-emulator-player-receivers-kind10-order0-20261005-evidence';raw=(e/'stdout.log').read_text();artifact=(e/'night-ablation.log').read_text();r=mod.analyze(raw,artifact,'emulator',10,0,0,0);assert r['status'].startswith('PASS_')
rows=[]
def reject(name,text,kind=10,order=0):
 try:mod.analyze(text,artifact,'emulator',kind,order,0,0)
 except (ValueError,AssertionError) as ex:rows.append(dict(name=name,rejected=True,reason=str(ex)));return
 raise AssertionError('accepted bad input '+name)
line=next(x for x in raw.splitlines()if 'NIGHTRECEIVERGATES phase=1 offset=750' in x)
reject('missing_receiver_gate',raw.replace(line+'\n',''))
reject('duplicate_receiver_gate',raw+'\n'+line+'\n')
reject('wrong_receiver_mode',raw.replace(line,line.replace('mode=1','mode=0')))
reject('no_player_receiver_witness',raw.replace(line,line.replace('bagAllowed=5','bagAllowed=0')))
reject('negative_counter',raw.replace(line,line.replace('bagAllowed=5','bagAllowed=-1')))
reject('overflow_counter',raw.replace(line,line.replace('bagAllowed=5','bagAllowed=4294967296')))
reject('wrong_order',raw,order=1)
reject('wrong_kind',raw,kind=11)
reject('unexpected_pool_cut',raw.replace('extraMask=0 appliedExtraMask=0','extraMask=1 appliedExtraMask=1',1))
reject('wrong_receiver_schema',raw.replace(line,line+' surprise=0'))
reject('missing_receiver_phase', '\n'.join(x for x in raw.splitlines()if 'NIGHTRECEIVERPHASE phase=1' not in x))
reject('incomplete_final',raw.replace('NIGHTDONE','NIGHTINCOMPLETE'))
p=b/'player-receiver-host-negatives-v2.json';assert not p.exists();p.write_bytes((json.dumps(dict(status='PASS_REAL_KIND10_CAPTURE_AND_12_NEGATIVE_CONTROLS',positiveEvidence=str(e),controls=rows),indent=2)+'\n').encode());print(len(rows))
