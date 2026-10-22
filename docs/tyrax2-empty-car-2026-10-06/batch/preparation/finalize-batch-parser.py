from pathlib import Path
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1';p=r/'analyze-batch.py';s=p.read_text(encoding='utf8').replace('oneArm=True,matchedAB=True','oneArm=False,matchedAB=True').replace('Phase0 is initial parked; phase1 bounded virtual-pad steering/throttle/braking; phase2 neutral post-drive, not matched savings.','All three phases are parked for ON/OFF/ON or reverse representation comparison.');p.write_bytes(s.encode())
s=(lab/'test-batch-controls.py').read_text(encoding='utf-8-sig').replace('parser-controls-v1','parser-controls-v2')
needle=" trial('missing-batch'+str(order)"
insert=""" broken=s.splitlines();target=next(i for i,l in enumerate(broken)if l.startswith('LOG: LIGHTCONTEXT phase=1 offset=750 '));broken[target]=re.sub(r' x=[0-9a-fA-F]{8}', ' x=3f000000',broken[target]);trial('unmatched-point'+str(order),'\\n'.join(broken)+'\\n',baseart,False)
 broken=s.splitlines();target=next(i for i,l in enumerate(broken)if l.startswith('LOG: CLOSEOUTCONTEXT phase=1 offset=750 '));broken[target]=re.sub(r' positionX=[0-9a-fA-F]{8}', ' positionX=3f000000',broken[target]);trial('unmatched-camera'+str(order),'\\n'.join(broken)+'\\n',baseart,False)
"""
s=s.replace(needle,insert+needle).replace('PASS_SIXTEEN_STATIC_BATCH_PARSER_CONTROLS','PASS_TWENTY_STATIC_BATCH_PARSER_CONTROLS').replace("print('PASS16')","print('PASS20')");(lab/'test-batch-controls-v2.py').write_bytes(s.encode())
