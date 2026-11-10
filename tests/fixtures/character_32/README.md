# Test fixture: character_32 / 测试夹具：character_32

A complete body-part bundle extracted from `assets/character_32.png` (32x32),
used as the committed test input for the reconstruction gate and the animation
engine. All 6 part PNG dimensions match their `_bbox.txt` w/h exactly.
从 `assets/character_32.png`（32x32）提取的完整部件包，用作重建校验和动画引擎的
提交测试输入。6 个部件 PNG 尺寸与其 `_bbox.txt` 的 w/h 完全一致。

## Files / 文件

| File | Format | Meaning |
|---|---|---|
| `{name}.png` | BGRA | the part sprite, cropped to its bounding box / 部件精灵图，已裁到包围盒 |
| `{name}_bbox.txt` | `x y w h` | placement in **original-image coordinates** / 在**原图坐标**中的位置 |
| `anchors.txt` | `partId x y` | one rotation pivot per part, in original-image coords / 每部件一个旋转锚点，原图坐标 |
| `image_path.txt` | path | source image, relative to project root / 源图路径，相对项目根 |

Reconstruction invariant / 重建不变量：blit each `{name}.png` at its `_bbox.txt`
offset onto a transparent 32x32 canvas — the result must equal
`assets/character_32.png` **pixel-exact, including alpha**.

## Provenance / 来源

These artifacts were produced by the now-lost `animator`-era extraction run
(2026-06-12) and recovered from `output/parts/` during Phase 0 cleanup. They
survived only as untracked working-tree files — never committed. See
`docs/lost-animator-symbols.txt`.
这些产物出自现已丢失的 `animator` 时期那次提取（2026-06-12），在 Phase 0 清理时
从 `output/parts/` 抢救回来。它们当时只是未跟踪的工作区文件，从未提交。见
`docs/lost-animator-symbols.txt`。

`anchors.txt` has been **regenerated** in the current single-pivot format
(`partId x y`) from these bboxes via `defaultPivot()` in `src/utils.cpp`. The
original 2-anchor file used `# partId index x y` with bbox top/bottom centers;
that model is discarded because it put arm pivots on each arm's own mid-axis
rather than at the shoulder joint. Original values are recorded in
`docs/lost-animator-symbols.txt` §3.
`anchors.txt` 已按当前单 pivot 格式（`partId x y`）用 `src/utils.cpp` 的
`defaultPivot()` 根据这些 bbox 重新生成。旧的 2 锚点格式（包围盒上下中点）已推翻，
因为它把手臂锚点放在手臂自身中轴上而非肩关节。旧值记录在
`docs/lost-animator-symbols.txt` §3。
