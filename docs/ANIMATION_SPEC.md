# FlipBookGenerator — Character Animation Spec / 角色动画设计规范

> Covers the six-flipbook study of `assets/reference_sheet.png` (the 6×6 grid of
> 32×32 cells). Everything here is measured from that sheet, pixel by pixel, with
> the body bob (`body_offset_y`) compensated.
>
> 本文覆盖对 `assets/reference_sheet.png`（6×6 格、每格 32×32）的六本翻页书研究。
> 所有数据均逐像素测量得出，并已补偿身体起伏（`body_offset_y`）。
>
> English on top, Chinese below — per the project's bilingual convention.
> 英文在上，中文在下 —— 沿用项目双语约定。

---

## 0. Scope & the two animation tiers / 范围与两级动画

A character is turned into **six flipbooks**: three views (front / side / back)
× two motions (idle / walk). This spec says what each flipbook does and how it is
composed from layers.

角色被做成**六本翻页书**：三视图（正面 / 侧面 / 背面）× 两种动作（待机 / 行走）。
本规范说明每本翻页书做什么、由哪些图层合成。

CLAUDE.md defines two tiers. This spec targets **Tier A (procedural)**: standard
sprites + tone swap + arm occlusion + idle stretch + bob. The hand-redrawn
per-frame regions (hips, boots) are approximated by rules now, with a
`VariantChip` slot reserved so hand-drawn chips can replace them later (Tier B).

CLAUDE.md 定义了两级。本规范针对 **Tier A（程序化）**：标准精灵 + 换色 + 手臂遮挡 +
待机伸展 + 起伏。逐帧手绘的区域（髋部、靴子）本轮用规则近似，并预留 `VariantChip`
槽位，日后可用手绘芯片替换（Tier B）。

---

## 1. The six flipbooks / 六本翻页书总览

| Flipbook / 翻页书 | Frames | fps | Ref row | Leg motion / 腿部动作 |
|---|---|---|---|---|
| `idle_front` | 2 | 4 | 0 | planted / 钉住不动 |
| `walk_front` | 6 | 10 | 1 | straight + tone swap / 直立+换色 |
| `idle_side` | 2 | 4 | 2 | planted / 钉住不动 |
| `walk_side` | 6 | 10 | 3 | **genuinely swinging / 真摆动** |
| `idle_back` | 2 | 4 | 4 | planted / 钉住不动 |
| `walk_up` | 6 | 10 | 5 | straight + tone swap / 直立+换色 |

**fps**: walks run at 10 fps (6 frames = 600 ms per stride). Idle runs at **4 fps**
in the engine (a full breath = one second); the sheet labels idle 8 fps. This is a
deliberate deviation, kept visible.

**fps**：行走 10fps（6 帧 = 一步 600ms）。待机引擎里跑 **4fps**（一次呼吸一秒）；
参考图标 8fps。这是有意的取舍，保持可见。

Measured, bob-compensated (`logical row = physical y − body_offset_y`):

实测，补偿起伏后（`逻辑行 = 物理 y − body_offset_y`）：

| Flipbook | Rows unchanged across frames / 逐帧不变的行 |
|---|---|
| `walk_front` | y0–14 |
| `walk_side` | y0–15 |
| `walk_up` | y0–17 |
| `idle_front` | y0–1, y17, **y23–31** (physical, legs) |
| `idle_side` | y0–1, y17, **y24–31** (physical, legs) |
| `idle_back` | y0–1, y18–19, **y24–31** (physical, legs) |

**Head band y2–12 differs by 0 pixels across every walk frame in all three views**
— it is a single standard sprite. Same for the upper torso.

**头部 y2–12 在三个视图的全部行走帧里 0 像素差异** —— 是一张标准精灵。上躯干同理。

---

## 2. Layer model & z-order / 分层模型与图层顺序

Layers stack bottom → top. Higher layers occlude lower ones; there is no blending
(z-order wins, pixel-art rule 4).

图层自下而上堆叠。上层遮挡下层，绝不混合（z-order 胜出，像素风规则 4）。

