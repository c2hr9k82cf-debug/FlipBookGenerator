#!/usr/bin/env bash
# deploy_to_vm.sh — ship source to a Linux box, build the image there, run it
# 把源码送到 Linux 机器、在那边构建镜像并启动服务
#
# The image is built ON the target so it always matches the target's CPU arch
# and needs no container runtime on this Mac. Docker only has to exist on the
# target. / 镜像在目标机上构建 —— 架构必然匹配，本机也不需要容器运行时。
# 目标机上有 Docker 即可。
#
# Usage / 用法:
#   scripts/deploy_to_vm.sh user@host [remote_dir]
# Example / 示例:
#   scripts/deploy_to_vm.sh lin@192.168.64.3
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET="${1:?usage: deploy_to_vm.sh user@host [remote_dir]}"
REMOTE_DIR="${2:-flipbook_web}"
HOST="${TARGET#*@}"

echo "== connectivity / 连通性 =="
if ! ssh -o ConnectTimeout=5 -o BatchMode=yes "$TARGET" true 2>/dev/null; then
    # BatchMode probes without a password prompt; if that fails, try normally
    # (password auth) so the user gets the chance to type it.
    # BatchMode 探测不弹密码框；失败则正常连接（密码认证），让用户有机会输入。
    if ! ssh -o ConnectTimeout=5 "$TARGET" true; then
        echo "无法连接 $TARGET —— 请检查虚拟机是否在运行、网络是否通。" >&2
        exit 1
    fi
fi

echo "== pack source / 打包源码 =="
TAR="/tmp/flipbook_web_src.tar.gz"
# The working tree, not a commit: deploy what is on disk. Excludes keep the
# tar lean. / 打工作区而不是某次提交：送的就是盘上这份。排除项保持包精简。
tar czf "$TAR" \
    --exclude='build' --exclude='output' --exclude='.git' \
    --exclude='*.app' --exclude='__pycache__' --exclude='.DS_Store' \
    .

echo "== copy + build + run on $TARGET / 传到目标机构建并运行 =="
scp -q "$TAR" "$TARGET:~/flipbook_web_src.tar.gz"
ssh "$TARGET" "
    set -e
    rm -rf ~/$REMOTE_DIR && mkdir -p ~/$REMOTE_DIR
    tar xzf ~/flipbook_web_src.tar.gz -C ~/$REMOTE_DIR
    command -v docker >/dev/null || {
        echo '目标机没有 docker。安装：sudo apt-get install -y docker.io && sudo usermod -aG docker \$USER' >&2
        exit 1
    }
    docker build -t flipbook-web ~/$REMOTE_DIR
    docker rm -f flipbook-web 2>/dev/null || true
    docker run -d --name flipbook-web --restart unless-stopped \
        -p 8080:8080 -v flipbook-data:/data flipbook-web
"

echo
echo "done / 完成 —— 打开 http://$HOST:8080/"
echo "logs / 日志: ssh $TARGET 'docker logs -f flipbook-web'"
