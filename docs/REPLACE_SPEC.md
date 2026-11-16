# FlipBookGenerator — Per-frame Region Replacement / 逐帧区域替换规范

> The v0.13.1 renderer that brings **user shapes** into the animation frames:
> one normal-pose user view in, six flipbooks out whose per-frame pixels are
> the user's, not just a recolour of the template. Rules live in
> `src/core/replace.cpp` (`renderReplaced`); this document is the contract.
>
> v0.13.1 的渲染器，把**用户形状**带进动画帧：一张规范姿态用户视图进去，六本
> 翻页书出来的逐帧像素是用户的，而不只是模板换色。规则住在
> `src/core/replace.cpp`（`renderReplaced`）；本文是契约。
>
> English on top, Chinese below — per the project's bilingual convention.
> 英文在上，中文在下 —— 沿用项目双语约定。

---

## 0. Why / 为什么

Tier B chip playback gives pixel-exact *template* motion, but until v0.13.0 it
only swapped colours — a custom hat or jacket shape never reached the frames.
Region replacement samples the user's normal-pose view into every frame at the
right slot layer, so the customisation is animated.

Tier B 芯片回放给出逐像素精确的*模板*动作，但 v0.13.0 之前只换色 —— 自定义的
帽子、夹克形状从不进帧。区域替换把用户的规范姿态视图按槽层序采进每一帧，
自定义因此动起来。

Coordinate conventions / 坐标约定：

- **FRAME row y** — a row as drawn in one chip frame (bobs with `body_top_y`).
  帧行 y —— 芯片帧里的画法行（随 `body_top_y` 起伏）。
- **NORMAL row yNorm = y - (body_top_y - 2)** — the reference-pose row; row
  bands and slot classification judge in normal rows.
  规范行 yNorm = y - (body_top_y - 2) —— 参考姿态行；行带与槽分类按规范行判。
- Region boxes in `chibi_regions.json` are **frame-coordinate** unions
  (see `docs/PARTS_FORMAT.md`).
  `chibi_regions.json` 的框是**帧坐标**并集（见 `docs/PARTS_FORMAT.md`）。

---

## 1. The M21 render model / M21 渲染模型

For each flipbook frame f (chip `C`, `body_top_y = bty`, `off = bty - 2`), for
each cell `(x, y)` with `sy = y - off` (the normal-pose sample position):

对芯片集每帧 f（芯片 `C`，`body_top_y = bty`，`off = bty - 2`），对每格
`(x, y)`，取 `sy = y - off`（规范姿态采样位）：

| Candidate / 候选 | Enters when / 进入条件 | z / 层 | Colour / 颜色 |
|---|---|---|---|
| **FRAME** (the chip occupant / 芯片占用) | `C(x,y)` non-transparent, **not suppressed** (see recession / 不被**缩水**，见下) | `zOf(classifyFrame(C))`, unowned = 0 | `PaletteMap(C)` |
| **MATERIAL** (the user's pixel / 用户像素) | `U(x,sy)` non-transparent **and** (`N(x,sy)` transparent **or** `C(x,y)` non-transparent) **and** `userSlots(x,sy) >= 0` | `zOf(userSlots)` | see M15 below / 见下 M15 |

- `U` = the user view, `N` = the reference view (`renderTemplateView`), both in
  normal-pose coordinates. `U` is sampled with the frame's bob offset — the
  user's shape bobs with the body.
  `U` = 用户视图，`N` = 参考视图（`renderTemplateView`），都在规范姿态坐标。
  `U` 按帧起伏采样 —— 用户形状随身体起伏。
- **Gate / 闸门**: the material only enters where the reference is transparent
  (a user shape extension) or the chip paints the cell. Without it a swinging
  leg's vacated cell would keep showing the static material (double legs).
  **闸门**：素材仅当参考处透明（用户形状扩展）或芯片画了这格才进入。没有它，
  摆腿腾空的格会继续显示静态素材（双腿重影）。
- **Winner / 胜者**: higher `defaultLayerZ` wins; **ties go to the material**.
  z 大者胜；**平票素材胜**。
- **Recession / 缩水**: where the user deleted a design cell (`N(x,sy)`
  non-transparent, `U(x,sy)` transparent) and the frame cell belongs to the
  same slot as that design cell (`classifyFrame(C)(x,y) == refSlots(x,sy)`),
  the FRAME candidate is suppressed — the user's smaller shape really shrinks.
  A swinging limb of **another** slot passing through the deleted cell is not
  suppressed (slot mismatch) and survives untouched.
  用户删掉了设计格（`N(x,sy)` 不透明、`U(x,sy)` 透明）且帧内格与该设计格**同槽**
  （`classifyFrame(C)(x,y) == refSlots(x,sy)`）时，FRAME 候选让位 —— 用户的小
  形状真的变小。**别的槽**的摆动肢体扫过删除格不受影响（异槽不匹配）、原样保留。
- z table (`defaultLayerZ`, `src/core/parts_library.cpp`):
  pants/boots 10 < top 20 < face 30 < body (bare skin) 45 < hair 50.
  z 表：裤/靴 10 < 上衣 20 < 脸 30 < 裸肤 45 < 头发 50。
- No candidate → the cell is transparent. 无候选 → 透明。

### M15 colour rule / M15 色规

A material pixel that **follows** the reference colour layout
(`N` non-transparent and `U == PaletteMap(N)`) outputs `PaletteMap(C)` — the
chip's per-frame tone swaps and redraws survive. A **deviating** material (a
user pattern / shape extension) outputs its own colour `U` — **except** a
colour that is no palette-map destination: that is an extraction minority the
tolerant pass repaired to the majority (the GUI warns "majority chosen"), and
it outputs `PaletteMap(N)`, keeping the map a function.

