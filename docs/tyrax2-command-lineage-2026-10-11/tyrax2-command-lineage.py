"""Offline A18 owned-command structure; actual bound VIF parser, no timing claims."""
import argparse
import collections
import hashlib
import json
import math
import re
import struct
import subprocess
import tempfile
from pathlib import Path


def flatten(data, qwords):
    assert 0 < qwords <= 65536 and len(data) % 16 == 0
    assert qwords * 16 < len(data)
    tags, cursor = [], 0
    while cursor < qwords:
        tag = struct.unpack_from('<4I', data, cursor * 16)
        size, kind = tag[0] & 65535, (tag[0] >> 28) & 7
        assert not tag[0] & 0x8c000000 and kind in (1, 3, 7)
        tags.append((cursor, tag, size, kind))
        cursor += 1 if kind == 3 else size + 1
        assert cursor <= qwords
        if kind == 7:
            assert cursor == qwords
            break
    assert tags[-1][3] == 7
    first = next(tag for _, tag, size, kind in tags if kind == 3 and size)
    base = first[1] - qwords * 16
    assert 0x1000 <= base < 0x2000000 and base % 64 == 0
    payload_cursor = qwords * 16
    words, refs = [], []
    for at, tag, size, kind in tags:
        words.extend(tag[2:])
        if kind == 3:
            offset = tag[1] - base
            if size:
                assert offset == payload_cursor and offset % 16 == 0
                assert 0 <= offset <= len(data) and size * 16 <= len(data) - offset
                refs.append(dict(tag=at, address=tag[1], offset=offset, bytes=size * 16))
                payload_cursor += size * 16
        else:
            offset = (at + 1) * 16
        if size:
            words.extend(struct.unpack_from('<' + str(size * 4) + 'I', data, offset))
    assert payload_cursor == len(data)
    return words, dict(base=base, tags=len(tags), references=refs, ownedBytes=len(data))


def programs(elf, resident, symbol_text):
    assert elf[:7] == b'\x7fELF\x01\x01\x01'
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', elf)
    section_offset, section_size, count = header[6], header[11], header[12]
    assert section_size == 40
    sections = [struct.unpack_from('<10I', elf, section_offset + i * section_size) for i in range(count)]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        assert section[9] == 16 and section[5] % 16 == 0
        strings = sections[section[6]]
        names = elf[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], 16):
            name, value, size, info, other, index = struct.unpack_from('<IIIBBH', elf, offset)
            text = names[name:names.index(b'\0', name)].decode()
            if text.startswith('StaPipVU1') and text.endswith(('_CodeStart', '_CodeEnd')):
                symbols[text] = (value, index)
    if not symbols:
        # The deployed ELF is stripped; use the hash-pinned native nm export.
        for address, name in re.findall(r'^([0-9a-f]{8}) T (StaPipVU1\w+_Code(?:Start|End))$', symbol_text, re.M):
            value = int(address, 16)
            candidates = [i for i, section in enumerate(sections)
                          if section[1] == 1 and section[3] <= value <= section[3] + section[5]]
            # End symbols may coincide with the next section; resolve start/end together below.
            symbols[name] = (value, candidates)
    result = []
    for name, (start, index) in sorted(symbols.items()):
        if not name.endswith('_CodeStart'):
            continue
        end, end_index = symbols[name[:-len('_CodeStart')] + '_CodeEnd']
        if isinstance(index, list):
            candidates = [i for i in index if i in end_index and
                          sections[i][3] <= start < end <= sections[i][3] + sections[i][5]]
            assert len(candidates) == 1, name
            index = end_index = candidates[0]
        assert index == end_index and 0 < end - start <= 16384 and (end - start) % 8 == 0
        section = sections[index]
        offset = section[4] + start - section[3]
        assert section[3] <= start <= end <= section[3] + section[5]
        image = elf[offset:offset + end - start]
        locations = [i // 8 for i in range(0, len(resident) - len(image) + 1, 8)
                     if resident[i:i + len(image)] == image]
        assert len(locations) <= 1, name
        result.append(dict(name=name, instructions=len(image) // 8,
                           residentStart=locations[0] if locations else None,
                           sha256=hashlib.sha256(image).hexdigest()))
    assert result
    return result


