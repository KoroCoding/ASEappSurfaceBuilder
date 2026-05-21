#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
build_dir="${script_dir}/build"
dist_dir="${repo_root}/standalone_exe/macos"
env_name="${ASEAPP_CONDA_ENV:-aseapp-surface-builder}"
stage_dir="${ASEAPP_MACOS_STAGE_DIR:-/private/tmp/aseapp-surface-builder-cpack}"
parallel="${ASEAPP_BUILD_PARALLEL:-$(sysctl -n hw.ncpu 2>/dev/null || echo 2)}"
default_identity="ASEapp Surface Builder Local Code Signing"

apply_dmg_volume_icon() {
  local source_dmg="$1"
  local icon_path="$2"

  if [[ ! -f "${icon_path}" ]]; then
    echo "DMG volume icon was not found: ${icon_path}" >&2
    printf '%s\n' "${source_dmg}"
    return 0
  fi
  if ! command -v SetFile >/dev/null 2>&1; then
    echo "SetFile was not found; skipping DMG volume icon." >&2
    printf '%s\n' "${source_dmg}"
    return 0
  fi

  local source_base
  source_base="$(basename "${source_dmg}" .dmg)"
  local rw_base="${stage_dir}/${source_base}-volume-icon-rw"
  local rw_dmg="${rw_base}.dmg"
  local final_base="${stage_dir}/${source_base}-volume-icon"
  local final_dmg="${final_base}.dmg"
  local mount_dir="${stage_dir}/volume-icon-mount"

  rm -rf "${mount_dir}"
  rm -f "${rw_dmg}" "${final_dmg}"
  mkdir -p "${mount_dir}"

  hdiutil convert "${source_dmg}" -format UDRW -o "${rw_base}" >/dev/null
  hdiutil attach -nobrowse -readwrite -mountpoint "${mount_dir}" "${rw_dmg}" >/dev/null

  cp "${icon_path}" "${mount_dir}/.VolumeIcon.icns"
  SetFile -c icnC "${mount_dir}/.VolumeIcon.icns" || true
  SetFile -a C "${mount_dir}" || true
  sync

  hdiutil detach "${mount_dir}" >/dev/null
  hdiutil convert "${rw_dmg}" -format UDZO -imagekey zlib-level=9 -o "${final_base}" >/dev/null
  rm -f "${rw_dmg}"
  mv -f "${final_dmg}" "${source_dmg}"

  printf '%s\n' "${source_dmg}"
}

if command -v conda >/dev/null 2>&1; then
  run_cmd=(conda run -n "${env_name}")
  env_prefix="$("${run_cmd[@]}" python -c 'import os; print(os.environ["CONDA_PREFIX"])')"
else
  if [[ -z "${CONDA_PREFIX:-}" ]]; then
    echo "conda was not found and CONDA_PREFIX is not set." >&2
    echo "Install the environment with: conda env create -f environment.yml" >&2
    exit 1
  fi
  run_cmd=()
  env_prefix="${CONDA_PREFIX}"
fi

if [[ -z "${ASEAPP_CODESIGN_IDENTITY:-}" ]]; then
  "${script_dir}/tools/macos/create_self_signed_codesign_cert.sh"
  export ASEAPP_CODESIGN_IDENTITY="${default_identity}"
fi

if [[ -n "${ASEAPP_NOTARY_PROFILE:-}" && -z "${ASEAPP_ENABLE_HARDENED_RUNTIME:-}" ]]; then
  export ASEAPP_ENABLE_HARDENED_RUNTIME=1
fi

mkdir -p "${dist_dir}"
rm -rf "${stage_dir}"
mkdir -p "${stage_dir}"

"${run_cmd[@]}" cmake \
  -S "${script_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DCMAKE_PREFIX_PATH="${env_prefix}" \
  -DCMAKE_BUILD_TYPE=Release

"${run_cmd[@]}" cmake \
  --build "${build_dir}" \
  --config Release \
  --parallel "${parallel}"

# Stage CPack output outside Documents/GitHub. On macOS FileProvider-backed
# folders can add FinderInfo/resource-fork metadata that breaks codesign.
"${run_cmd[@]}" cpack \
  --config "${build_dir}/CPackConfig.cmake" \
  -D "CPACK_PACKAGE_DIRECTORY=${stage_dir}"

app_path="$(find "${stage_dir}/_CPack_Packages/Darwin/DragNDrop" -type d -name 'ASEappNativeUI.app' -print -quit)"
dmg_path="$(find "${stage_dir}" -type f -name 'ASEappSurfaceBuilder-*-macOS.dmg' -print -quit)"

if [[ -z "${app_path}" || -z "${dmg_path}" ]]; then
  echo "macOS package was not generated correctly." >&2
  exit 1
fi

codesign --verify --deep --strict --verbose=2 "${app_path}"
dmg_icon_path="${build_dir}/generated/icons/aseapp_surface_builder_icon.icns"
dmg_path="$(apply_dmg_volume_icon "${dmg_path}" "${dmg_icon_path}")"
hdiutil verify "${dmg_path}" >/dev/null

if [[ -n "${ASEAPP_CODESIGN_IDENTITY:-}" && "${ASEAPP_CODESIGN_IDENTITY}" != "-" ]]; then
  codesign --force --sign "${ASEAPP_CODESIGN_IDENTITY}" "${dmg_path}"
  codesign --verify --verbose=2 "${dmg_path}"
fi

if [[ -n "${ASEAPP_NOTARY_PROFILE:-}" ]]; then
  if [[ "${ASEAPP_CODESIGN_IDENTITY:-}" == "" || "${ASEAPP_CODESIGN_IDENTITY}" == "-" ]]; then
    echo "ASEAPP_NOTARY_PROFILE requires ASEAPP_CODESIGN_IDENTITY to be a Developer ID Application identity." >&2
    exit 1
  fi
  xcrun notarytool submit "${dmg_path}" --keychain-profile "${ASEAPP_NOTARY_PROFILE}" --wait
  xcrun stapler staple "${dmg_path}"
  xcrun stapler validate "${dmg_path}"
fi

final_dmg="${dist_dir}/$(basename "${dmg_path}")"
cp "${dmg_path}" "${final_dmg}"

echo "Created ${final_dmg}"
echo "Staged app verified at ${app_path}"
