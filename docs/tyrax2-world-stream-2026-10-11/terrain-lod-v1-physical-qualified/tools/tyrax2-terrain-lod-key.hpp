#pragma once
namespace TerrainLodKey {
struct Key { int lod,lodW,lodE,lodN,lodS; };
inline int relevant(int neighbour,int own,bool boundary){
  return !boundary && neighbour>own ? neighbour:0;
}
inline bool equivalent(Key a,Key b,int cx,int cz,int chunksX,int chunksZ){
  return a.lod==b.lod &&
    relevant(a.lodW,a.lod,cx==0)==relevant(b.lodW,b.lod,cx==0) &&
    relevant(a.lodE,a.lod,cx==chunksX-1)==relevant(b.lodE,b.lod,cx==chunksX-1) &&
    relevant(a.lodN,a.lod,cz==0)==relevant(b.lodN,b.lod,cz==0) &&
    relevant(a.lodS,a.lod,cz==chunksZ-1)==relevant(b.lodS,b.lod,cz==chunksZ-1);
}
}
