from pathlib import Path
import json,subprocess,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');parent=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-v1';root=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';game=root/'empty-car';proof=[]
s=(lab/'prepare-shadow.py').read_text(encoding='utf8');exec(s[s.index("p=game/'src/gen/game_lighting.gen.cpp'"):])
