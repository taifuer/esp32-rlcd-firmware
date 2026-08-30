#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

cd "${RLCD_PROJECT_DIR}"

git diff --check
git diff --cached --check
if git rev-parse --verify HEAD^ >/dev/null 2>&1; then
    git diff --check HEAD^ HEAD
else
    git show --check --format=oneline HEAD >/dev/null
fi

disallowed_paths_pattern='(^|/)(build|managed_components|third_party)(/|$)|(^|/)sdkconfig(\.old)?$|(^|/)dependencies\.lock$'
if git ls-files | grep -E "${disallowed_paths_pattern}" >/dev/null; then
    echo "Git 仓库包含不应跟踪的生成文件或第三方源码:" >&2
    git ls-files | grep -E "${disallowed_paths_pattern}" >&2
    exit 1
fi

if git grep -n -I -E '(/root/|/home/[^/]+/|[A-Za-z]:\\Users\\)' -- . \
    ':!LICENSES/**' ':!scripts/check-repository.sh' >/dev/null; then
    echo "跟踪文件包含本机绝对路径:" >&2
    git grep -n -I -E '(/root/|/home/[^/]+/|[A-Za-z]:\\Users\\)' -- . \
        ':!LICENSES/**' ':!scripts/check-repository.sh' >&2
    exit 1
fi

required_log_defaults=(
    'CONFIG_LOG_DEFAULT_LEVEL_INFO=y'
    'CONFIG_LOG_DEFAULT_LEVEL=3'
    'CONFIG_LOG_MAXIMUM_EQUALS_DEFAULT=y'
    'CONFIG_LOG_MAXIMUM_LEVEL=3'
)
for required_log_default in "${required_log_defaults[@]}"; do
    if [[ "$(grep -Fxc -- "${required_log_default}" sdkconfig.defaults || true)" -ne 1 ]]; then
        echo "sdkconfig.defaults 必须把默认与最高日志级别锁定为 INFO: ${required_log_default}" >&2
        exit 1
    fi
done
if grep -E '^CONFIG_LOG_(DEFAULT|MAXIMUM)_LEVEL=' sdkconfig.defaults \
    | grep -Fvx -e 'CONFIG_LOG_DEFAULT_LEVEL=3' \
        -e 'CONFIG_LOG_MAXIMUM_LEVEL=3' >/dev/null; then
    echo "sdkconfig.defaults 不得用其他数值覆盖 INFO 日志级别。" >&2
    grep -En '^CONFIG_LOG_(DEFAULT|MAXIMUM)_LEVEL=' sdkconfig.defaults >&2
    exit 1
fi
if grep -Eq '^CONFIG_LOG_(DEFAULT|MAXIMUM)_LEVEL_(DEBUG|VERBOSE)=y$' \
    sdkconfig.defaults; then
    echo "sdkconfig.defaults 不得启用 DEBUG 或 VERBOSE 日志。" >&2
    grep -En '^CONFIG_LOG_(DEFAULT|MAXIMUM)_LEVEL_(DEBUG|VERBOSE)=y$' \
        sdkconfig.defaults >&2
    exit 1
fi

while IFS= read -r -d '' markdown_file; do
    markdown_dir="$(dirname -- "${markdown_file}")"
    while IFS= read -r target; do
        target="${target#<}"
        target="${target%>}"
        case "${target}" in
            ''|\#*|http://*|https://*|mailto:*)
                continue
                ;;
        esac
        target="${target%%#*}"
        if [[ ! -e "${markdown_dir}/${target}" ]]; then
            echo "失效的本地 Markdown 链接: ${markdown_file} -> ${target}" >&2
            exit 1
        fi
    done < <(perl -ne 'while (/\]\(([^)]+)\)/g) { print "$1\n" }' "${markdown_file}")
done < <(git ls-files -z '*.md')

while IFS= read -r sums_file; do
    sums_dir="$(dirname -- "${sums_file}")"
    sums_name="$(basename -- "${sums_file}")"
    pushd "${sums_dir}" >/dev/null
    sha256sum --check "${sums_name}"
    popd >/dev/null
done < <(git ls-files 'dist/*/SHA256SUMS')

echo "仓库结构、文档链接和发布哈希检查通过。"
