#!/usr/bin/env bash
# 检查编译镜像所需的主机软件，列出缺少的项及安装命令（不代为安装）。
#
#   bash install.sh
#
# 交叉工具链随厂商输入准备，由 make 自动处理。应用开发用的编译器和 CANN
# 由 V610-App-SDK 的 install.sh 安装。

set -Eeuo pipefail

case ${1:-} in
    -h|--help)
        sed -n '2,7s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
        exit 0
        ;;
esac

# 镜像构建用到的主机工具（Buildroot 的必备项，加上本工程的脚本所需）。
TOOLS=(make gcc g++ patch perl tar gzip bzip2 xz unzip cpio rsync bc file which
       sed awk find git curl python3 cmake dtc sha256sum)
MISSING=()
for tool in "${TOOLS[@]}"; do
    command -v "$tool" >/dev/null 2>&1 || MISSING+=("$tool")
done

# 厂商的启动镜像工具用到的 Python 模块: <import 名>:<pip 包名>
PY_MODULES=(openpyxl:openpyxl Crypto:pycryptodome)
MISSING_PY=()
if command -v python3 >/dev/null 2>&1; then
    for item in "${PY_MODULES[@]}"; do
        python3 -c "import ${item%%:*}" >/dev/null 2>&1 || MISSING_PY+=("${item##*:}")
    done
fi

if ((${#MISSING[@]} > 0 || ${#MISSING_PY[@]} > 0)); then
    if ((${#MISSING[@]} > 0)); then
        printf '缺少主机工具: %s\n' "${MISSING[*]}" >&2
        printf '  Debian/Ubuntu: sudo apt-get install build-essential patch perl tar gzip bzip2 xz-utils unzip cpio rsync bc file git curl python3 python3-pip cmake device-tree-compiler\n' >&2
    fi
    if ((${#MISSING_PY[@]} > 0)); then
        printf '缺少 Python 模块: %s\n' "${MISSING_PY[*]}" >&2
        printf '  python3 -m pip install --user %s\n' "${MISSING_PY[*]}" >&2
    fi
    exit 1
fi
printf '主机环境检查通过。\n'