```
   ┌──────────────────────────────────────────────┐
 5 │ HAIR      头发   (top — occludes face & torso) │  ← 3 designs, back = mirror of front
 4 │ ARMS      手臂   (black outline occludes shirt)│  ← standard sprite, copied verbatim
 3 │ CLOTHES   衣服   (coloured on revealed cells)  │  ← painted on the *revealed* cells
 2 │ PANTS     裤子   (two-tone swap)               │  ← 2 tones, swap per frame
 1 │ SKELETON  骨架   (skin + outline, never moves) │  ← copied across all 3 views
   └──────────────────────────────────────────────┘
```

Rules / 规则：

- **Skeleton (skin `N` + outline `O`) is copied across all three views unchanged.**
  骨架（皮肤 `N` + 描边 `O`）三面照搬，不变。
- **Arms are a standard sprite pasted verbatim** — position never changes per frame.
  手臂是标准精灵，逐帧原样照抄，位置不变。Its black outline (`O`) covers the shirt's
  side edge on alternating frames. 它的黑描边在交替的帧上挡住衣服侧边。
- **Clothes are coloured on the cells that survive occlusion** (see §6).
  衣服在「被遮挡后露出的格子」上着色（见 §6）。
- **Hair is the topmost layer**, designed per view; the back hair is the horizontal
  mirror of the front hair and covers the face + torso outline (see §7).
  头发是最上层，三面分开设计；背面头发 = 正面头发水平翻转，挡住脸与躯干轮廓（见 §7）。

---

## 3. Torso shape per view & front↔back link / 三面躯干形状与正背联系

**Row bands are identical for all three views** (so a character never jumps when
rotating): head y2–12, torso y13–20, legs y21–31. **Column splits differ per view**.

**行划分三面一致**（转面时不跳）：头 y2–12、躯干 y13–20、腿 y21–31。**列划分按视图**。

| View | Content box / 内容框 | Width | Arm columns / 臂列 |
|---|---|---|---|
| front | x9–22, y2–31 | 14 | x9–11 / x20–22 |
| side | x10–21, y2–31 | 12 | front arm x10–12, rear arm x20–21 |
| back | x9–22, y2–31 | 14 | x9–11 / x20–22 |

**Front ↔ back consistency is logical, not geometric**: the back is narrower where
the body is narrower from behind, but the *row bands, part boundaries and anchors*
line up exactly. Other body parts (legs, arms, torso) are simply **copied** across
views — no per-view reshaping.

**正背的「一致」是逻辑上的一致，不是几何上的一致**：背面在身体本来更窄处更窄，但
**行划分、部件边界、锚点**完全对齐。其他身体部位（腿、臂、躯干）三面**照搬**，
不做逐视图重塑。

---

## 4. Per-part motion principle / 各部件运动原理

Compensating the bob, here is exactly what each region does.

补偿起伏后，各区域的行为如下。

| Region / 区域 | Idle / 待机 | Walk front & up / 正背行走 | Walk side / 侧走 |
|---|---|---|---|
| **Head** y2–12 | rides up 1 row on f0 / f0 抬 1 行 | **standard sprite, 0 diff** | standard sprite, 0 diff |
| **Torso** y13–20 | extends 1 row (colour-sampled) / 延长 1 行（取色） | **standard sprite** (walk_up y0–17 identical) | standard sprite |
| **Arms** | follow torso / 随躯干 | verbatim; outline occludes shirt / 照抄，描边挡衣 | verbatim; **far arm hidden 4/6 frames** / 远臂 6 帧隐 4 帧 |
| **Legs** y23–25 | **planted, pixel-identical / 钉住，逐像素不动** | **straight** (walk_up = 2 variants = 2 tone groups) | **6 variants, swinging / 真摆** |
| **Pants** | two-tone / 两主色 | two-tone swap / 交替换色 | two-tone swap |
| **Hip** y18–22 | redrawn as bridge / 重画当桥 | 6 variants (hand-redrawn) | 6 variants |
| **Boots** y26–31 | planted / 钉住 | 6 variants (hand-redrawn) | 6 variants |