**沿**参考色布局的素材（`N` 不透明且 `U == PaletteMap(N)`）输出
`PaletteMap(C)` —— 芯片逐帧色调交换与重绘得以保留。**偏离**的素材（用户图案 /
形状扩展）输出自己的颜色 `U` —— **唯一例外**是非映射目标色：那是提取宽容处理时
按多数派修复掉的少数派（GUI 警告"已按主色处理"），它输出 `PaletteMap(N)`，
保持映射的函数性。

### Slot assignment / 槽归属

`splitViewBySlot` (→ `assignSlots`, `src/core/replace.cpp`) resolves each
opaque pixel's slot in three steps — this is what lets recoloured and
repaired views split correctly:

`splitViewBySlot`（→ `assignSlots`，`src/core/replace.cpp`）三步解析每个不透明
像素的槽 —— 换过色、被修复过的视图因此能正确拆分：

1. **Map preimage / 映射原像**: scan the PaletteMap for the pixel's dst colour;
   classify each src. Unique slot → its slot; two distinct slots (a merged
   recolour, e.g. white and skin both mapped to one pale) → ambiguous; an
   outline-only preimage (the user's outline colour) → `SLOT_OUTLINE`, joining
   the neighbour vote instead of being dropped.
   扫 PaletteMap 找该 dst 色，分类各 src。唯一槽 → 其槽；两个不同槽（合并换色，
   如白与肤同映射一个浅色）→ 歧义；纯描边原像（用户的描边色）→ `SLOT_OUTLINE`，
   进邻域投票而不是被丢弃。
2. **Reference design / 参考设计**: on ambiguity or no preimage, the reference
   design at the same cell decides (the shape follows the design's anatomy).
   歧义或无原像时，同一格的参考设计定槽（形状随设计解剖结构走）。
3. **Raw role colour / 原始角色色**: last resort — read the pixel as an
   un-recoloured role colour (the chips path, empty map).
   最后手段 —— 把像素当未换色的角色色读（芯片路径，空 map）。

Outline pixels then join the slot their already-assigned 8-neighbours vote for
(scan order, ties to the first seen — `Counter.most_common` parity with the
Python tools). A pixel with no owner anywhere stays `SLOT_NONE` and its
material never enters the frames.

描边像素随后按"已归属 8 邻"投票进槽（扫描序，平票取先见 —— 与 Python 工具的
`Counter.most_common` 一致）。处处无归属的像素保持 `SLOT_NONE`，其素材永不进帧。

### Per-frame arm/clothes rule / 逐帧衣臂规则

Answering "该盖住衣服就盖住": the chip's occupant at a cell decides the frame's
slot; bare skin (z45) beats a painted sleeve (z20) wherever the chip draws an
arm there, while a sleeve painted into a shirt cell wins the z20 tie and the
user's shape enters. A vacated cell (chip transparent, reference non-empty)
stays empty — the sleeve never smears into swing extremes.

回答"该盖住衣服就盖住"：格内芯片占用决定该帧的槽；芯片画手臂处裸肤（z45）胜过
画上的袖（z20），而袖画进上衣格平票胜出、用户形状进帧。腾空格（芯片透明、参考
非空）保持空 —— 袖绝不外溢到摆动极值。

---

## 2. Invariants / 不变量

1. **replace_fidelity / 替换保真**: user == reference + identity map ⇒ every
   replaced frame is byte-exact to the chip frame (24/24). Replacement without
   customisation is lossless. Both candidates resolve to `C` — proven in the
   design, gated by `--replace-fidelity-gate`.
   用户 == 参考 + 恒等映射 ⇒ 替换帧逐字节等于芯片帧（24/24）。无自定义的替换
   无损。两候选同归 `C` —— 设计已证，`--replace-fidelity-gate` 把守。
2. **Coverage / 覆盖**: every opaque output pixel is a palette-map destination
   or a user-view colour. A FRAME leak (a chip colour the extraction never
   covered) is caught; a MATERIAL extension passes.
   非透明输出像素 ∈ 映射目标色 ∪ 用户视图颜色。FRAME 泄漏（提取没覆盖的芯片色）
   被抓住；MATERIAL 扩展放行。
3. **No holes beyond the user's deletion / 用户删除处之外不出洞**: a
   chip-opaque cell is transparent in the output **only** where the recession
   rule fired (the user deleted that design cell and the frame cell is the
   same slot). Every other chip-opaque cell stays opaque.
   芯片不透明处**仅当**缩水规则触发（用户删了该设计格且帧内格同槽）才透明。
   其余芯片不透明处一律保持不透明。
4. **Pixel-art hard rules / 像素风硬约束** still hold: nearest-neighbour only,
   no anti-aliasing, no blending at overlaps (z-order wins), integer-pixel
   translations. 照旧：只用最近邻、无抗锯齿、重叠无混合（z 序取胜）、整数像素平移。

---

## 3. Data flow / 数据流

```
user view U (normal pose) ──extractPaletteMap(U, idle f0)──▶ PaletteMap
reference N = renderTemplateView(spec, view)
userSlots = splitViewBySlot(U, PaletteMap)         // per-view, once / 每视图一次
bodyTop   = loadAnimBodyTop(anim_reference.json)   // per-frame bty / 逐帧 bty
                     │
                     ▼
renderReplaced(chips, PaletteMap, U, userSlots, N, bodyTop, SlotTables)
                     │
                     ▼
six flipbooks (idle 2f + walk 6f per view) → sheet rows (mirrored left)
```

`buildFourDirSheet` (`src/core/sheet.cpp`) runs this for all three views; the
GUI and the CLI drive the same code (golden invariant: byte-identical export).

`buildFourDirSheet`（`src/core/sheet.cpp`）对三视图跑这条链；GUI 与 CLI 走同一
段代码（金不变量：导出逐字节一致）。

---

## 4. Gates / 闸门

| Gate / 闸门 | Proves / 证明 |
|---|---|
| `--replace-classify-gate` | the classifier reproduces the committed tool artefacts (ref_* sprites, chibi_regions masks) / 分类器复现工具提交产物 |
| `--replace-fidelity-gate` | lossless when nothing is customised (24/24 byte-exact) / 无自定义时无损（24/24 逐字节） |
| `--replace-custom-gate` | custom shapes enter (hat stamps every frame), outline tips vote in, no holes beyond the user's deletion, arm/clothes rule per frame, recession, merged-colour/stray repair / 自定义进帧（帽子盖满）、描边尖投票进槽、删除处之外不出洞、衣臂逐帧成立、缩水、合并色/游离色修复 |
| `fourdir_sheet` (ctest) | the whole sheet pipeline incl. the coverage invariant / 全管线含覆盖不变量 |
| golden invariant (manual) | `FLIPBOOK_AUTOTEST` GUI export == CLI `--fourdir` byte-identical / GUI 导出 == CLI 逐字节 |

---

## 5. Limits / 边界

- Material pixels should use **palette role colours** — an arbitrary RGB
  classifies to no slot on its own and falls back to the reference design at
  the same cell (then raw role read); only a pixel no route can own is dropped
  (`userSlots < 0`). A deviating non-destination colour is repaired to
  `PaletteMap(N)` (M15 exception).
  素材像素应当用**调色板角色色** —— 任意 RGB 自己分不出槽时落到同一格的参考设计
  （再落原始角色色读）；只有各路都无主的像素才被丢弃（`userSlots < 0`）。偏离的
  非目标色被修复为 `PaletteMap(N)`（M15 例外）。
- Strict extraction (`--fourdir`) requires the user silhouette to match the
  template; shape *extensions* are tolerated only in GUI/tolerant mode (the
  extension pixels are then routed in as MATERIAL).
  严格提取要求用户剪影与模板一致；形状*扩展*仅在 GUI/宽容模式下放行（扩展像素
  随后作为 MATERIAL 进帧）。
- This is still Tier B: motion is the template's, shapes are the user's. It
  cannot invent motion the chips do not contain.
  这仍是 Tier B：动作是模板的，形状是用户的。它无法发明芯片里没有的动作。
