"""Decode independent wide/pooled first-frame bit snapshots, without physics replay."""
from pathlib import Path
import argparse,hashlib,json,math,struct


def decode(path):
    data=path.read_bytes();assert len(data)%4==0
    words=list(struct.unpack('<'+'I'*(len(data)//4),data));cursor=0
    def take(n):
        nonlocal cursor
        assert 0<=n<=len(words)-cursor,path
        result=words[cursor:cursor+n];cursor+=n;return result
    magic,version,phase,frame=take(4);assert magic==0x504f4f4c and version in (1,2)
    ns,nc=take(2);steps=[];contacts=[]
    names=['sphereR','invMass','bounce','friction']
    for prefix in ['x0','x1','v','w','vp','wp']:names.extend(prefix+'.'+a for a in 'xyz')
    for prefix,count in [('q0',4),('R0',9),('R1',9),('invIw',9)]:names.extend(f'{prefix}[{k}]' for k in range(count))
    for prefix in ['bounds.mn','bounds.mx','obb.centre','obb.axis0','obb.axis1','obb.axis2']:names.extend(prefix+'.'+a for a in 'xyz')
    assert len(names)==71
    for _ in range(ns):
        step=dict(zip(['obj','slot','asleep','grounded','sphere','nv','box'],take(7)));assert step['nv']<=24
        step.update(zip(names,take(71)))
        if version==2:
            for k in range(step['nv']):step.update(zip([f'lv[{k}].{a}' for a in 'xyz'],take(3)))
        for prefix in ['wv0','wv']:
            for k in range(step['nv']):step.update(zip([f'{prefix}[{k}].{a}' for a in 'xyz'],take(3)))
        steps.append(step)
    for _ in range(nc):contacts.append(dict(zip(['a','b','other','n.x','n.y','n.z','p.x','p.y','p.z','depth'],take(10))))
    assert cursor==len(words),path
    return dict(phase=phase,frame=frame,version=version,steps=steps,contacts=contacts,sha256=hashlib.sha256(data).hexdigest())


def differences(left,right):
    result=[]
    integer_fields={'obj','slot','asleep','grounded','sphere','nv','box','a','b','other'}
    def floating(key,word):
        if word is None or key in integer_fields:return None
        value=struct.unpack('<f',struct.pack('<I',word))[0]
        return value if math.isfinite(value) else str(value)
    for i,(x,y) in enumerate(zip(left,right)):
        for key in list(x)+[k for k in y if k not in x]:
            if x.get(key)!=y.get(key):
                a,b=x.get(key),y.get(key)
                result.append(dict(index=i,object=x.get('obj'),field=key,wide_bits=a,pooled_bits=b,
                    wide_float=floating(key,a),pooled_float=floating(key,b)))
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    results=[]
    for frame in [0,1]:
        wide=decode(a.root/f'step-pool-12-{frame}.bin');pooled=decode(a.root/f'step-pool-13-{frame}.bin')
        assert wide['version']==pooled['version']
        sd=differences(wide['steps'],pooled['steps']);cd=differences(wide['contacts'],pooled['contacts'])
        results.append(dict(frame=frame,snapshot_version=wide['version'],step_counts=[len(wide['steps']),len(pooled['steps'])],contact_counts=[len(wide['contacts']),len(pooled['contacts'])],
            step_field_differences=len(sd),contact_field_differences=len(cd),first_step_differences=sd[:24],first_contact_differences=cd[:24],
            wide_sha256=wide['sha256'],pooled_sha256=pooled['sha256']))
    result=dict(frames=results,scope='Exact words from initialized pre-solver step fields, all actual vertices, and ordered contact geometry at frames0/1. No scalar replay and no performance qualification. Count differences are reported separately from zipped field comparisons.')
    a.output.write_text(json.dumps(result,indent=2),encoding='utf8')
    for r in results:print(json.dumps(r))


if __name__=='__main__':main()