**Key proof — walk_up legs are straight.** Across 6 frames the leg band y23–25 has
exactly **2 distinct variants**, and those 2 are precisely `tone_swap_groups`
`[0,1,4,5]` / `[2,3]`. So 100 % of the leg difference is the two-tone colour swap;
**zero geometric change**. Same for walk_front's straight legs. Only walk_side
truly swings (6 variants).

**关键证明 —— 背走的腿是直的。** 6 帧里腿区 y23–25 恰好 **2 种变体**，且正好是
`tone_swap_groups` 的 `[0,1,4,5]` / `[2,3]`。即腿的差异 **100% 是两主色换色**，
**几何零变化**。正走同理。只有侧走真正摆动（6 变体）。

---

## 5. Pants two-tone swap / 裤子两主色交替换色

**Spec rule / 规范规则**: the pants use exactly **two main tones of similar hue but
different lightness**. Detect the two dominant leg colours and swap them per frame —
no per-pixel art needed.

**规范规则**：裤子恰好用**两种色相相近、明度不同的主色**。统计两条腿的两种主色，
逐帧互换 —— 无需逐像素手绘。

Current tones / 当前两主色：

- `pants` = (205,104,61) — dark / 深
- `tan` = (230,144,78) — light / 浅

Swap schedule = `tone_swap_groups` in `assets/templates/anim_reference.json`
(frames in one group share a tone arrangement):

换色排程 = `assets/templates/anim_reference.json` 的 `tone_swap_groups`
（同组帧共用一种配色）：

| Animation | tone_swap_groups | Meaning |
|---|---|---|
| `walk_front` | `[[0,1,5],[2,3,4]]` | left/right tones trade at f2 & f5 |
| `walk_up` | `[[0,1,4,5],[2,3]]` | trade at f2 & f4 |
| `walk_side` | `[[0,1,2,4,5],[3]]` | trade only at f3 |

In code this is a per-frame **palette swap** (`Pose::swaps`, §11): the two tone
values trade places. The leg *shapes* are untouched.

代码里这是逐帧的**调色板换色**（`Pose::swaps`，§11）：两个色值互换位置。腿的*形状*
完全不动。

---

## 6. Arm occlusion & shirt revealed cells / 手臂遮挡与衣服露出格子

