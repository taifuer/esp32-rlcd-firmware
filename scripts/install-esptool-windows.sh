#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

check_only=false
if [[ "${1:-}" == "--check" ]]; then
    check_only=true
elif [[ $# -ne 0 ]]; then
    echo "用法: $0 [--check]" >&2
    exit 2
fi

esptool_exe="${RLCD_ESPTOOL_WINDOWS_DIR}/esptool.exe"
checksum_file="${RLCD_ESPTOOL_WINDOWS_DIR}/SHA256SUMS"

verify_installation() {
    [[ -f "${esptool_exe}" && -f "${checksum_file}" ]] || return 1
    (cd "${RLCD_ESPTOOL_WINDOWS_DIR}" && sha256sum --check --status SHA256SUMS)
}

if verify_installation; then
    echo "Windows esptool v${ESPTOOL_WINDOWS_VERSION} 已缓存且校验通过: ${esptool_exe}"
    exit 0
fi

if [[ "${check_only}" == true ]]; then
    echo "Windows esptool 尚未安装或本地校验失败: ${RLCD_ESPTOOL_WINDOWS_DIR}" >&2
    exit 1
fi

for command_name in sha256sum unzip; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "缺少命令: ${command_name}" >&2
        exit 1
    fi
done

asset_name="esptool-v${ESPTOOL_WINDOWS_VERSION}-windows-amd64.zip"
asset_url="https://github.com/espressif/esptool/releases/download/v${ESPTOOL_WINDOWS_VERSION}/${asset_name}"
temporary_dir="$(mktemp -d /tmp/rlcd-esptool-install.XXXXXX)"
archive_path="${temporary_dir}/${asset_name}"
staging_dir="${temporary_dir}/staging"

cleanup() {
    rm -rf "${temporary_dir}"
}
trap cleanup EXIT

if command -v wget >/dev/null 2>&1; then
    wget --output-document="${archive_path}" "${asset_url}"
elif command -v curl >/dev/null 2>&1; then
    curl --location --fail --show-error --output "${archive_path}" "${asset_url}"
else
    echo "需要 wget 或 curl 下载 Espressif 官方工具。" >&2
    exit 1
fi

printf '%s  %s\n' "${ESPTOOL_WINDOWS_ARCHIVE_SHA256}" "${archive_path}" | sha256sum --check --status
mkdir -p "${staging_dir}"
unzip -q -j "${archive_path}" \
    'esptool-windows-amd64/esptool.exe' \
    'esptool-windows-amd64/README.md' \
    'esptool-windows-amd64/LICENSE' \
    -d "${staging_dir}"

mkdir -p "${RLCD_ESPTOOL_WINDOWS_DIR}"
install -m 0755 "${staging_dir}/esptool.exe" "${esptool_exe}"
install -m 0644 "${staging_dir}/README.md" "${RLCD_ESPTOOL_WINDOWS_DIR}/UPSTREAM_README.md"
install -m 0644 "${staging_dir}/LICENSE" "${RLCD_ESPTOOL_WINDOWS_DIR}/LICENSE"
printf '%s\n%s\n%s\n' \
    "version=${ESPTOOL_WINDOWS_VERSION}" \
    "url=${asset_url}" \
    "archive_sha256=${ESPTOOL_WINDOWS_ARCHIVE_SHA256}" \
    > "${RLCD_ESPTOOL_WINDOWS_DIR}/SOURCE.txt"
(cd "${RLCD_ESPTOOL_WINDOWS_DIR}" && \
    sha256sum esptool.exe UPSTREAM_README.md LICENSE SOURCE.txt > SHA256SUMS)

verify_installation
echo "Windows esptool 已安装到仓库外部缓存: ${RLCD_ESPTOOL_WINDOWS_DIR}"
