"""Private kind29 cold schema controls; counters do not prove VU/GS output."""
FIELDS='invocations sourceGroups sourceMembers groups members candidateGroups candidateMembers controlMainSubmits candidateMainSubmits reflectionCarriers reflectionUses fallbackGroups demotedGroups eligibilityRefused dirtyDemotions geometryCompared geometryMismatches duplicateMainSubmits verticesCompared colorBytesCompared'.split()
GROUP_FIELDS='group ready admitted demoted submitted first count members geometryHash infoHash lightKey spotEnabled'.split()
MEMBER_FIELDS='ordinal object idLo idHi group ready admitted mainOriginal mainGrouped reflectionUses count positionHash colorHash copiedPositionHash copiedColorHash infoHash lightKey spotEnabled'.split()
def check(stdout,flags,loop):
 need,n=loop.need,loop.number;records=loop.rows(stdout,'NIGHTMAINBATCH');groupRows=loop.rows(stdout,'NIGHTMAINBATCHGROUP');memberRows=loop.rows(stdout,'NIGHTMAINBATCHMEMBER');windows={};groups={};members={}
 for row in records:
  need(set(row)==set(['phase','offset','enabled']+FIELDS),'exact batch aggregate schema');p,o=n(row,'phase'),n(row,'offset');key=(p,o);need(p in range(3)and o in(750,1155)and key not in windows,'batch cold aggregate ownership');need(n(row,'enabled')==flags[p],'batch actual arm');windows[key]=row
 need(set(windows)=={(p,o)for p in range(3)for o in(750,1155)},'six cold batch records')
 for label,rows,fields,target,index in [('group',groupRows,GROUP_FIELDS,groups,'group'),('member',memberRows,MEMBER_FIELDS,members,'ordinal')]:
  for row in rows:
   need(set(row)==set(['phase','offset','enabled']+fields),'exact batch '+label+' schema');p,o=n(row,'phase'),n(row,'offset');key=(p,o,n(row,index));need((p,o)in windows and key not in target,'batch '+label+' ownership/duplicate');need(n(row,'enabled')==flags[p],'batch '+label+' arm');target[key]=row
 result={};activated=[]
 for(p,o),row in windows.items():
  c={k:n(row,k)for k in FIELDS};need(c['invocations']>0,'entered cold main view');need(c['geometryMismatches']==c['duplicateMainSubmits']==0,'batch semantic/duplicate mismatch')
  gg={i:v for(pp,oo,i),v in groups.items()if(pp,oo)==(p,o)};mm={i:v for(pp,oo,i),v in members.items()if(pp,oo)==(p,o)}
  need(set(gg)==set(range(c['sourceGroups']))and set(mm)==set(range(c['sourceMembers'])),'source group/member inventory rows')
  need(c['groups']<=c['sourceGroups']and c['members']<=c['sourceMembers'],'admitted source subsets');need(c['candidateGroups']<=c['groups']and c['candidateMembers']<=c['members'],'actual candidate subsets')
  need(c['candidateMainSubmits']==c['candidateGroups'],'actual one submit per replaced group');need(flags[p]or c['candidateGroups']==c['candidateMembers']==c['candidateMainSubmits']==0,'Off has zero grouped replacements')
  need(c['verticesCompared']==c['geometryCompared']and c['colorBytesCompared']==16*c['verticesCompared'],'full position/color vertex oracle count')
  for name in('fallbackGroups','demotedGroups'):need(c[name]<=c['sourceGroups'],'fallback/demotion group subset')
  for name in('eligibilityRefused','dirtyDemotions'):need(c[name]<=c['sourceMembers'],'fallback/demotion member event bound')
  covered=set();admittedMembers=actualMembers=0;originals=grouped=reflectUses=carriers=oracleVertices=0
  for i,g in gg.items():
   v={k:n(g,k)for k in GROUP_FIELDS};need(all(v[k]<=1 for k in('ready','admitted','demoted','submitted','spotEnabled')),'group booleans');need(v['admitted']<=v['ready']and v['submitted']<=v['admitted']and not(v['demoted']and v['admitted']),'group ready/admission/demotion');need(flags[p]or v['submitted']==0,'Off group not submitted');need(v['members']>0,'nonempty source group')
   owned=list(range(v['first'],v['first']+v['members']));need(all(j in mm for j in owned)and not covered.intersection(owned),'exact nonoverlap membership');covered.update(owned)
   if v['ready']:
    need(sum(n(mm[j],'count')for j in owned)==v['count'],'ready copied group vertex source denominator');oracleVertices+=v['count']
   else:need(v['count']==0,'unready group has no copied vertex denominator')
   admittedMembers+=v['members']*v['admitted'];actualMembers+=v['members']*v['submitted']
   for j in owned:
    m={k:n(mm[j],k)for k in MEMBER_FIELDS};need(m['group']==i,'member declared group');need(all(m[k]<=1 for k in('ready','admitted','mainOriginal','mainGrouped','spotEnabled')),'member booleans');need(m['mainOriginal']+m['mainGrouped']<=1,'no duplicate selected original/grouped main');need(m['mainGrouped']==v['submitted'],'actual grouped membership coverage')
    if v['submitted']:
     need(m['ready']==m['admitted']==1 and m['count']>0,'actual replacement member readiness');need(m['positionHash']==m['copiedPositionHash']and m['colorHash']==m['copiedColorHash'],'actual replacement source/copy semantic hash witnesses');need(m['infoHash']==v['infoHash']and m['lightKey']==v['lightKey']and m['spotEnabled']==v['spotEnabled'],'actual replacement info/light key witnesses')
    originals+=m['mainOriginal'];grouped+=m['mainGrouped'];reflectUses+=m['reflectionUses'];carriers+=m['ready']
  need(covered==set(mm),'source member complete identity partition');need(len({(n(m,'idLo'),n(m,'idHi'))for m in mm.values()})==len(mm),'unique authored source member IDs');need(len({n(m,'object')for m in mm.values()})==len(mm),'unique source object indices')
  need(c['groups']==sum(n(g,'admitted')for g in gg.values())and c['members']==admittedMembers,'admitted aggregate identity partition');need(c['candidateGroups']==sum(n(g,'submitted')for g in gg.values())and c['candidateMembers']==actualMembers==grouped,'actual grouped aggregate identity partition');need(c['controlMainSubmits']==originals and c['reflectionUses']==reflectUses,'actual original/reflection hook sums');need(c['reflectionCarriers']==carriers,'actual ready original reflection carrier sum');need(c['geometryCompared']==oracleVertices,'full-word oracle covers every ready copied group')
  if c['candidateGroups']:
   need(c['candidateMembers']>0 and c['geometryCompared']>0,'positive actual replacement requires full-word oracle');activated.append((p,o))
  result[f'{p}:{o}']=dict(aggregate=row,groups=gg,members=mm)
 return dict(coldWindows=result,candidateActivatedInCold=bool(activated),activatedColdWindows=activated,candidatePricingActivationQualified=bool(activated),fallbackOnlyCompletedCapture=not activated,fullTimedWindowActivationKnown=False,semanticColdCounterAndIdentityControlsPassed=True,fullPositionColorOracleWordsPerVertex=8,textureSTInactiveEligibilitySourceReviewRequired=True,actualVUOrGSOutputQualified=False,reflectionPacketCadenceQualified=False,sourceCountersAreNotNativeExecutionProof=True)
