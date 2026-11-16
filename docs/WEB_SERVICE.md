# FlipBookGenerator — Web Service / 网站服务

> v0.14.0. Three template-aligned views in (browser upload), one four-direction
> sheet + engine-agnostic metadata out (download links), with an in-browser
> animation preview. The `.app`'s pipeline, served over HTTP — everything runs
> locally on your own machine.
>
> v0.14.0。三张模板对齐视图进（浏览器上传），一张四方向大图 + 引擎无关元数据出
> （下载链接），浏览器里直接预览动画。`.app` 的管线搬到 HTTP 上 —— 全程跑在你
> 自己的机器上。
>
> English on top, Chinese below — per the project's bilingual convention.
> 英文在上，中文下 —— 沿用项目双语约定。

---

## 1. Architecture / 架构

```
浏览器 web/index.html            flipbook_server (C++)           flipbook_core
  三图上传 + canvas 预览   →   POST /api/convert (multipart)  →  buildFourDirSheet
  下载 sheet.png / meta.json ←   job 目录 + JSON 响应          ←  （宽容模式）
```

- `src/web/server_main.cpp` — the HTTP shell (vendored cpp-httplib v0.63.0,
  MIT, `third_party/httplib/`). Loads chips + template once, converts per
  request, keeps jobs for 24h. / HTTP 薄壳（vendored cpp-httplib v0.63.0，
  MIT）。启动时载入芯片与模板，逐请求转换，任务保留 24 小时。
- The conversion is the **same `buildFourDirSheet` call the GUI and the CLI
  make** — the golden invariant holds by construction, and is asserted
  byte-for-byte in testing (see §4). 转换用的是**与 GUI/CLI 相同的
  `buildFourDirSheet` 调用** —— 金不变量构造性成立，且有逐字节断言（见 §4）。
- Semantics = the GUI's **tolerant** mode: a silhouette diff or a minority
  colour is warned and repaired, never rejected. The CLI twin is
  `flipbook --fourdir --tolerant`. / 语义 = GUI 的**宽容**模式：剪影差或少数派
  色只提醒并修复，绝不拒绝。CLI 孪生是 `flipbook --fourdir --tolerant`。
