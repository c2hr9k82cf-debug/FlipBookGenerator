# syntax=docker/dockerfile:1
# FlipBookGenerator — web service image / 网站服务镜像
#
# Three views in, one four-direction sheet out, served over HTTP with an
# in-browser animation preview. Everything local: no cloud, no AI dependency.
# 三视图进，一张四方向大图出，HTTP 服务 + 浏览器动画预览。全程本地：
# 无云端，工具本身不依赖 AI。
#
# Build on the TARGET machine (native arch) / 在目标机器上构建（原生架构）:
#   docker build -t flipbook-web .
# Run / 运行:
#   docker run -d --name flipbook-web -p 8080:8080 -v flipbook-data:/data flipbook-web
# Then open http://<host>:8080/ / 然后打开 http://<主机>:8080/

# ---- build stage / 构建阶段 ----
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ca-certificates libopencv-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
# GUI is off: the image has no display and the web shell is a core-only
# consumer (the CLAUDE.md iron rule — flipbook_core never includes UI).
# GUI 关闭：镜像里没有显示器，网站薄壳只消费 core（CLAUDE.md 铁律 ——
# flipbook_core 永不 include UI）。
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
        -DFLIPBOOK_BUILD_GUI=OFF -DFLIPBOOK_BUILD_WEB=ON \
 && cmake --build build -j"$(nproc)" --target flipbook flipbook_server \
 && strip build/flipbook build/flipbook_server

# ---- runtime stage / 运行阶段 ----
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
# Runtime OpenCV = the three modules flipbook_core links. On 24.04 the package
# names carry the t64 suffix; fall back to libopencv-dev if a mirror renames
# them (same three modules come along either way).
# 运行时 OpenCV = flipbook_core 链接的三个模块。24.04 包名带 t64 后缀；若镜像
# 改名则回退装 libopencv-dev（两种方式都带齐这三个模块）。
RUN apt-get update && { apt-get install -y --no-install-recommends \
        libopencv-core406t64 libopencv-imgproc406t64 libopencv-imgcodecs406t64 \
     || apt-get install -y --no-install-recommends libopencv-dev; } \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/build/flipbook /src/build/flipbook_server /app/
COPY web /app/web
COPY assets /app/assets
# Job outputs (sheet.png / meta.json / uploads) live on the volume.
# 任务产物（大图 / 元数据 / 上传件）放在卷里。
VOLUME ["/data"]
EXPOSE 8080
ENTRYPOINT ["/app/flipbook_server", \
            "--port", "8080", \
            "--web", "/app/web", \
            "--jobs", "/data/jobs", \
            "--assets", "/app/assets"]
