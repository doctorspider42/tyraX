"""Extract an emitted TerrainGame method without executing fixture setup."""
import re

def extract(text,name):
 match=re.search(r'(?m)^(?:void|bool|float) TerrainGame::'+name+r'\(',text);assert match,name
 start=match.start();brace=text.index('{',match.end());depth=0;state='code';i=brace
 while i<len(text):
  c=text[i];n=text[i+1:i+2]
  if state=='line':
   if c=='\n':state='code'
  elif state=='comment':
   if c=='*' and n=='/':state='code';i+=1
  elif state in ['"',"'"]:
   if c=='\\':i+=1
   elif c==state:state='code'
  else:
   if c=='/' and n=='/':state='line';i+=1
   elif c=='/' and n=='*':state='comment';i+=1
   elif c in ['"',"'"]:state=c
   elif c=='{':depth+=1
   elif c=='}':
    depth-=1
    if depth==0:return text[start:i+1]
  i+=1
 raise RuntimeError(name)
