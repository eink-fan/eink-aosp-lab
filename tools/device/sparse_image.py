"""Bounded standard Android sparse images; zero data is FILL, never skipped."""
import struct

BLOCK = 4096
RAW, FILL, SKIP = 0xCAC1, 0xCAC2, 0xCAC3

def encode_region(data, start_block, total_blocks):
    if not data or len(data) % BLOCK or start_block < 0:
        raise ValueError('invalid block-aligned region')
    count = len(data) // BLOCK
    if start_block + count > total_blocks:
        raise ValueError('region exceeds image')
    chunks = []
    if start_block:
        chunks.append((SKIP, start_block, b''))
    zero = bytes(BLOCK)
    begin = 0
    while begin < count:
        is_zero = data[begin * BLOCK:(begin + 1) * BLOCK] == zero
        end = begin + 1
        while end < count and (data[end * BLOCK:(end + 1) * BLOCK] == zero) == is_zero:
            end += 1
        chunks.append((FILL if is_zero else RAW, end - begin,
                       bytes(4) if is_zero else data[begin * BLOCK:end * BLOCK]))
        begin = end
    remaining = total_blocks - start_block - count
    if remaining:
        chunks.append((SKIP, remaining, b''))
    output = bytearray(struct.pack('<I4H4I', 0xED26FF3A, 1, 0, 28, 12,
                                   BLOCK, total_blocks, len(chunks), 0))
    for kind, blocks, body in chunks:
        output.extend(struct.pack('<2H2I', kind, 0, blocks, 12 + len(body)))
        output.extend(body)
    return bytes(output)
