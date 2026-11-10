#!/usr/bin/env python3
# extract_reference_animation.py — measure animation metrics from the reference sheet.
# 从参考图测量动画数据。
#
# Reads the 6x6 reference grid (192x192), extracts per-frame leg positions,
# body offsets, and color-swap patterns, writes assets/templates/anim_reference.json.
# 读取 6x6 参考图格（192x192），提取每帧的腿位、身体偏移、颜色交换模式，
# 写入 assets/templates/anim_reference.json。
#
# Usage / 用法:
#   python3 -I tools/extract_reference_animation.py [reference.png]

import json
import os
import struct
import sys
import zlib


def read_png(path):
    """Read a PNG file and return (width, height, bpp, rows)."""
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', f'not a PNG: {path}'
    pos = 8
    idat = b''
    w = h = 0
    ct = 6
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos+4])[0]
        tag = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+length]
        if tag == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', chunk[:10])
        elif tag == b'IDAT':
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    bpp = 4 if ct == 6 else 3
    rows = []
    stride = w * bpp + 1
    for y in range(h):
        row_data = raw[y*stride:(y+1)*stride]
        ft = row_data[0]
        row = bytearray(row_data[1:])
        if ft == 1:
            for i in range(bpp, len(row)):
                row[i] = (row[i] + row[i-bpp]) & 0xFF
        elif ft == 2:
            if y > 0:
                prev = rows[y-1]
                for i in range(len(row)):
                    row[i] = (row[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(len(row)):
                left = row[i-bpp] if i >= bpp else 0
                up = rows[y-1][i] if y > 0 else 0
                row[i] = (row[i] + (left + up) // 2) & 0xFF
        elif ft == 4:
            for i in range(len(row)):
                a = row[i-bpp] if i >= bpp else 0
                b = rows[y-1][i] if y > 0 else 0
                c = rows[y-1][i-bpp] if (y > 0 and i >= bpp) else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                row[i] = (row[i] + pr) & 0xFF
        rows.append(row)
    return w, h, bpp, rows


# Palette: RGB -> char / 调色板
PALETTE = {
    (46, 34, 47): 'outline',
    (247, 150, 23): 'hair_mid',
    (251, 107, 29): 'hair_dark',
    (249, 194, 43): 'hair_highlight',
    (253, 203, 176): 'skin',
    (252, 167, 144): 'blush',
    (230, 144, 78): 'tan',
    (199, 220, 208): 'mint',
    (255, 255, 255): 'white',
    (232, 59, 59): 'red',
    (205, 104, 61): 'pants',
    (155, 171, 178): 'grey',
    (127, 112, 138): 'grey_dark',
    (22, 90, 76): 'teal',
}

# Reference layout / 参考图布局
CELL = 32
GRID_W = 6
GRID_H = 6
# Row mapping: (view, anim_type) / 行映射
ROW_MAP = {
    (0, 'idle'): 'idle_front',
    (1, 'walk'): 'walk_front',
    (2, 'idle'): 'idle_side',
    (3, 'walk'): 'walk_side',
    (4, 'idle'): 'idle_back',
    (5, 'walk'): 'walk_up',   # back walk = walk_up (away from camera)
}
IDLE_FRAMES = 2
WALK_FRAMES = 6


def get_pixel(rows, bpp, px, py):
    if bpp == 4:
        return (rows[py][px*4], rows[py][px*4+1], rows[py][px*4+2], rows[py][px*4+3])
    return (rows[py][px*3], rows[py][px*3+1], rows[py][px*3+2], 255)


def measure_frame(rows, bpp, cx, cy):
    """Measure one 32x32 cell. Returns dict with body_offset, leg data."""
    # Body top y (first opaque pixel row)
    top_y = 32
    for ly in range(32):
        for lx in range(32):
            r, g, b, a = get_pixel(rows, bpp, cx * CELL + lx, cy * CELL + ly)
            if a > 0:
                top_y = min(top_y, ly)
                break
        if top_y < 32:
            break

    # Leg region analysis (y21-y31 in cell coords)
    pants_pixels = []
    tan_pixels = []
    for ly in range(21, 32):
        for lx in range(32):
            r, g, b, a = get_pixel(rows, bpp, cx * CELL + lx, cy * CELL + ly)
            if a == 0:
                continue
            color = PALETTE.get((r, g, b))
            if color == 'pants':
                pants_pixels.append((lx, ly))
            elif color == 'tan':
                tan_pixels.append((lx, ly))

    def x_range(pixels):
        if not pixels:
            return [0, 0]
        xs = [p[0] for p in pixels]
        return [min(xs), max(xs)]

    def y_range(pixels):
        if not pixels:
            return [0, 0]
        ys = [p[1] for p in pixels]
        return [min(ys), max(ys)]

    return {
        'body_top_y': top_y,
        'pants_count': len(pants_pixels),
        'tan_count': len(tan_pixels),
        'pants_x_range': x_range(pants_pixels),
        'pants_y_range': y_range(pants_pixels),
        'tan_x_range': x_range(tan_pixels),
        'tan_y_range': y_range(tan_pixels),
    }


def detect_tone_swap(frames):
    """Detect left/right tone swap pattern across frames.
    Returns list of frame indices where the swap occurs (vs frame 0)."""
    if not frames:
        return []
    ref = frames[0]
    ref_left = ref['pants_x_range'][0] < ref['tan_x_range'][0]
    swap_groups = [[], []]  # group 0 = same as frame 0, group 1 = swapped
    for i, f in enumerate(frames):
        f_left = f['pants_x_range'][0] < f['tan_x_range'][0]
        if f_left == ref_left:
            swap_groups[0].append(i)
        else:
            swap_groups[1].append(i)
    return swap_groups


def main():
    ref_path = sys.argv[1] if len(sys.argv) > 1 else '09d46502714b463788ad0eb9a23ae0f9.png'
    if not os.path.exists(ref_path):
        # Try project root
        ref_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                os.path.basename(ref_path))
    if not os.path.exists(ref_path):
        print(f'ERROR: reference image not found: {ref_path}')
        return 1

    w, h, bpp, rows = read_png(ref_path)
    print(f'Reference: {w}x{h}, {bpp}bpp')

    result = {
        'source': os.path.basename(ref_path),
        'layout': {
            'grid': f'{GRID_W}x{GRID_H}',
            'cell_size': CELL,
            'row_mapping': {f'row_{k[0]}_{k[1]}': v for k, v in ROW_MAP.items()},
        },
        'animations': {},
    }

    for (row, anim_type), anim_name in ROW_MAP.items():
        n_frames = IDLE_FRAMES if anim_type == 'idle' else WALK_FRAMES
        frames = []
        for col in range(n_frames):
            m = measure_frame(rows, bpp, col, row)
            m['frame'] = col
            frames.append(m)

        # Body top y offsets (relative to frame 0)
        base_y = frames[0]['body_top_y']
        offsets = [f['body_top_y'] - base_y for f in frames]

        # Tone swap detection (walk only)
        swap = detect_tone_swap(frames) if anim_type == 'walk' else None

        # Passing frames (legs together = min x-range span)
        if anim_type == 'walk':
            spans = []
            for f in frames:
                all_x = f['pants_x_range'] + f['tan_x_range']
                span = max(all_x) - min(all_x)
                spans.append(span)
            min_span = min(spans)
            passing = [i for i, s in enumerate(spans) if s <= min_span + 1]
        else:
            passing = None

        anim_data = {
            'frames': n_frames,
            'fps': 8 if anim_type == 'idle' else 10,
            'body_offset_y': offsets,
            'frame_data': frames,
        }
        if swap is not None:
            anim_data['tone_swap_groups'] = swap
        if passing is not None:
            anim_data['passing_frames'] = passing

        result['animations'][anim_name] = anim_data

    # Write output
    out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           'assets', 'templates')
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, 'anim_reference.json')
    with open(out_path, 'w') as f:
        json.dump(result, f, indent=2)
    print(f'Wrote {out_path}')

    # Summary
    for name, data in result['animations'].items():
        swap = data.get('tone_swap_groups', '-')
        passing = data.get('passing_frames', '-')
        print(f'  {name}: {data["frames"]} frames, offsets={data["body_offset_y"]}, '
              f'swap={swap}, passing={passing}')

    return 0


if __name__ == '__main__':
    sys.exit(main())
