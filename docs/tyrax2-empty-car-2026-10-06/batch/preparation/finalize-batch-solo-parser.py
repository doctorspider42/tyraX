from pathlib import Path
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1';p=r/'analyze-batch.py';s=p.read_text(encoding='utf8').replace('BATCHCONTEXT|BATCHPHASE','BATCHCONTEXT|BATCHPHASE|BATCHPREP');needle="batchRows=rows(stdout,'BATCHCONTEXT');";insert="""prepRows=rows(stdout,'BATCHPREP');assert len(prepRows)==1
for key,value in dict(owners=50,parts=300,vertices=70800,matrix=0,impostor=0,valid=1).items():assert n(prepRows[0],key)==value
assert stdout.find('LOG: BATCHPREP ')<stdout.find('LOG: BATCHCONTEXT '),'Solo prep was not complete before first cold witness'
""";s=s.replace(needle,insert+needle);p.write_bytes(s.encode())
s=(lab/'test-batch-controls-v3.py').read_text(encoding='utf8').replace('parser-controls-v3','parser-controls-v4').replace("return '\\n'.join(lines)+'\\n'","return 'LOG: BATCHPREP owners=50 parts=300 vertices=70800 matrix=0 impostor=0 valid=1\\n'+'\\n'.join(lines)+'\\n'")
needle=" trial('missing-batch'+str(order)";insert=""" trial('missing-solo-prep'+str(order),s.replace('LOG: BATCHPREP owners=50 parts=300 vertices=70800 matrix=0 impostor=0 valid=1\\n',''),baseart,False)
 trial('empty-solo-parts'+str(order),s.replace('parts=300 vertices=70800','parts=0 vertices=70800'),baseart,False)
 trial('wrong-solo-input'+str(order),s.replace('parts=300 vertices=70800','parts=300 vertices=70799'),baseart,False)
 trial('duplicate-solo-prep'+str(order),'LOG: BATCHPREP owners=50 parts=300 vertices=70800 matrix=0 impostor=0 valid=1\\n'+s,baseart,False)
""";s=s.replace(needle,insert+needle).replace('PASS_TWENTY_FOUR_STATIC_BATCH_PARSER_CONTROLS','PASS_THIRTY_TWO_STATIC_BATCH_PARSER_CONTROLS').replace("print('PASS24')","print('PASS32')");(lab/'test-batch-controls-v4.py').write_bytes(s.encode())
