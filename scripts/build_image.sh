#!/usr/bin/env bash
# build_image.sh — build the web-service image locally (needs Docker/colima)
# 本机构建网站服务镜像（需要 Docker/colima）
set -euo pipefail
cd "$(dirname "$0")/.."
docker build -t flipbook-web:0.14.0 -t flipbook-web:latest .
echo
echo "Run / 运行:"
echo "  docker run -d --name flipbook-web -p 8080:8080 -v flipbook-data:/data flipbook-web"
echo "Then open / 然后打开: http://localhost:8080/"
