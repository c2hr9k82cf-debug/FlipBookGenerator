# PARTS_FORMAT — 部件库格式 / Parts library format (v0.9.1)

> 交换格式 = **文件夹**。整个 `parts_library/` 目录可以拷走、分享、备份。
> The exchange format IS the folder. The whole `parts_library/` directory can
> be copied, shared and backed up.

## 目录布局 / Directory layout

```
parts_library/
  <slot>/                      # 六槽之一 / one of the six slots
    <item_name>/               # 物品名（目录名即默认名）/ item name (dir = default name)
      front.png                # 必备 / required
      side.png                 # 必备 / required
      back.png                 # back:"file" 时必备 / required iff back:"file"
      item.json                # 必备 / required
      poses/                   # v2 预留，v1 不读不写 / reserved for v2, v1 untouched
```

**槽位 / Slots**（固定六种 / exactly six）：`hair`、`face`、`top`、`pants`、`boots`、`body`。

### 颜色-槽位规则 / Colour-to-slot rules

模板内容分三带（`chibi_standard.json` 的 `part_map`）：头部 y2-12、躯干 y13-20、
腿 y21-31。像素归哪个槽由**角色色 + 区域**共同决定 —— 同一个颜色在不同区域归属
不同槽（v0.11.2 修复 C 修的正是这里）：

| 槽 / Slot | 画什么 / What belongs | 备注 / Notes |
|---|---|---|
| `hair` | 发型、发饰 | 头部带的 mint/red/gold 饰品（蝴蝶结、发夹）归这里 —— 换上衣不该带走它们 |
| `face` | 脸、五官、腮红 | 眼线行的裸露皮肤归这里 |
| `top` | 上衣、袖子 | **领口要画进领口带 y8-12**（头部区下沿）—— 只画到 y13 会露脖子 |
| `pants` | 裤 / 裙 | 腿区上部 |
| `boots` | 靴 | 腿区的靴筒与靴子 |
| `body` | **裸露皮肤** | 手臂、大腿等没被衣服盖住的部分，**不是躯干**（旧文案"身体"是误导） |

另：`hair_highlight` 与 `gold` 同 RGB，按区域消歧 —— 头部带上是发饰高光，其余
是金色装饰。

/ The template content spans three bands (`part_map`): head y2-12, torso
y13-20, legs y21-31. A pixel's slot is decided by its **role colour AND
region** — the same colour can belong to different slots (this is what
v0.11.2 fix C repairs):

| Slot | What belongs | Notes |
|---|---|---|
| `hair` | hair, hair accessories | mint/red/gold decorations on the head belt (bows, clips) belong here — they must not move when the top changes |
| `face` | face, eyes, blush | the bare-skin eye-level strip included |
| `top` | shirt, sleeves | **paint the collar into the collar band y8-12** (bottom of the head region) — stopping at y13 leaves the neck bare |
| `pants` | pants / skirt | upper legs |
| `boots` | boots | boot shafts in the leg band |
| `body` | **bare skin** | exposed arms/thighs, **not** the torso ("body" was a misnomer) |

Also: `hair_highlight` and `gold` share the same RGB and are disambiguated by
region — hair-shine on the head belt, gold trim elsewhere.

## 精灵图片 / Sprite PNGs

| 规则 / Rule | 值 / Value |
|---|---|
| 尺寸 / Size | 32×32（模板画布大小，不裁内容）/ template canvas size, content not cropped |
| 通道 / Channels | BGRA 8-bit（透明背景）/ BGRA 8-bit (transparent background) |
| 坐标系 / Coordinates | **模板绝对坐标**（内容 x9-x22, y2-y31）/ template absolute coords (content x9-x22, y2-y31) |
| 像素值 / Pixel values | 必须是角色调色板色（见下方 role 约束）/ must be palette role colours (see role constraint) |

`front.png` 与 `side.png` 是**必备**的（部件必须正面+侧面设计）。
`back.png` 按 `item.json` 的 `back` 字段决定：
- `"back": "file"` → `back.png` **必备**，直接用作背面；
- `"back": "auto"` → `back.png` **不得存在**（背面由合成时剪影推断自动生成）。
  歧义宁可拒绝：同时存在会被加载器拒绝。

/ `front.png` and `side.png` are **required** (every part is designed front+side).
`back.png` follows the `back` field in `item.json`:
- `"back": "file"` → `back.png` **required**, used verbatim;
- `"back": "auto"` → `back.png` **must not exist** (the back is inferred from the
  silhouette at compose time). Ambiguity is refused: the loader rejects items
  that have both.

## item.json

```json
{
  "name": "hair_orange_bob",
  "slot": "hair",
  "zOrder": 30,
  "anchors": { "front": [15, 12], "side": [15, 12], "back": [15, 12] },
  "roleColors": [
    { "role": "hair_mid",      "src": [231, 124, 64], "dst": [120, 80, 200] },
    { "role": "hair_highlight" }
  ],
  "back": "auto",
  "silhouette": "template"
}
```

