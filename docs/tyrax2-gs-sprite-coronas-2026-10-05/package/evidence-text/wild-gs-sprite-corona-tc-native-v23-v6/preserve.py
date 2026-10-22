"""Read-only actual V6 archive derived from the immutable V5 observation helper."""
from pathlib import Path
BASE=Path(__file__).resolve().parent.parent/'wild-gs-sprite-corona-tc-native-v22-v5/preserve.py'
s=BASE.read_text().replace("F=LAB/'wild-gs-sprite-corona-physical-v1'","F=LAB/'wild-gs-sprite-corona-physical-v2'")
s=s.replace("prefix=code.split('# V11 compiler artifacts are bound directly')[0]", "prefix=code.split('# V11 compiler artifacts are bound directly')[0].replace('wild-gs-sprite-corona-physical-v1','wild-gs-sprite-corona-physical-v2').replace('wild-gs-sprite-corona-prototype-v5','wild-gs-sprite-corona-prototype-v6')")
s=s.replace("==2046 and budgets['EEClip']['withBillboardsWords']==1846","==2040 and budgets['EEClip']['withBillboardsWords']==1840")
s=s.replace("['bytes']==4096","['bytes']==4048")
s=s.replace("status='REJECT_ACTUAL_NATIVE_V5_RESIDENT_BUDGET_WITH_BILLBOARDS',blockers=['VU1Clip with unchanged billboards requires2046 words, exceeding draw-finish limit2042 by4. No eviction or execution authorized.']", "status='OBSERVED_ACTUAL_NATIVE_V6_RESIDENT_BUDGET_FITS_RUNTIME_UNVERIFIED',blockers=[]")
s=s.replace("'Actual compiler/link success does not override resident budget rejection.'", "'Native byte/budget observation alone does not establish whole VU semantics or actual runtime output.'")
exec(compile(s,str(BASE),'exec'),{'__file__':__file__,'__name__':'preserve_actual_v6'})
