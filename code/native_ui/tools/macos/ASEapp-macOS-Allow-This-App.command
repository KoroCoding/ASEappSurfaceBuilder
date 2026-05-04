#!/usr/bin/env bash
set -euo pipefail

base_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
app_source="${base_dir}/ASEappNativeUI.app"
cert_path="${base_dir}/ASEappSurfaceBuilderLocalCodeSigning.cer"
install_dir="${ASEAPP_INSTALL_DIR:-${HOME}/Applications}"
app_target="${install_dir}/ASEappNativeUI.app"
label="ASEapp Surface Builder"

if [[ ! -d "${app_source}" ]]; then
  echo "ASEappNativeUI.app was not found next to this script."
  echo "Run this script from the mounted ASEappSurfaceBuilder DMG."
  read -r -p "Press Return to close."
  exit 1
fi

if [[ ! -f "${cert_path}" ]]; then
  echo "The self-signed public certificate was not found next to this script."
  read -r -p "Press Return to close."
  exit 1
fi

echo "This script will:"
echo "  1. Copy ASEappNativeUI.app to ${install_dir}"
echo "  2. Trust the bundled self-signed code-signing certificate in your login keychain"
echo "  3. Remove the download quarantine flag from the copied app"
echo "  4. Register a local Gatekeeper exception where macOS allows it"
echo "  5. Open the app"
echo
echo "Only continue if you received this DMG from a source you trust."
echo
if [[ "${ASEAPP_ASSUME_YES:-}" != "1" ]]; then
  read -r -p "Continue? [y/N] " answer
  case "${answer}" in
    y|Y|yes|YES) ;;
    *) echo "Cancelled."; exit 1 ;;
  esac
fi

mkdir -p "${install_dir}"
rm -rf "${app_target}"
ditto "${app_source}" "${app_target}"

security add-trusted-cert \
  -r trustRoot \
  -p codeSign \
  -k "${HOME}/Library/Keychains/login.keychain-db" \
  "${cert_path}" >/dev/null 2>&1 || true

xattr -dr com.apple.quarantine "${app_target}" >/dev/null 2>&1 || true
spctl --add --label "${label}" "${app_target}" >/dev/null 2>&1 || true

codesign --verify --deep --strict "${app_target}"
open "${app_target}"

echo
echo "ASEapp Surface Builder was installed and opened from:"
echo "${app_target}"
read -r -p "Press Return to close."
