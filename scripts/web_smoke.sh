#!/usr/bin/env bash
# web_smoke.sh — smoke-test a running flipbook_server against bundled views
# 对运行中的网站服务用内置角色三视图做冒烟测试
#
# Usage / 用法:
#   scripts/web_smoke.sh [base_url] [character_dir] [character_name]
# Defaults: http://127.0.0.1:8080 , assets/characters , 肯德基员工
set -euo pipefail
cd "$(dirname "$0")/.."

BASE="${1:-http://127.0.0.1:8080}"
CH="${2:-assets/characters}"
NAME="${3:-肯德基员工}"

echo "== health / 健康检查 =="
curl -sf "$BASE/health" | python3 -I -c "import json,sys; d=json.load(sys.stdin); assert d['ok'], d; print('health OK, version', d['version'])"

echo "== convert / 转换 =="
RESP=$(curl -sf -F "front=@$CH/${NAME}_front.png" \
            -F "side=@$CH/${NAME}_side.png" \
            -F "back=@$CH/${NAME}_back.png" \
            "$BASE/api/convert")
echo "$RESP" | python3 -I -c "
import json, sys
d = json.load(sys.stdin)
assert d['ok'], d
m = d['meta']
assert m['sheet'] == {'file': 'sheet.png', 'w': 192, 'h': 256, 'cols': 6, 'rows': 8}, m['sheet']
assert len(m['animations']) == 8, m['animations']
names = [a['name'] for a in m['animations']]
assert names == ['down_idle','down_walk','up_idle','up_walk',
                 'left_idle','left_walk','right_idle','right_walk'], names
idle = m['animations'][0]
assert idle['frames'] == 2 and idle['fps'] == 4, idle
walk = m['animations'][1]
assert walk['frames'] == 6 and walk['fps'] == 10, walk
for inp in m['inputs']:
    assert inp['align'] == {'scale': 1, 'dx': 0, 'dy': 0}, inp   # clean views stay identity
print('convert OK, job', d['job'], '- warnings:', len(d['warnings']))
print('sheet_url', d['sheet_url'])
open('/tmp/fb_smoke_sheet_url.txt', 'w').write(d['sheet_url'])
open('/tmp/fb_smoke_meta_url.txt', 'w').write(d['meta_url'])
"

echo "== sheet download / 大图下载 =="
curl -sf "$BASE$(cat /tmp/fb_smoke_sheet_url.txt)" -o /tmp/fb_smoke_sheet.png
python3 -I -c "
from PIL import Image
im = Image.open('/tmp/fb_smoke_sheet.png')
assert im.size == (192, 256), im.size
print('sheet OK:', im.size)
"

echo "== meta download / 元数据下载 =="
curl -sf "$BASE$(cat /tmp/fb_smoke_meta_url.txt)" -o /tmp/fb_smoke_meta.json
python3 -I -c "
import json
m = json.load(open('/tmp/fb_smoke_meta.json'))
assert m['app'] == 'FlipBookGenerator', m['app']
print('meta OK:', m['version'])
"

echo
echo "RESULT: PASS — web service smoke / 网站服务冒烟通过"