| 字段 / Field | 必需 / Req | 含义 / Meaning |
|---|---|---|
| `name` | 否（缺省=目录名）/ no (default = dir name) | 物品名 / item name |
| `slot` | 否（但必须与父目录一致）/ no (must match folder) | 槽位；与目录不符即报错 / slot; mismatch with the folder is an error |
| `zOrder` | 否（缺省按槽查表：hair 40 / face 30 / top 20 / pants 10 / boots 10 / body 5）/ no (slot default: hair 40 / face 30 / top 20 / pants 10 / boots 10 / body 5) | 合成序，大在上；省略即用缺省表（v0.11.2 修复 A，不再缺省 0 垫底）/ compositing order, higher on top; omitted = the slot table (v0.11.2 fix A — no longer defaults to 0 at the bottom) |
| `anchors` | 否 / no | 逐视图锚点 `[x,y]`；缺省用模板默认 / per-view anchor `[x,y]`; template default otherwise |
| `roleColors` | 否 / no | 本物品的角色换色，见下 / role recolours for this item, see below |
| `back` | 否（缺省 `"auto"`）/ no (default `"auto"`) | `"auto"` = 剪影推断；`"file"` = 用 back.png / `"auto"` = silhouette inference; `"file"` = use back.png |
| `silhouette` | 否，v1 固定 `"template"` / no, v1 fixed `"template"` | 剪影形状来源（v2 才有别的值）/ silhouette source (v2 adds values) |

### roleColors — 角色换色 / role recolour

每条：`{ "role": <角色名>, "src": [R,G,B]?, "dst": [R,G,B] }`

- `role`：模板调色板角色名（`hair_mid`、`pants`、`skin`…）。
- `src`：可选。该物品精灵里这个角色**实际用的颜色**。缺省 = 模板 palette 里
  该角色的标准色（合成时解析）。
- `dst`：必填。最终要替换成的颜色。

替换是**精确色替换**（无模糊匹配）：只有恰等于 `src` 的像素会被换成 `dst`。
近似色不动 —— 这是像素风"无混合"铁律的一部分。

/ Each entry: `{ "role": <role name>, "src": [R,G,B]?, "dst": [R,G,B] }`
- `role`: a template palette role name.
- `src`: optional. The colour this item's sprite actually uses for that role.
  Omitted = the template palette's standard value (resolved at compose time).
- `dst`: required. The final colour to swap in.
Replacement is an **exact colour swap** (no fuzzy matching): only pixels equal
to `src` become `dst`. Near-miss colours are untouched — part of the pixel-art
"no blending" rule.

### role 约束（v1）/ role constraint (v1)

**精灵的每个不透明像素的颜色必须等于 `roleColors` 里某个 `src`（或模板 palette
里某角色的值）。** 换句话说：画只许用角色色。这保证全局换色 = 精确色替换、
像素级无混合。工具内填色编辑器（P3）在画笔层面强制此约束。

/ **Every opaque sprite pixel must equal some `src` in `roleColors` (or some
template palette role value).** The paint tool only offers role colours. This
guarantees global recolour = exact swap, zero per-pixel blending. The in-app
fill editor (P3) enforces this at the brush level.

## 位置 / Locations

| 库 / Library | 路径 / Path | 性质 / Nature |
|---|---|---|
| 用户库 / user | `~/Library/Application Support/FlipBookGenerator/parts_library/` | 可写 / writable |
| 起步库 / starter | `.app/Contents/Resources/parts_library/` | 只读 / read-only |

导入 = 把选中的库文件夹**合并**进用户库（同名提示覆盖）；
导出 = 把整个 `parts_library/` 复制到用户选的目录。v1 只做文件夹复制。

/ Import = **merge** a chosen library folder into the user library (same-name
asks to overwrite); export = copy the whole `parts_library/` to a chosen
directory. v1 is plain folder copy.

## 加载器错误行为 / Loader error behaviour

任一物品非法 ⇒ **整库加载失败**，报错指明出事文件夹与原因（半加载的库比没有
库更糟）。已知错误路径（`--parts-library-gate` 逐一覆盖）：

- 缺 `front.png` / `side.png` / `item.json`
- `item.json` 语法错误、类型错误
- `slot` 与目录不符
- 图片尺寸非 32×32 或非 BGRA
- `back:"file"` 缺 `back.png`；`back:"auto"` 却有 `back.png`
- `roleColors` 缺 `role`/`dst` 或数组形状错
- `silhouette` 不是 `"template"`

/ Any invalid item ⇒ **the whole load fails**, naming the folder and reason.
Known error paths (all covered by `--parts-library-gate`): missing
`front.png`/`side.png`/`item.json`; malformed `item.json`; slot/folder
mismatch; wrong size or channels; `back:"file"` without `back.png`;
`back:"auto"` with `back.png`; bad `roleColors`; unknown `silhouette`.
