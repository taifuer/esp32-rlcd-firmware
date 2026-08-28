#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

readonly RLCD_MODEL_OFFSET=$((0x610000))
readonly RLCD_MODEL_PARTITION_SIZE=$((0x300000))

version="${1:-}"
if [[ ! "${version}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "用法: $0 vX.Y.Z" >&2
    exit 2
fi

IFS='.' read -r version_major version_minor _ <<<"${version#v}"
include_voice_model=false
if ((10#${version_major} > 0 ||
     (10#${version_major} == 0 && 10#${version_minor} >= 18))); then
    include_voice_model=true
fi

version_dir="${RLCD_PROJECT_DIR}/dist/${version}"
factory_name="esp32-rlcd-firmware-${version}-factory.bin"
ota_name="esp32-rlcd-firmware-${version}-ota.bin"
model_name="esp32-rlcd-firmware-${version}-model.bin"
factory_path="${version_dir}/${factory_name}"
ota_path="${version_dir}/${ota_name}"
model_source_path="${version_dir}/${model_name}"
model_manifest_path="${RLCD_PROJECT_DIR}/src/audio/include/voice_model_manifest.h"
if [[ ! -f "${factory_path}" || ! -f "${ota_path}" ||
      ! -f "${version_dir}/SHA256SUMS" ]]; then
    echo "发布目录不完整: ${version_dir}" >&2
    exit 1
fi

if [[ "${include_voice_model}" == true ]]; then
    if [[ ! -s "${model_source_path}" ]]; then
        echo "v0.18.0 及更新版本发布目录缺少离线语音模型: ${model_source_path}" >&2
        exit 1
    fi
    model_bytes="$(stat -c '%s' "${model_source_path}")"
    if [[ ! -f "${model_manifest_path}" ]]; then
        echo "缺少离线语音模型清单: ${model_manifest_path}" >&2
        exit 1
    fi
    manifest_model_bytes="$(sed -nE \
        's/^#define AUDIO_VOICE_MODEL_IMAGE_SIZE ([0-9]+)U$/\1/p' \
        "${model_manifest_path}")"
    manifest_model_sha256="$(sed -nE \
        's/^[[:space:]]*"([0-9a-f]{64})"$/\1/p' \
        "${model_manifest_path}")"
    read -r actual_model_sha256 _ < <(sha256sum "${model_source_path}")
    if [[ -z "${manifest_model_bytes}" ||
          -z "${manifest_model_sha256}" ||
          "${model_bytes}" != "${manifest_model_bytes}" ||
          "${actual_model_sha256}" != "${manifest_model_sha256}" ]]; then
        printf '发布模型与固件清单不一致: size=%s/%s sha256=%s/%s\n' \
            "${model_bytes}" "${manifest_model_bytes:-unparsed}" \
            "${actual_model_sha256}" "${manifest_model_sha256:-unparsed}" >&2
        exit 1
    fi
    if ((model_bytes > RLCD_MODEL_PARTITION_SIZE)); then
        printf '离线语音模型超过 0x%x 字节: %u\n' \
            "${RLCD_MODEL_PARTITION_SIZE}" "${model_bytes}" >&2
        exit 1
    fi
    factory_bytes="$(stat -c '%s' "${factory_path}")"
    if ((factory_bytes < RLCD_MODEL_OFFSET + model_bytes)); then
        echo "Factory 镜像没有覆盖完整离线语音模型区域" >&2
        exit 1
    fi
    if ! cmp --silent --bytes="${model_bytes}" \
        "${model_source_path}" "${factory_path}" 0 \
        "${RLCD_MODEL_OFFSET}"; then
        echo "Factory 镜像中的模型与发布目录 -model.bin 不一致" >&2
        exit 1
    fi
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
if [[ "${include_voice_model}" == true ]]; then
    install -m 0644 "${model_source_path}" "${release_dir}/${model_name}"
fi
install -m 0644 "${RLCD_PROJECT_DIR}/LICENSE" "${release_dir}/LICENSE"
install -m 0644 "${RLCD_PROJECT_DIR}/NOTICE.md" "${release_dir}/NOTICE.md"
for preview_name in \
    home-screen.svg calendar-screen.svg image-screen.svg image-delete-confirm.svg \
    device-health.svg network-time.svg audio.svg voice.svg \
    wifi-maintenance.svg online-update.svg local-update.svg \
    status.svg settings.svg alarm.svg settings-portal.svg \
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
    firmware_files=("${factory_name}" "${ota_name}")
    if [[ "${include_voice_model}" == true ]]; then
        firmware_files+=("${model_name}")
    fi
    LC_ALL=C sha256sum -- "${firmware_files[@]}" > SHA256SUMS
    sha256sum --check SHA256SUMS >/dev/null
    LC_ALL=C sha256sum -- * > RELEASE_SHA256SUMS
    sha256sum --check RELEASE_SHA256SUMS >/dev/null
)

echo "Release 文件已生成: ${release_dir}"
find "${release_dir}" -maxdepth 1 -type f -printf '  %f\n' | sort
echo "此脚本不会创建 GitHub Release，也不会修改仓库可见性。"