**Mechanism (the user's own example)**: when walking forward, the arm's **black
outline** shifts 1 px and alternately covers a shirt pixel on the torso's side edge.
The arm sprite itself never moves — only which cells it wins over the shirt.

**机制（用户举例）**：向前走时手臂的**黑描边**横移 1 像素，交替挡住躯干侧边的一个
衣服像素。手臂精灵本身不动 —— 变的只是它盖过衣服的那些格子。

`walk_front` row y15 (O = outline, N/m = skin, W/E = shirt)：

```
f0  .........ONmOWWWWWWEmO..........   ← left arm ONmO covers shirt edge
f1  ..........OmEWWWWWWEmO..........   ← shirt EmO revealed
f2  ..........OmEWWWWWWOmO..........   ← right arm OmO covers shirt edge
f3  ..........OmEWWWWWWOmO..........
f4  ..........OmEWWWWWWEmO..........
f5  ..........OmEWWWWWWEmO..........
```

**Spec rule / 规范规则**: the arm is a **standard sprite copied verbatim**; the task
is only to know *where the shirt is covered and when it shows*. **Collect every
revealed shirt cell across all frames into one set, and paint the clothes on that
set.** Cells the arm wins on some frames simply let the arm through on those frames.

**规范规则**：手臂是**逐帧照抄的标准精灵**；只需算清*衣服哪里被挡、何时露出*。
**把所有帧里露出的衣服格子统计成一个集合，在该集合上画衣服。** 手臂在某些帧胜出的
格子，那些帧就让给手臂。

Measured for `walk_front` (shirt = tones `E`,`W`) / 实测：

- shirt-visible cells per frame: **43–46**
- **union (cells that ever show) = 66**
- intersection (always show) = 24
- **arm cells that are shirt on some other frame = 32** ← the alternating occlusion

**Far-arm occlusion / 远端手臂消失**: in `walk_side` the rear arm is drawn on only
**2 of 6 frames** (f2/f3, 19 px); on f0/f1/f4/f5 it is nearly gone (9 px). So the
occluded arm **disappears for several frames** — modelled as `Pose::hidden` (§11).

侧走时远端手臂只在 **6 帧里的 2 帧**（f2/f3）画出，其余帧几乎消失。即被挡手臂
**消失几帧** —— 用 `Pose::hidden`（§11）建模。

---

## 7. Hair: per-view design & back = mirror of front / 头发：三面设计与正背翻转

Hair is the **topmost layer** and is designed **separately per view** (front / side
/back). It can occlude both the face and the torso outline.

头发是**最上层**，**三面分开设计**（正面 / 侧面 / 背面）。它能遮挡脸和躯干轮廓。

**Back hair = the horizontal mirror of the front hair.** It covers where the face
and torso would be seen from behind.

**背面头发 = 正面头发水平翻转。** 它盖住从背面本该看到的脸和躯干。

Measured proof / 实测佐证 — skin (`N`) only appears from **y14** on the back (face
y6–13 hidden by hair), but from **y6** on the front:

背面皮肤（`N`）只从 **y14** 起（脸 y6–13 被头发盖住），正面从 **y6** 起：

| View | face/skin bbox / 脸与皮肤框 | Meaning |
|---|---|---|
| front | x10–21, y6–22 | full face visible / 脸完整可见 |
| side | x11–21, y7–22 | profile / 侧脸 |
| back | x10–21, **y14**–22 | face covered by hair / 脸被头发盖 |

The hair crown (y2–4) is already identical across all three views (symmetric top);
from y5 the front shows the face and the back shows the rear hair.

头顶（y2–4）三面已一致（对称顶部）；y5 起正面显脸、背面显后脑头发。

**Design rule for new hair / 新发型设计规则**: author the front hair, then derive
the back hair by horizontal mirror; the side hair is designed on its own. This keeps
the turn-around logically consistent.

新发型：先画正面头发，背面由水平翻转导出；侧面单独设计。这样转身在逻辑上连贯。

---

## 8. Face per-frame size & position / 脸部逐帧大小与位置

The face's visible box shifts slightly each frame because of the bob and occlusion.

脸的可见框因起伏与遮挡逐帧微调。

| Frame | `walk_front` face bbox |
|---|---|
| f0 | x10–20, y5–22 |
| f1 | x10–21, y6–23 |
| f2 | x10–21, y5–22 |
| f3 | x10–21, y6–22 |
| f4 | x10–21, y7–22 |
| f5 | x10–21, y6–22 |

The face is **not** redrawn — it is the standard head sprite riding the bob, its
bbox changing only because the whole head translates. On the back the face is
covered by hair (§7).

脸**不是**重画的 —— 它是随起伏移动的标准头部精灵，框变化只因整个头平移。背面时
脸被头发盖住（§7）。

---

## 9. Idle stretch / 待机伸展

The idle is a **breath**: the legs stay planted and the upper body stretches.

待机是一次**呼吸**：腿钉住不动，上身伸展。

Measured (`idle_front`; side & back identical in kind):

实测（`idle_front`；侧面、背面同理）：

- **Legs + boots (physical y23–31) are pixel-identical between f0 and f1** — legs
  do not move. **腿和靴（物理 y23–31）两帧逐像素相同** —— 腿不动。
- **Upper body (head + torso + arms) shifts up 1 row on f0**: `f0[y] == f1[y+1]`
  for y = 0–17. f0 is the taller/stretch pose (content y2–31), f1 the rest pose
  (y3–31). **上身（头+躯干+手臂）在 f0 整体抬 1 行**：y=0–17 满足 `f0[y]==f1[y+1]`。
  f0 是伸展态（内容 y2–31），f1 是放松态（y3–31）。
- **Hip band y18–22 is redrawn as a bridge** between the fixed legs and the raised
  torso. **腰区 y18–22 重画当桥**，接住固定的腿和抬起的躯干。

**Spec rule / 规范规则** (the user's words): *the legs do not move; instead the
torso + arms are enlarged by one row and the head correspondingly goes up one row.*
Implement as: **torso extended 1 row by colour-sampling**, head `moveY −1`, arms
follow the torso, legs fixed.

**规范规则**（用户原话）：*腿不动，而是躯干和手臂扩大一行，头部相应上一行。* 实现：
**躯干取色延长 1 行**，头 `moveY −1`，臂随躯干，腿固定。

---

## 10. Transform hard rules / 变换硬约束

Unchanged from CLAUDE.md; never violated in the render path.

沿用 CLAUDE.md；渲染路径永不违反。

- Nearest-neighbour only; rotation by inverse mapping + nearest sampling.
  仅最近邻；旋转用逆映射 + 最近邻采样。
- Translations round to the integer pixel grid. No anti-aliasing, no blending —
  z-order wins. 平移取整到像素网格。无抗锯齿、无混合 —— z-order 胜出。
- **DETACH RULE**: a part must never gap from its neighbour. Safe primitives:
  `rootX/rootY`, `rotateDeg` (about own joint), `scaleY` (far-end shorten). Per-part
  `moveX/moveY` is unsafe. **脱节铁律**：部件绝不可与相邻部件裂开。安全动作：整体
  `rootX/rootY`、绕自身关节 `rotateDeg`、远端缩短 `scaleY`。部件级 `moveX/moveY` 不安全。
- A lerped positional amplitude must be ≥ 2 px (a 1 px amplitude flattens under
  `lround`). Idle & walk lay one keyframe per frame, so 1 px rasterises exactly.
  要插值的位置幅度必须 ≥ 2 像素（1 像素会被 `lround` 压平）。待机与行走每帧一个
  关键帧，所以 1 像素能精确光栅化。
- **16×16 warning**: below 32×32 a limb may be too small for rotation to read;
  auto-degrade to translation-only. **16×16 警告**：小于 32×32 时肢体太小看不出旋转，
  自动降级为纯平移。

---

## 11. Data model & API (software-ready) / 数据模型与接口（软件化）

The design is rule-based now, but the data model reserves slots so hand-drawn chips
and a real editor can replace the rules later.

本轮是规则化的，但数据模型预留了槽位，日后手绘芯片与真正的编辑器可替换规则。

### 11.1 Pose extensions / Pose 扩展

```cpp
// core.h — added to Pose (defaults keep every existing Pose working)
struct Pose {
    std::array<PartTransform, PART_COUNT> parts;
    float rootX = 0.f, rootY = 0.f;

    // Per-frame visibility: hide an occluded part this frame (e.g. far arm).
    // 逐帧可见性：本帧隐藏被遮挡的部件（如远端手臂）。
    std::array<bool, PART_COUNT> hidden{};

    // Per-frame palette swap: the two pants tones trading places.
    // 逐帧调色板换色：两条裤的两个主色互换。
    std::vector<std::pair<cv::Vec4b, cv::Vec4b>> swaps;   // from → to
};
```

- `hidden` is applied in `composeFrame`'s z-order loop (skip hidden parts).
  `hidden` 在 `composeFrame` 的 z-order 循环里生效（跳过隐藏部件）。
  **Companion**: `fillJointPinches` must skip repair on columns a hidden part
  vacates (a hidden arm's source pixels are still opaque and would be re-filled).
  **配套**：`fillJointPinches` 必须跳过隐藏部件空出的列（隐藏手臂在源图里仍不透明，
  会被重新填上）。
- `swaps` is one recolour pass right after blitting, **before** the seam repairs.
  `swaps` 是 blit 后、缝修补**前**的一次改色后处理。
  Do **not** reuse `PartLayer::RoleColor` (dead code: `role` is never read, single
  colour cannot express from→to, and its BGR clashes with the JSON's RGB).
  **不要**复用 `PartLayer::RoleColor`（死代码：`role` 从未被读、单色表达不了 from→to、
  BGR 与 JSON 的 RGB 冲突）。

### 11.2 VariantChip — reserved hand-drawn slot / 手绘芯片槽（预留）

Hip (y18–22) and boots (y26–31) are hand-redrawn per frame in the sheet. Rules
approximate them now; this slot lets hand-drawn chips replace the rules later.

髋部（y18–22）与靴（y26–31）在参考图里逐帧手绘。本轮用规则近似；此槽日后可换手绘芯片。

```cpp
struct VariantChip {
    enum class Slot { Hip, Boot } slot;   // which region / 哪个区域
    int  bindTo;                          // part id it rides on / 跟随的部件
    std::vector<cv::Mat> frames;          // one chip per frame / 每帧一张
};
// When present for a slot, the chip replaces the rule approximation for that frame.
// 当某槽位有芯片时，该帧用芯片替换规则近似。
```

### 11.3 PartLayer & composing / 图层与合成

`src/core/parts.h` already models swappable layers (`PartLayer`, `compositeCharacter`).
The composition order for this spec is bottom → top:

`src/core/parts.h` 已建模可换图层（`PartLayer`、`compositeCharacter`）。本规范的合成
顺序自下而上：

```
SKELETON < PANTS < CLOTHES < ARMS < HAIR
```

Data flow / 数据流:

```
PartLayer (swappable hair/clothes/pants)
        → compositeCharacter()  → standard frame → Rig → Animation → render
```

Fix `RoleColor` before reuse: give it a **source** colour (so from→to works), and
resolve the RGB-vs-BGR mismatch against `chibi_standard.json` (which is RGB).

复用前修 `RoleColor`：给它**源色**（from→to 才成立），并理顺与 `chibi_standard.json`
（RGB）的 RGB/BGR 顺序差异。

### 11.4 Torso extend (idle) / 躯干延长（待机）

The idle torso "enlarged by one row" is a **colour-sampled extension**: scale the
torso `scaleY = (h+1)/h` about its **bottom** pivot; nearest-neighbour inverse
mapping repeats one row at the top = a colour-sampled extra row. The head rides
`moveY −1`. The bottom stays pinned (DETACH), and the hip seam is bridged.

待机躯干「扩大一行」是**取色延长**：躯干绕**底部** pivot 做 `scaleY = (h+1)/h`；
最近邻逆映射在顶部重复一行 = 取色得到的额外行。头 `moveY −1` 随动。底部钉住（脱节
铁律），腰缝由重画/修补接桥。

> This is the one mechanism that needs pixel-exact verification before it is
> trusted; verify against the sheet's idle frames.
> 这是唯一需要先做像素精确验证的机制；要对照参考图的待机帧验证。

---

## 12. Summary checklist / 校验清单

- [ ] Head y2–12 is a single standard sprite, 0 diff across all walk frames.
      头部 y2–12 是单张标准精灵，全部行走帧 0 差异。
- [ ] `walk_up` / `walk_front` legs straight (geometry unchanged; only tone swap).
      背走/正走腿全直（几何不变，仅换色）。
- [ ] `walk_side` legs genuinely swing (6 variants). 侧走腿真摆（6 变体）。
- [ ] Idle: legs + boots pixel-identical; upper body up 1 row; hip bridged.
      待机：腿+靴逐像素不动；上身抬 1 行；腰接桥。
- [ ] Pants swap exactly two similar-hue tones per `tone_swap_groups`.
      裤子按 `tone_swap_groups` 交换两种相近色相的主色。
- [ ] Far arm hidden on 4 of 6 `walk_side` frames. 远臂在侧走 6 帧隐 4 帧。
- [ ] Shirt painted on the union of revealed cells. 衣服画在露出格子并集上。
- [ ] Back hair = horizontal mirror of front hair, covers face + torso.
      背发 = 正发水平翻转，盖住脸与躯干。
- [ ] DETACH RULE holds on every frame (1 blob, 0 holes). 每帧不脱节（1 blob、0 holes）。
