"""A05: strict affine single-run road audit; does not change geometry."""
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

def inspect(h,rows):
    index,ordinal,n,run,blend,edge,sts,colors=h
    result=dict(index=index,ordinal=ordinal,vertices=n,run=run,blend=blend,edge=edge)
    def reject(reason):
        return dict(result,eligible=False,reason=reason)
    if not run or n>run:return reject('not single strip run')
    if blend or edge:return reject('blend or edge')
    if n<6:return reject('already minimal')
    data=list(rows)
    while len(data)>1 and data[-1]==data[-2]:data.pop()
    if len(data)%2 or len(data)<6:return reject('not paired stations')
    first,second,last_left,last_right=data[0],data[1],data[-2],data[-1]
    u0,u1=first[4],second[4]
    v0,v1=first[5],last_left[5]
    if abs(u1-u0)<1e-8 or v1<=v0:return reject('singular UV domain')
    if any(r[3]!=1 or r[6:8]!=(1,0) or r[8:]!=first[8:] for r in data):
        return reject('nonconstant W Q or colour')
    prev=None
    for i in range(0,len(data),2):
        a,b=data[i:i+2]
        if a[4]!=u0 or b[4]!=u1 or a[5]!=b[5] or (prev is not None and a[5]<=prev):
            return reject('not monotone constant-width UV rails')
        prev=a[5]
    du=[(second[j]-first[j])/(u1-u0) for j in range(3)]
    dv=[(last_left[j]-first[j])/(v1-v0) for j in range(3)]
    normal=[du[1]*dv[2]-du[2]*dv[1],du[2]*dv[0]-du[0]*dv[2],du[0]*dv[1]-du[1]*dv[0]]
    if sum(x*x for x in normal)<1e-12:return reject('singular surface')
    errors=[math.sqrt(sum((first[j]+du[j]*(r[4]-u0)+dv[j]*(r[5]-v0)-r[j])**2 for j in range(3))) for r in data]
    result['maxAffinePositionError']=max(errors)
    # Existing lateral plane budget; here a stricter full-position residual.
    if max(errors)>1e-5:return reject('non-affine XYZUV at 1e-5')
    selected=[0,1,len(data)-2,len(data)-1,len(data)-1,len(data)-1]
    return dict(result,eligible=True,reason='affine XYZUV single run',replacementVertices=6,
                selected=selected,rows=[list(data[i]) for i in selected])

def selftest():
    rows=[(u,0,v,1,u,v,1,0,128,128,128,128) for v in range(8) for u in (0,1)]
    h=[0,0,len(rows),75,0,0,len(rows),len(rows)]
    assert inspect(h,rows)['eligible']
    for index,field,value in [(4,1,.001),(4,4,.1),(4,5,0),(4,6,2),(4,8,90)]:
        bad=[list(r) for r in rows];bad[index][field]=value
        assert not inspect(h,[tuple(r) for r in bad])['eligible']
    for field in (4,5):
        bad=list(h);bad[field]=1;assert not inspect(bad,rows)['eligible']
    padded=rows+[rows[-1],rows[-1]]
    hp=list(h);hp[2]=hp[6]=hp[7]=len(padded)
    assert inspect(hp,padded)['eligible']
    reduced=[rows[i] for i in (0,1,14,15,15,15)]
    assert surface_error(rows,reduced)['maxSurfaceReconstructionError']<1e-12
    bad=[list(r) for r in reduced];bad[2][1]=.01
    assert surface_error(rows,bad)['maxSurfaceReconstructionError']>.001
    hole=[list(r) for r in reduced]
    for r in hole:
        if r[5]==7:r[5]=6
    try:surface_error(rows,hole)
    except AssertionError:pass
    else:raise AssertionError('Accepted a UV hole')
    return ['affine','nonplanar','UV rail','UV monotonicity','Q','colour','blend','edge','padding',
            'triangle intersection identity','surface deviation','UV coverage hole']

def inspect_partial(h,rows,full):
    if full['eligible'] or full['reason']!='non-affine XYZUV at 1e-5':return full
    data=list(rows)
    while len(data)>1 and data[-1]==data[-2]:data.pop()
    stations=len(data)//2;kept=[0];span_errors=[]
    start=0
    while start<stations-1:
        end=start+1;error=0.0
        for trial in range(start+2,stations):
            sub=data[2*start:2*(trial+1)];header=list(h)
            header[2]=header[6]=header[7]=len(sub)
            verdict=inspect(header,sub)
            if not verdict['eligible']:break
            end=trial;error=verdict['maxAffinePositionError']
        kept.append(end);span_errors.append(error);start=end
    selected=[i for station in kept for i in (2*station,2*station+1)]
    while len(selected)%3:selected.append(selected[-1])
    if len(selected)>=len(rows):return full
    return dict(full,eligible=True,reason='piecewise affine XYZUV single run',
                maxAcceptedSpanError=max(span_errors),replacementVertices=len(selected),
                selected=selected,rows=[list(data[i]) for i in selected])

def reconstruction_error(original,reduced):
    triangles=[]
    for i in range(len(reduced)-2):
        a,b,c=reduced[i:i+3]
        det=(b[4]-a[4])*(c[5]-a[5])-(b[5]-a[5])*(c[4]-a[4])
        if abs(det)>1e-12:triangles.append((a,b,c,det))
    worst=0.0
    for r in original:
        found=[]
        for a,b,c,det in triangles:
            s=((r[4]-a[4])*(c[5]-a[5])-(r[5]-a[5])*(c[4]-a[4]))/det
            t=((b[4]-a[4])*(r[5]-a[5])-(b[5]-a[5])*(r[4]-a[4]))/det
            if s>=-1e-9 and t>=-1e-9 and s+t<=1+1e-9:
                found.append(math.sqrt(sum((a[j]*(1-s-t)+b[j]*s+c[j]*t-r[j])**2 for j in range(3))))
        assert found,'candidate does not cover an original UV'
        worst=max(worst,max(found))
    return worst

