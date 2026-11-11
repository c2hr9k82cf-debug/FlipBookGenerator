#!/usr/bin/env bash
# build_app.sh — build output/dist/FlipBookGenerator.app / 构建 .app
#
# Packaging rules (CLAUDE.md — the killer failure is a .app that crashes on a
# machine without Homebrew):
#   1. dylibbundler copies every OpenCV dylib into Contents/Frameworks and
#      rewrites install names to @executable_path/../Frameworks.
#   2. codesign (ad-hoc, arm64) runs AFTER dylibbundler, never before.
#   3. otool -L must show only system paths and @executable_path — anything
#      else would crash on a clean machine.
# 打包铁律（CLAUDE.md —— 最大坑是没装 Homebrew 的机器上双击即崩）：
#   1. dylibbundler 把 OpenCV 动态库拷进 Contents/Frameworks 并把 install name
#      改写为 @executable_path/../Frameworks。
#   2. codesign（arm64 ad-hoc）必须在 dylibbundler 之后，绝不在之前。
#   3. otool -L 只允许出现系统路径与 @executable_path —— 别的在干净机器上必崩。
set -euo pipefail

cd "$(dirname "$0")"

APP="output/dist/FlipBookGenerator.app"
EXE_IN_APP="$APP/Contents/MacOS/FlipBookGenerator"

# --- tool check / 工具检查 ---
command -v dylibbundler >/dev/null 2>&1 || {
    echo "dylibbundler not found — install it: brew install dylibbundler" >&2
    exit 1
}

# --- version from the single source of truth / 版本取自唯一来源 ---
VERSION=$(sed -n 's/^project(FlipBookGenerator VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
if [ -z "$VERSION" ]; then
    echo "cannot read VERSION from CMakeLists.txt project(...)" >&2
    exit 1
fi
echo "== FlipBookGenerator $VERSION =="

# --- build / 构建 ---
cmake -S . -B build >/dev/null
cmake --build build --target flipbook_gui -j4

# --- assemble the bundle / 组装 bundle ---
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/assets" "$APP/Contents/Frameworks"
cp build/flipbook_gui "$EXE_IN_APP"
# Templates (chips + character templates) and the starter parts library ship;
# the user's own views are dragged in at runtime. findChipsDir() /
# findStarterLibraryDir() look in ../Resources/assets.
# 捆模板（芯片 + 角色模板）与起步部件库；用户自己的视图运行时拖入。
# findChipsDir() / findStarterLibraryDir() 会找 ../Resources/assets。
cp -R assets/templates "$APP/Contents/Resources/assets/"
cp -R assets/parts_library "$APP/Contents/Resources/assets/"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>FlipBookGenerator</string>
    <key>CFBundleDisplayName</key>
    <string>FlipBookGenerator</string>
    <key>CFBundleExecutable</key>
    <string>FlipBookGenerator</string>
    <key>CFBundleIdentifier</key>
    <string>local.flipbook.generator</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$VERSION</string>
    <key>CFBundleVersion</key>
    <string>$VERSION</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSHumanReadableCopyright</key>
    <string>Local pixel-art flipbook tool / 本地像素风翻页工具</string>
</dict>
</plist>
PLIST

# --- bundle dylibs / 捆绑动态库 ---
# Install names use the @rpath prefix, resolved through the executable's one
# LC_RPATH (@executable_path/../Frameworks). This is not cosmetic: some dylibs
# (libgfortran via OpenBLAS) have no room for LONGER install names, and
# @rpath/libfoo.dylib never grows the load commands.
# install name 用 @rpath 前缀，经可执行文件唯一的 LC_RPATH
# （@executable_path/../Frameworks）解析。这不是为了好看：有些 dylib（经 OpenBLAS
# 进来的 libgfortran）装不下更长的 install name，而 @rpath/libfoo.dylib 永远不会
# 撑大 load command。
dylibbundler -od -b \
    -x "$EXE_IN_APP" \
    -d "$APP/Contents/Frameworks" \
    -p "@rpath"
# dylibbundler rewrites every old /opt/homebrew rpath — on the executable AND on
# each bundled dylib — to the literal "@rpath/", which confuses dyld's own @rpath
# expansion ("duplicate LC_RPATH '@rpath/'") and resolution fails even though the
# dylibs sit in Frameworks. Drop it everywhere; the one rpath that matters lives
# on the executable. A dylib may carry the literal entry MORE THAN ONCE
# (libopenblasp has three), so delete until none survive — capped, then asserted.
# dylibbundler 把旧的 /opt/homebrew rpath —— 可执行文件和每个捆绑 dylib 上的 ——
# 都改成了字面 "@rpath/"，搅乱 dyld 自己的 @rpath 展开（"duplicate LC_RPATH
# '@rpath/'"），导致解析失败，尽管 dylib 就在 Frameworks 里。全部删掉；真正需要的
# rpath 只留在可执行文件上。一个 dylib 可能带**多条**字面表项（libopenblasp 有
# 三条），所以删到一条不剩 —— 有次数上限，最后断言。
for f in "$EXE_IN_APP" "$APP/Contents/Frameworks/"*.dylib; do
    for _ in 1 2 3 4 5; do
        otool -l "$f" | grep -q 'path @rpath/ (offset' || break
        install_name_tool -delete_rpath "@rpath/" "$f" 2>/dev/null || true
    done
done
if otool -l "$EXE_IN_APP" "$APP/Contents/Frameworks/"*.dylib | grep -q 'path @rpath/ (offset'; then
    echo "FAIL — a literal '@rpath/' LC_RPATH survived; dyld would fail at launch" >&2
    exit 1
fi
install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXE_IN_APP"

# --- ad-hoc sign AFTER dylibbundler / dylibbundler 之后 ad-hoc 签名 ---
codesign --force --deep -s - "$APP"

# --- verify: nothing foreign in the dependency list ---
# --- 验证：依赖清单里不得有外来路径 ---
echo "== otool -L check / 依赖检查 =="
BAD=$(otool -L "$EXE_IN_APP" | tail -n +2 | awk '{print $1}' | grep -v -e '^/usr/lib/' -e '^/System/' -e '^@executable_path/' -e '^@rpath/' || true)
if [ -n "$BAD" ]; then
    echo "FAIL — unresolved dependencies / 未解析的依赖:" >&2
    echo "$BAD" >&2
    exit 1
fi
if ! otool -l "$EXE_IN_APP" | grep -q '@executable_path/../Frameworks'; then
    echo "FAIL — the executable has no LC_RPATH into Contents/Frameworks" >&2
    exit 1
fi
echo "OK — all dependencies are bundled or system / 全部依赖已捆绑或是系统库"

SIZE=$(du -sh "$APP" | awk '{print $1}')
echo "Built $APP ($SIZE)"
echo "Test on a CLEAN machine (no Homebrew) before shipping. / 发布前请在干净机器（无 Homebrew）上实测。"