- Every upload passes through **auto-align** (the GUI's 自动对齐): integer
  scale 1–4 × ±8px search against the view's idle chip. An already-aligned
  view returns identity, so clean inputs are untouched. / 每张上传都过
  **自动对齐**（GUI 的「自动对齐」）：对视图 idle 芯片做整数缩放 1–4 × ±8px
  搜索。已对齐的图返回恒等 —— 干净输入不受影响。
- `flipbook_core` still never includes a UI header (the iron rule). The
  server is just another core consumer. / `flipbook_core` 照旧永不 include
  UI 头（铁律）。服务端只是 core 的又一个消费者。

## 2. API

| Endpoint / 接口 | Meaning / 含义 |
|---|---|
| `GET /` | the frontend page / 前端页面 |
| `GET /health` | `{"ok":true,"version":"0.14.0"}` |
| `POST /api/convert` | multipart fields `front`, `side`, `back` (PNG ≤16MB each) → JSON |
| `GET /api/jobs/<id>/sheet.png` | the 192×256 sheet (download link) / 大图（下载链接） |
| `GET /api/jobs/<id>/meta.json` | engine-agnostic metadata / 引擎无关元数据 |
| `GET /api/jobs/<id>/diff_<view>.png` | per-view diff overlay, only when there were diffs / 差异标注图，仅有差异时存在 |

`POST /api/convert` response / 响应：

```json
{
  "ok": true,
  "job": "b0ebc05be333",
  "sheet_url": "/api/jobs/b0ebc05be333/sheet.png",
  "meta_url": "/api/jobs/b0ebc05be333/meta.json",
  "diff_urls": {"front": ".../diff_front.png"},
  "warnings": ["正面：有 25 个像素配色不一致（见差异图），已按主色处理。…"],
  "meta": { "…same as meta.json…" }
}
```

Errors / 错误：`{"ok":false,"error":"…"}` with HTTP 400 (bad input) / 404 (no
such job) / 500 (I/O). Input contract: **PNG with alpha**; field names are
exactly `front` / `side` / `back`. / 输入契约：**带透明通道的 PNG**；字段名恰为
`front` / `side` / `back`。

`meta.json` (engine-agnostic — this is the Phase 5 export contract) /
元数据（引擎无关 —— 即 Phase 5 导出契约）：

```json
{
  "app": "FlipBookGenerator", "version": "0.14.0",
  "frame": {"w": 32, "h": 32},
  "sheet": {"file": "sheet.png", "w": 192, "h": 256, "cols": 6, "rows": 8},
  "animations": [
    {"name": "down_idle",  "row": 0, "frames": 2, "fps": 4,  "loop": true, "mirror": false},
    {"name": "down_walk",  "row": 1, "frames": 6, "fps": 10, "loop": true, "mirror": false},
    {"name": "up_idle",    "row": 2, "frames": 2, "fps": 4,  "loop": true, "mirror": false},
    {"name": "up_walk",    "row": 3, "frames": 6, "fps": 10, "loop": true, "mirror": false},
    {"name": "left_idle",  "row": 4, "frames": 2, "fps": 4,  "loop": true, "mirror": true},
    {"name": "left_walk",  "row": 5, "frames": 6, "fps": 10, "loop": true, "mirror": true},
    {"name": "right_idle", "row": 6, "frames": 2, "fps": 4,  "loop": true, "mirror": false},
    {"name": "right_walk", "row": 7, "frames": 6, "fps": 10, "loop": true, "mirror": false}
  ],
  "warnings": ["…"],
  "inputs": [{"view": "front", "w": 32, "h": 32,
              "align": {"scale": 1, "dx": 0, "dy": 0},
              "mask_diff_px": 0, "colour_conflict_px": 25}]
}
```

Sheet layout / 大图布局: 8 rows × 6 cols × 32px — rows down_idle / down_walk /
up_idle / up_walk / left_idle / left_walk / right_idle / right_walk. Idle rows
hold 2 real frames (the sheet cycles them f0 f1 f0 f1 f0 f1 — a player should
use `frames`, not the row width). Left rows are exact horizontal mirrors of
right. fps values come from `motion_chips/index.txt`. / 待机行实际 2 帧
（大图里循环铺成 f0 f1 f0 f1 f0 f1 —— 播放器按 `frames` 取，不要按行宽取）。
左侧行是右侧行的精确水平镜像。fps 取自 `motion_chips/index.txt`。

## 3. Run & deploy / 运行与部署

Local (dev) / 本机（开发）：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release      # FLIPBOOK_BUILD_WEB=ON 默认开
cmake --build build --target flipbook_server
./build/flipbook_server --port 8080 --web web --jobs /tmp/fb_jobs --assets assets
# open http://localhost:8080/  （或跑 scripts/web_smoke.sh 冒烟）
```

Container / 容器：

```bash
# on the target machine (native arch) / 在目标机器上（原生架构）
docker build -t flipbook-web .
docker run -d --name flipbook-web --restart unless-stopped \
    -p 8080:8080 -v flipbook-data:/data flipbook-web
# open http://<host>:8080/
```

Or one shot from this Mac / 或在 Mac 上一键部署（源码送过去、那边构建）：

```bash
scripts/deploy_to_vm.sh lin@192.168.64.3
```

The image bundles: the two binaries, `web/` (frontend) and `assets/`
(templates + motion chips, 536KB). No compiler, no Homebrew, no display.
Jobs (uploads + outputs) live in the `/data` volume. / 镜像打包：两个二进制、
`web/`（前端）、`assets/`（模板 + 动作芯片，536KB）。无编译链、无 Homebrew、
无显示器。任务（上传 + 产物）在 `/data` 卷里。

## 4. Verification / 验证

- `scripts/web_smoke.sh [base_url]` — health / convert / sheet / meta round
  trip against the bundled 肯德基员工 views, with structure asserts.
  / 健康 / 转换 / 大图 / 元数据往返 + 结构断言。
- **Golden invariant through the web path / 金不变量过 web 路径**:
  web sheet == CLI `--fourdir` output, byte-identical (strict-clean inputs),
  and == CLI `--fourdir --tolerant` output, byte-identical (inputs with
  minority repairs). Verified on the reference chip frames and the
  band-recoloured KFC views respectively. / web 大图与 CLI `--fourdir` 逐字节
  一致（严格干净输入），并与 CLI `--fourdir --tolerant` 逐字节一致（带少数派
  修复的输入）。分别在参考芯片帧与分带换色的肯德基视图上验证过。
- `ctest` 34/34 unaffected. / 34/34 不受影响。

## 5. Security notes / 安全须知

The service is a **LAN tool**, like the `.app` is a desktop tool. It has no
authentication. What it does enforce / 服务是**局域网工具**，与 `.app` 是桌面
工具同理。它没有鉴权。它强制的是：

- PNG-only uploads, ≤16MB per image, ≤64MB per request. / 只收 PNG、单图
  ≤16MB、整请求 ≤64MB。
- Job ids are server-generated hex; the file route matches only two fixed
  names — no user text ever reaches a filesystem path. / 任务 id 是服务端生成
  的十六进制；文件路由只匹配两个固定文件名 —— 用户文本绝不进文件系统路径。
- Jobs are deleted after 24h (and beyond 256 kept). / 任务 24 小时后删除
  （超过 256 个删最旧）。
- Everything is computed locally; nothing is uploaded anywhere. / 一切本地
  计算，不向任何外部服务上传。

If you expose it beyond your LAN, put it behind a reverse proxy with auth and
TLS. / 若要暴露到局域网之外，请套一层带鉴权与 TLS 的反向代理。
