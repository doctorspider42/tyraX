"""Root-invoked after successful native build; restore only authored baked resources."""
from pathlib import Path
import argparse,hashlib,json,shutil
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--fixture',type=Path,required=True);p.add_argument('--build-log',type=Path,required=True);p.add_argument('--reported-exit-code',type=int,choices=[0],required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists(),'unique provenance file required'
manifest=a.fixture/'target-source-manifest.json';source=json.loads(manifest.read_text())['files']
for rel,digest in source.items():assert sha(a.fixture/rel)==digest,'source drift '+rel
bin=a.fixture/'game/bin';elf=bin/'vehicle-playground.elf';assert elf.is_file() and elf.with_suffix('.elf.sym').is_file();log=a.build_log.read_text(errors='replace');assert '[editor] Native build complete:' in log,'native publication marker required';path=str(a.fixture).replace('\\','/').lower();wsl='/mnt/'+path[0]+path[2:] if len(path)>1 and path[1]==':' else path;assert path in log.replace('\\','/').lower() or wsl in log.replace('\\','/').lower(),'wrong fixture build log'
baked=a.fixture/'game/.res-baked';restored=[];rows=[];encoded=[]
for src in baked.rglob('*'):
 if not src.is_file():continue
 rel=src.relative_to(baked)
 # Mirrors Makefile.base top-level gi/shadow exclusion and POSIX glob omission.
 if rel.parts[0] in ('gi','shadow') or rel.parts[0].startswith('.'):continue
 dst=bin/rel
 if src.suffix.lower()=='.wav' and dst.with_suffix('.adpcm').is_file():
  converted=dst.with_suffix('.adpcm');assert '[editor] adpenc ' in log and str(rel).replace('\\','/') in log,'ADPCM build evidence missing'
  assert converted.stat().st_size>16,'truncated ADPCM';encoded.append({'source':str(rel).replace('\\','/'),'source_wav_sha256':sha(src),'runtime':str(converted.relative_to(bin)).replace('\\','/'),'runtime_sha256':sha(converted),'byte_equivalence_to_wav':False,'conversion_provenance':'successful native build adpenc record; no re-encoding in helper'});continue
 if not dst.is_file() or sha(dst)!=sha(src):
  restored.append({'path':str(rel).replace('\\','/'),'before':sha(dst) if dst.is_file() else None,'after':sha(src)});dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
 assert sha(dst)==sha(src);rows.append({'path':str(rel).replace('\\','/'),'sha256':sha(dst)})
asset_report={'verified_byte_equal_baked_assets':len(rows),'verified_converted_adpcm_assets':len(encoded),'restored_assets':len(restored),'restored':restored,'byte_equal_assets':rows,'converted_audio':encoded,'source_code_changed':False,'coverage':'current authored baked resources, gi/shadow and top-level dotfiles excluded exactly as Makefile; WAV files deliberately transformed by native adpenc'}
report_path=a.fixture/'runtime-assets-restored.json';assert not report_path.exists(),'unique initial asset report required';report_path.write_text(json.dumps(asset_report,indent=2)+'\n')
assets={str(f.relative_to(bin)).replace('\\','/'):sha(f) for f in bin.rglob('*') if f.is_file() and f.suffix not in ('.elf','.sym','.cfg','.log','.run')};(a.fixture/'runtime-assets-manifest.json').write_text(json.dumps({'files':assets},indent=2)+'\n')
record={'status':'quiet_native_build_and_assets_verified','build_backend':'native','build_exit_code':a.reported_exit_code,'exit_code_source':'root process result; exact build log preserved','selected_elf_sha256':sha(elf),'selected_symbol_sha256':sha(elf.with_suffix('.elf.sym')),'target_source_manifest_sha256':sha(manifest),'build_log':str(a.build_log),'build_log_sha256':sha(a.build_log),'verified_source_files':len(source),'runtime_assets':len(assets),'current_authored_baked_byte_equal_assets':len(rows),'converted_audio_assets':len(encoded),'asset_report_sha256':sha(report_path),'quiet_profile0_hardware_trace0':True,'ordinary_clocks':True};a.out.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
