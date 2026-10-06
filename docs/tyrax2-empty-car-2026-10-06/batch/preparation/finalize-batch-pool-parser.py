from pathlib import Path
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1';p=r/'analyze-batch.py';s=p.read_text(encoding='utf8').replace('dict(owners=50,parts=300,vertices=70800,matrix=0,impostor=0,valid=1)','dict(owners=50,parts=300,vertices=70800,matrix=0,impostor=0,dirty=0,unmapped=0,empty=0,errors=0,authored=55,mapSize=55,valid=1)');needle="assert stdout.find('LOG: BATCHPREP ')";s=s.replace(needle,"assert n(prepRows[0],'runtimePool')>=55 and n(prepRows[0],'geometryPool')>=55\n"+needle);p.write_bytes(s.encode())
s=(lab/'test-batch-controls-v4.py').read_text(encoding='utf8').replace('parser-controls-v4','parser-controls-v5');s=s.replace('matrix=0 impostor=0 valid=1','matrix=0 impostor=0 dirty=0 unmapped=0 empty=0 errors=0 authored=55 mapSize=55 runtimePool=119 geometryPool=119 valid=1')
needle=" trial('missing-batch'+str(order)";insert=""" trial('undersized-runtime-pool'+str(order),s.replace('runtimePool=119','runtimePool=54'),baseart,False)
 trial('undersized-geometry-pool'+str(order),s.replace('geometryPool=119','geometryPool=54'),baseart,False)
 trial('wrong-authored-map'+str(order),s.replace('mapSize=55','mapSize=56'),baseart,False)
 trial('prep-error-bit'+str(order),s.replace('errors=0 authored=55','errors=2 authored=55'),baseart,False)
""";s=s.replace(needle,insert+needle).replace('PASS_THIRTY_TWO_STATIC_BATCH_PARSER_CONTROLS','PASS_FORTY_STATIC_BATCH_PARSER_CONTROLS').replace("print('PASS32')","print('PASS40')");(lab/'test-batch-controls-v5.py').write_bytes(s.encode())
