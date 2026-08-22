#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

version="${1:-}"
if [[ ! "${version}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "用法: $0 vX.Y.Z" >&2
    exit 2
fi

version_dir="${RLCD_PROJECT_DIR}/dist/${version}"
factory_name="esp32-rlcd-firmware-${version}-factory.bin"
ota_name="esp32-rlcd-firmware-${version}-ota.bin"
factory_path="${version_dir}/${factory_name}"
ota_path="${version_dir}/${ota_name}"
if [[ ! -f "${factory_path}" || ! -f "${ota_path}" ||
      ! -f "${version_dir}/SHA256SUMS" ]]; then
    echo "发布目录不完整: ${version_dir}" >&2
    exit 1
fi

"${RLCD_PROJECT_DIR}/scripts/check-licenses.sh"
(
    cd "${version_dir}"
    sha256sum --check SHA256SUMS
)

release_dir="${RLCD_PROJECT_DIR}/build/release/${version}"
rm -rf -- "${release_dir}"
install -d -m 0755 "${release_dir}"
install -m 0644 "${factory_path}" "${release_dir}/${factory_name}"
install -m 0644 "${ota_path}" "${release_dir}/${ota_name}"
install -m 0644 "${version_dir}/SHA256SUMS" "${release_dir}/SHA256SUMS"
install -m 0644 "${RLCD_PROJECT_DIR}/LICENSE" "${release_dir}/LICENSE"
install -m 0644 "${RLCD_PROJECT_DIR}/NOTICE.md" "${release_dir}/NOTICE.md"
for preview_name in \
    home-screen.svg calendar-screen.svg \
    device-health.svg network-time.svg audio.svg \
    wifi-maintenance.svg online-update.svg local-update.svg \
    online-update-confirm.svg online-update-progress.svg \
    online-update-verify.svg online-update-result.svg \
    firmware-update.svg update-progress.svg \
    firmware-info.svg device-status.svg; do
    if [[ -f "${version_dir}/${preview_name}" ]]; then
        install -m 0644 "${version_dir}/${preview_name}" \
            "${release_dir}/${preview_name}"
    fi
done

license_archive="${release_dir}/esp32-rlcd-firmware-${version}-licenses.tar.gz"
tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner \
    -C "${RLCD_PROJECT_DIR}" -czf "${license_archive}" \
    LICENSE NOTICE.md LICENSES

(
    cd "${release_dir}"
    LC_ALL=C sha256sum -- * > RELEASE_SHA256SUMS
    sha256sum --check RELEASE_SHA256SUMS >/dev/null
)

echo "Release 文件已生成: ${release_dir}"
find "${release_dir}" -maxdepth 1 -type f -printf '  %f\n' | sort
echo "此脚本不会创建 GitHub Release，也不会修改仓库可见性。"