def analyze(root, output):
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    report = json.loads((root / 'command-price-physical.json').read_text())
    assert report['valid'] and report['rasterEqual'] and report['presentationFence']
    assert report['target'] == 'physical' and report['legacySamplerSuspended']
    binding = json.loads((root / 'native-binding.json').read_text())
    inventory = json.loads((root / 'archive-sha256.json').read_text())
    used_files = ['command-price-physical.json', 'native-binding.json', 'symbols.txt',
                  'physical/console.log', 'physical/bin/vehicle-playground.elf',
                  'physical/bin/command-closure.bin', 'physical/bin/command-program-before.bin',
                  'physical/bin/command-program-after.bin']
    for rel in used_files:
        assert sha(root / rel) == inventory[rel], rel
    folder = root / 'physical/bin'
    elf = folder / 'vehicle-playground.elf'
    log = root / 'physical/console.log'
    closure = folder / 'command-closure.bin'
    assert sha(elf) == binding['elfSha256'] == report['elfSha256']
    assert sha(log) == report['logSha256']
    assert sha(closure) == report['closure']['sha256']
    engine = root / 'tyra/engine'
    for rel in ['inc/renderer/core/paths/path1/vif1_chain_check.hpp',
                'inc/renderer/core/paths/path1/frame_chain_arena.hpp',
                'inc/renderer/3d/pipeline/static/core/programs/stapip_vu1_shared_defines.h',
                'src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp']:
        assert sha(engine / rel) == binding['files']['tyra/engine/' + rel]
        assert sha(engine / rel) == inventory['tyra/engine/' + rel]
    defines = (engine / 'inc/renderer/3d/pipeline/static/core/programs/stapip_vu1_shared_defines.h').read_text()
    assert re.search(r'^#define VU1_STAPIP_COUNT_MASK 0x03FF$', defines, re.M)
    assert sha(folder / 'command-program-before.bin') == sha(folder / 'command-program-after.bin')
    words, dma = flatten(closure.read_bytes(), report['closure']['qwords'])
    assert dma['ownedBytes'] == report['closure']['bytes']
    images = programs(elf.read_bytes(), (folder / 'command-program-before.bin').read_bytes(),
                      (root / 'symbols.txt').read_text())
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        binary, stream = temp / 'oracle', temp / 'stream.bin'
        subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(engine / 'inc'), str(Path(__file__).with_name('verify-command-lineage.cpp')),
                        '-o', str(binary)], check=True)
        stream.write_bytes(struct.pack('<' + str(len(words)) + 'I', *words))
        run = subprocess.run([str(binary), str(stream)], capture_output=True, text=True, check=True)
        commands = [tuple(map(int, row.split())) for row in run.stdout.splitlines()]
        assert commands and all(i < len(words) and value == words[i] for i, value, _ in commands)
        counters, calls, pending_uploads = collections.Counter(), [], []
        last_program, last_mvp = None, None
        for index, word, payload_words in commands:
            opcode = (word >> 24) & 127
            counters[f'{opcode:02x}'] += 1
            if opcode & 0x60 == 0x60:
                pending_uploads.append(dict(word=index, command=word, destination=word & 1023,
                                            topRelative=bool(word & 0x8000), vectors=(word >> 16) & 255 or 256,
                                            payloadBytes=payload_words * 4))
                if not word & 0x8000 and word & 1023 == 0 and payload_words == 16:
                    last_mvp = index
            if opcode in (0x14, 0x15, 0x17):
                if opcode != 0x17:
                    last_program = word & 65535
                assert last_program is not None
                matches = [p['name'] for p in images if p['residentStart'] is not None and
                           p['residentStart'] <= last_program < p['residentStart'] + p['instructions']]
                call = dict(word=index, address=last_program, exactResidentImages=matches,
                            continuation=opcode == 0x17, uploads=pending_uploads, mvpUploadWord=last_mvp)
                headers = [u for u in pending_uploads if u['topRelative'] and u['destination'] == 0]
                assert len(headers) == 1 and headers[0]['payloadBytes'] == 32
                header_word = headers[0]['word'] + 1
                packed_count = words[header_word + 3]
                gif_low = words[header_word + 4] | words[header_word + 5] << 32
                call['headerCount'] = packed_count & 0x3ff
                call['gsPrimitive'] = (gif_low >> 47) & 7
                positions = [u for u in pending_uploads if u['topRelative'] and u['destination'] == 2
                             and (u['command'] >> 24) & 127 == 0x6c]
                if len(positions) == 1:
                    u = positions[0]
                    assert u['payloadBytes'] == u['vectors'] * 16
                    assert call['headerCount'] == u['vectors']
                    data = struct.pack('<' + str(u['vectors'] * 4) + 'I',
                                       *words[u['word'] + 1:u['word'] + 1 + u['vectors'] * 4])
                    values = list(struct.iter_unpack('<4f', data))
                    assert all(all(math.isfinite(v) for v in row) for row in values)
                    call['inputAtRelative2'] = dict(vectors=u['vectors'], sha256=hashlib.sha256(data).hexdigest(),
                                                   minimum=[min(v[i] for v in values) for i in range(4)],
                                                   maximum=[max(v[i] for v in values) for i in range(4)])
                calls.append(call)
                pending_uploads = []
        assert len(calls) == report['closure']['vuCalls'] and counters['4a'] == 0
        negatives = []
        for name, data in [('truncated-closure', closure.read_bytes()[:-16]),
                           ('changed-reference', bytearray(closure.read_bytes()))]:
            if name == 'changed-reference':
                tag = dma['references'][0]['tag']
                struct.pack_into('<I', data, tag * 16 + 4, dma['references'][0]['address'] + 16)
            try:
                flatten(data, report['closure']['qwords'])
            except AssertionError:
                negatives.append(name)
            else:
                raise AssertionError('DMA negative escaped: ' + name)
        # Truncate an actual final payload, not an arbitrary padding word.
        payload = next(row for row in reversed(commands) if row[2] > 0)
        stream.write_bytes(struct.pack('<' + str(payload[0] + 1) + 'I', *words[:payload[0] + 1]))
        bad = subprocess.run([str(binary), str(stream)], capture_output=True)
        assert bad.returncode == 3
        negatives.append('actual-vif-truncated-payload')
        invalid = list(words)
        invalid[commands[0][0]] = 0x09000000
        stream.write_bytes(struct.pack('<' + str(len(invalid)) + 'I', *invalid))
        assert subprocess.run([str(binary), str(stream)], capture_output=True).returncode == 2
        negatives.append('actual-vif-unknown-command')
    groups = collections.defaultdict(lambda: dict(calls=0, unpackPayloadBytes=0, unpacks=0,
                                                 relative2Vectors=0, relative2Histogram={}, primitives={}))
    for call in calls:
        key = '|'.join(call['exactResidentImages']) or f"unmapped:{call['address']}"
        groups[key]['calls'] += 1
        groups[key]['unpacks'] += len(call['uploads'])
        groups[key]['unpackPayloadBytes'] += sum(u['payloadBytes'] for u in call['uploads'])
        primitive = str(call['gsPrimitive'])
        groups[key]['primitives'][primitive] = groups[key]['primitives'].get(primitive, 0) + 1
        if 'inputAtRelative2' in call:
            vectors = call['inputAtRelative2']['vectors']
            groups[key]['relative2Vectors'] += vectors
            histogram = groups[key]['relative2Histogram']
            histogram[str(vectors)] = histogram.get(str(vectors), 0) + 1
    runs = {}
    for call in calls:
        key = str(call['mvpUploadWord'])
        run = runs.setdefault(key, dict(mvpUploadWord=call['mvpUploadWord'], calls=0,
                                       relative2Vectors=0, unpackPayloadBytes=0, programs={}))
        run['calls'] += 1
        run['unpackPayloadBytes'] += sum(u['payloadBytes'] for u in call['uploads'])
        name = '|'.join(call['exactResidentImages']) or 'unmapped'
        run['programs'][name] = run['programs'].get(name, 0) + 1
        if 'inputAtRelative2' in call:
            source = call['inputAtRelative2']
            run['relative2Vectors'] += source['vectors']
            if 'minimum' not in run:
                run['minimum'], run['maximum'] = list(source['minimum']), list(source['maximum'])
            else:
                run['minimum'] = [min(a, b) for a, b in zip(run['minimum'], source['minimum'])]
                run['maximum'] = [max(a, b) for a, b in zip(run['maximum'], source['maximum'])]
    result = dict(valid=True, elfSha256=report['elfSha256'], closureSha256=sha(closure),
                  parserSha256=sha(engine / 'inc/renderer/core/paths/path1/vif1_chain_check.hpp'),
                  symbolsSha256=sha(root / 'symbols.txt'), readerSha256=sha(Path(__file__)),
                  dma=dma, commandCounts=dict(counters), residentImages=images, calls=calls,
                  trailingUploads=pending_uploads, groups=dict(groups), negativeControls=negatives,
                  mvpDelimitedRuns=sorted(runs.values(), key=lambda r: r['unpackPayloadBytes'], reverse=True),
                  limitations=['frozen historical A18 workload, not current moving A35',
                               'exact program-image range, not semantic source-owner attribution',
                               'transfer bytes, not copied bytes or EE/VU/GS milliseconds',
                               'MVP-delimited runs are command structure, not proven bag/scene-owner identity'])
    assert not output.exists()
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(valid=True, calls=len(calls), groups=result['groups'], negativeControls=negatives), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    analyze(args.archive, args.output)