def surface_error(original,reduced):
    def cross(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    def area(poly):
        return abs(sum(poly[i][0]*poly[(i+1)%len(poly)][1]-poly[i][1]*poly[(i+1)%len(poly)][0]
                       for i in range(len(poly))))*.5 if len(poly)>2 else 0
    def triangles(rows):
        out=[]
        for i in range(len(rows)-2):
            vs=rows[i:i+3];uv=[r[4:6] for r in vs]
            if abs(cross(*uv))>1e-12:out.append((vs,uv))
        return out
    def clip(subject,clipper):
        sign=1 if cross(*clipper)>0 else -1
        poly=list(subject)
        for i in range(3):
            a,b=clipper[i],clipper[(i+1)%3];old=poly;poly=[]
            if not old:break
            prev=old[-1];dp=sign*cross(a,b,prev)
            for cur in old:
                dc=sign*cross(a,b,cur)
                if (dp>=0)!=(dc>=0):
                    t=dp/(dp-dc)
                    poly.append((prev[0]+t*(cur[0]-prev[0]),prev[1]+t*(cur[1]-prev[1])))
                if dc>=0:poly.append(cur)
                prev,dp=cur,dc
        return poly
    def point(tri,p):
        vs,uv=tri;a,b,c=uv;det=cross(a,b,c)
        s=((p[0]-a[0])*(c[1]-a[1])-(p[1]-a[1])*(c[0]-a[0]))/det
        t=((b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]))/det
        return [vs[0][j]*(1-s-t)+vs[1][j]*s+vs[2][j]*t for j in range(3)]
    old,new=triangles(original),triangles(reduced)
    total=sum(area(t[1]) for t in old);covered=0;worst=0;intersections=0
    for a in old:
        for b in new:
            poly=clip(a[1],b[1]);part=area(poly)
            if part<=1e-14:continue
            covered+=part;intersections+=1
            for p in poly:
                x,y=point(a,p),point(b,p)
                worst=max(worst,math.sqrt(sum((x[j]-y[j])**2 for j in range(3))))
    assert abs(total-covered)<=1e-8*max(1,total),'UV coverage mismatch'
    return dict(maxSurfaceReconstructionError=worst,uvArea=total,coveredUvArea=covered,intersections=intersections)

def main():
    controls=selftest()
    p=Path(sys.argv[1]);raw=p.read_bytes();at=0
    log=Path(sys.argv[2]).read_text(errors='replace')
    assert 'DRIVEDONE valid=1' in log
    assert re.findall(r'ROADGEOMETRY phase=(\d+) offset=(\d+) chunks=(\d+) vertices=(\d+) valid=(\d+)',log)==[('0','1050','66','26835','1')]
    assert re.findall(r'DRIVEPHASE phase=(\d+) ttl=(\d+) observed=(\d+) valid=(\d+) rows=(\d+).*?variant=(\d+)',log)==[('0','250','0','1','840','0')]
    assert re.findall(r'TURNPINS phase=(\d+) count=(\d+)',log)==[('0','18')]
    assert re.findall(r'NIGHTROADS phase=(\d+) hour=([\d.]+) paused=(\d+) chunks=(\d+) lights=(\d+)',log)==[('0','12.000000','1','66','0')]
    def unpack(fmt):
        nonlocal at
        n=struct.calcsize(fmt);assert at+n<=len(raw),'truncated geometry'
        out=struct.unpack_from(fmt,raw,at);at+=n;return out
    magic,count=unpack('<2I');assert magic==0x31414452 and count==66
    chunks=[];seen=set()
    for ordinal in range(count):
        h=unpack('<8I');index,number,n,run,blend,edge,sts,colors=h
        assert number==ordinal and index not in seen and n==sts==colors and 0<n<=2000 and n%3==0
        assert run in (0,75) and blend in (0,1) and edge in (0,1)
        seen.add(index);rows=[unpack('<12f') for _ in range(n)]
        assert all(math.isfinite(v) for r in rows for v in r)
        verdict=inspect_partial(h,rows,inspect(h,rows))
        if verdict['eligible']:
            error=reconstruction_error(rows,verdict['rows'])
            verdict['maxVertexReconstructionError']=error
            if error>1e-5:verdict.update(eligible=False,reason='reconstruction exceeds 1e-5')
            proof=surface_error(rows,verdict['rows']);verdict.update(proof)
            if proof['maxSurfaceReconstructionError']>1e-5:
                verdict.update(eligible=False,reason='surface reconstruction exceeds 1e-5')
        chunks.append(verdict)
    assert at==len(raw),'trailing geometry bytes'
    candidates=[c for c in chunks if c['eligible']]
    summary=dict(chunks=count,vertices=sum(c['vertices'] for c in chunks),
                 candidates=len(candidates),candidateOriginalVertices=sum(c['vertices'] for c in candidates),
                 candidateReplacementVertices=sum(c['replacementVertices'] for c in candidates))
    assert summary['vertices']==26835
    result=dict(sourceSha256=hashlib.sha256(raw).hexdigest(),logSha256=hashlib.sha256(Path(sys.argv[2]).read_bytes()).hexdigest(),summary=summary,controls=controls,chunks=chunks,
                scope='Conservative geometry opportunity, not PS2 performance or raster/depth equivalence')
    p.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(summary=summary,controls=controls,candidates=[{k:v for k,v in c.items() if k!='rows'} for c in candidates]),indent=2))
if __name__=='__main__':main()
