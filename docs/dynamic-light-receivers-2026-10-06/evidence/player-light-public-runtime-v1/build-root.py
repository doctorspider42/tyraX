"""ROOT ONLY: ordinary editor native build, no run or runtime authority creation."""
from pathlib import Path
import argparse,json,subprocess
from runtime_common import sha,sources,assets
p=argparse.ArgumentParser();p.add_argument('--editor',type=Path,required=True);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--repo',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
assert a.fixture.resolve().parent==Path(__file__).parent/'fixtures';assert len(list(a.fixture.glob('*.tyra')))==1
engine={str(p.resolve()):sha(p)for p in(a.repo/'vendor/tyra').rglob('*')if p.is_file()and p.suffix in('.hpp','.h','.cpp','.c','.vcl','.vsm','.S')}
log=a.out/'native-build.stdout';command=[str(a.editor.resolve()),'--build',str(a.fixture.resolve())];r=subprocess.run(command,capture_output=True);log.write_bytes(r.stdout);(a.out/'native-build.stderr').write_bytes(r.stderr);assert r.returncode==0,'failed normal build preserved'
text=r.stdout.decode(errors='replace');assert '[editor] Compiling natively (PS2DEV + OpenVCL)...'in text and '[editor] === Build OK ==='in text,'actual native backend marker required'
assert all(sha(Path(n))==d for n,d in engine.items()),'engine changed during build'
elfs=list((a.fixture/'bin').glob('*.elf'));assert len(elfs)==1;elf=elfs[0];sym=Path(str(elf)+'.sym');assert sym.is_file()
record=dict(status='PASS_NORMAL_NATIVE_EDITOR_CLI_BUILD_ONLY',command=command,exitCode=r.returncode,fixture=str(a.fixture.resolve()),editor=str(a.editor.resolve()),editorSha256=sha(a.editor),buildLog=str(log.resolve()),buildLogSha256=sha(log),elf=str(elf.resolve()),elfSha256=sha(elf),symbol=str(sym.resolve()),symbolSha256=sha(sym),sources=sources(a.fixture),assets=assets(a.fixture),repoEngineSources=engine,nativeObjectAndELFSymbolReviewPending=True,runtimeAuthorized=False,hardwarePerformanceQualified=False)
(a.out/'build-record.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf8');print(record['status'],sha(a.out/'build-record.json'))
