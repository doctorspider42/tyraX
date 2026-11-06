# Imported by make_arm.py for --headdiag: why crossing-bag head stamps do or do not
# replay. Counts printed every 2000 head attempts (HEADDIAG line).
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    t = p.read_text(encoding='utf-8-sig')
    t = t.replace('void StaPipQBufferRenderer::captureHeadStamp() {\n',
                  'namespace { unsigned hdC[16]; }\n'
                  'static void hdPrint() { printf("HEADDIAG try=%u hit=%u noEntry=%u facts=%u renderer=%u prim=%u tex=%u spot=%u wrap=%u cap=%u capNotOk=%u capNoClip=%u capBig=%u capQw=%u\\n", hdC[0],hdC[1],hdC[2],hdC[3],hdC[4],hdC[5],hdC[6],hdC[7],hdC[8],hdC[9],hdC[10],hdC[11],hdC[12],hdC[13]); fflush(stdout); }\n'
                  'void StaPipQBufferRenderer::captureHeadStamp() {\n', 1)
    # capture accounting
    t = t.replace('  const bool armed = stampArmed && stampOk;\n',
                  '  const bool armed = stampArmed && stampOk;\n'
                  '  ++hdC[9]; if (!stampOk) ++hdC[10]; if (stampClipQw == 0) ++hdC[11];\n'
                  '  if (stampHead) { const unsigned q = (unsigned)(packets[context]->next - stampHead); if (q > kStampMaxQw) { ++hdC[12]; hdC[13] = q; } }\n', 1)
    # try accounting: wrap the reject returns by counting at known points
    t = t.replace('  if (!stampEnabled || !headStampEnabled || stamps == nullptr) return false;\n  sweepStamps();\n  StampEntry* s = findStamp(headKey(bag), false);\n  if (s == nullptr || !s->partial) return false;\n',
                  '  if (!stampEnabled || !headStampEnabled || stamps == nullptr) return false;\n  if ((++hdC[0] % 2000) == 0) hdPrint();\n  sweepStamps();\n  StampEntry* s = findStamp(headKey(bag), false);\n  if (s == nullptr || !s->partial) { ++hdC[2]; return false; }\n', 1)
    t = t.replace('      memcmp(s->infoBytes, bag->info, sizeof(StaPipInfoBag)) != 0)\n    return false;\n  if (memcmp(&s->model, bag->info->model, sizeof(M4x4)) != 0) {\n    s->model = *bag->info->model;\n    stampWorldBox(*s);\n  }\n  // Renderer facts. No baked entry',
                  '      memcmp(s->infoBytes, bag->info, sizeof(StaPipInfoBag)) != 0)\n    { ++hdC[3]; return false; }\n  if (memcmp(&s->model, bag->info->model, sizeof(M4x4)) != 0) {\n    s->model = *bag->info->model;\n    stampWorldBox(*s);\n  }\n  // Renderer facts. No baked entry', 1)
    t = t.replace('      clipBlockVifQw == 0 || clipBlockVifQw != s->clipQw)\n    return false;\n',
                  '      clipBlockVifQw == 0 || clipBlockVifQw != s->clipQw)\n    { ++hdC[4]; return false; }\n', 1)
    t = t.replace('  if (stampPrimKey(bag) != s->primKey) return false;\n  // Visibility is the caller',
                  '  if (stampPrimKey(bag) != s->primKey) { ++hdC[5]; return false; }\n  // Visibility is the caller', 1)
    t = t.replace('  if ((meshSpot.enabled ? 1 : 0) != s->spotActive) return false;\n  {\n    const bool repeatNow =\n        tex == nullptr ||\n        (tex->getWrapSettings()->horizontal == WRAP_REPEAT &&\n         tex->getWrapSettings()->vertical == WRAP_REPEAT);\n    setSubmissionBatchCandidate(batchOk && repeatNow, tex != nullptr);',
                  '  if ((meshSpot.enabled ? 1 : 0) != s->spotActive) { ++hdC[7]; return false; }\n  {\n    const bool repeatNow =\n        tex == nullptr ||\n        (tex->getWrapSettings()->horizontal == WRAP_REPEAT &&\n         tex->getWrapSettings()->vertical == WRAP_REPEAT);\n    setSubmissionBatchCandidate(batchOk && repeatNow, tex != nullptr);', 1)
    t = t.replace('  // Nothing was kicked: the first package must name its program and state.\n  clearLastProgramName();\n',
                  '  // Nothing was kicked: the first package must name its program and state.\n  ++hdC[1];\n  clearLastProgramName();\n', 1)
    assert t.count('hdC[') >= 12, t.count('hdC[')
    p.write_bytes(t.encode())
