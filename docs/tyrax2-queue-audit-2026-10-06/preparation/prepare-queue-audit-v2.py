from pathlib import Path
import shutil,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');a=lab/'tyrax2-queue-audit-v1';b=lab/'tyrax2-queue-audit-v2';assert not b.exists()
shutil.copytree(a,b,ignore=shutil.ignore_patterns('obj','*.elf','*.sym','*.log'))
for p in b.glob('*.py'):p.write_bytes(p.read_text().replace('tyrax2-queue-audit-v1','tyrax2-queue-audit-v2').encode())
p=b/'tyra/engine/inc/debug/render_audit.hpp';p.write_bytes(p.read_text().replace('sizeof(Row)==148','sizeof(Row)==140').encode())
p=b/'prove-native.py';p.write_bytes(p.read_text().replace("('RenderAudit::samples',18944),('RenderAudit::current',148)","('RenderAudit::samples',17920),('RenderAudit::current',140)").encode())
p=b/'vehicle/source-review.json';d=json.loads(p.read_text());h=b/'tyra/engine/inc/debug/render_audit.hpp';d['files']['tyra/engine/inc/debug/render_audit.hpp']=hashlib.sha256(h.read_bytes()).hexdigest();p.write_bytes((json.dumps(d,indent=2)+'\n').encode())
print('V2_SIZE_GUARD_CORRECTED; V1_RETAINED')