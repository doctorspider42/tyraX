#pragma once
#include <tamtypes.h>

// The hardware DMAtag, laid out as ps2sdk's own dma_tags.h lays it out. Getting
// this wrong is not subtle - ID sits at bits 28..30, and a stub that put it at
// 26..28 made every REF tag read as a CNT and walked the decoder off the end of
// the chain.
typedef struct {
  unsigned int QWC : 16;
  unsigned int : 10;
  unsigned int PCE : 2;
  unsigned int ID : 3;
  unsigned int IRQ : 1;
  unsigned int ADDR : 31;
  unsigned int SPR : 1;
} dma_tag_t;
