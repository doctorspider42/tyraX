"""Host geometry/lifetime oracle for A04; run in WSL or Linux."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
root=Path(sys.argv[1]).resolve()
tools=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(tools/'verify-sky-bounds.py'),str(root)],check=True)
header=root/'tyra/engine/inc/debug/sky_prepared.hpp'
with tempfile.TemporaryDirectory() as tmp:
    exe=Path(tmp)/'verify'
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined',
                    '-I'+str(header.parent),str(tools/'verify-sky-prepared.cpp'),'-o',str(exe)],check=True)
    run=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
assert run.stdout.strip()=='191828 prepared slices verified',run.stdout
result=dict(passed=True,slices=191828,sanitizers='ASan/UBSan',
            headerSha256=hashlib.sha256(header.read_bytes()).hexdigest(),
            controls=['parent-relative split','small direct split','tail','version','count','maximum',
                      'offset','length','replacement buffer','same-buffer rebuild','nonfinite','empty'])
(root/'sky-prepared-host.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
