#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
native_ui_dir="$(cd -- "${script_dir}/../.." && pwd)"
cert_dir="${native_ui_dir}/certs"
common_name="${ASEAPP_SELF_SIGNED_CERT_NAME:-ASEapp Surface Builder Local Code Signing}"
keychain="${ASEAPP_KEYCHAIN:-${HOME}/Library/Keychains/login.keychain-db}"
cert_pem="${cert_dir}/ASEappSurfaceBuilderLocalCodeSigning.pem"
cert_der="${cert_dir}/ASEappSurfaceBuilderLocalCodeSigning.cer"
key_pem="${cert_dir}/ASEappSurfaceBuilderLocalCodeSigning.key.pem"
p12_path="${cert_dir}/ASEappSurfaceBuilderLocalCodeSigning.p12"
openssl_config="${cert_dir}/openssl-codesign.cnf"

mkdir -p "${cert_dir}"

if security find-identity -v -p codesigning | grep -Fq "\"${common_name}\""; then
  security find-certificate -c "${common_name}" -p "${keychain}" >"${cert_pem}"
  openssl x509 -in "${cert_pem}" -outform der -out "${cert_der}"
  echo "Using existing code signing identity: ${common_name}"
  echo "Exported public certificate: ${cert_der}"
  exit 0
fi

cat >"${openssl_config}" <<EOF
[ req ]
default_bits = 3072
distinguished_name = req_distinguished_name
x509_extensions = v3_codesign
prompt = no
default_md = sha256

[ req_distinguished_name ]
CN = ${common_name}
O = ASEapp
OU = Surface Builder

[ v3_codesign ]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
EOF

openssl req \
  -new \
  -x509 \
  -days 3650 \
  -nodes \
  -newkey rsa:3072 \
  -keyout "${key_pem}" \
  -out "${cert_pem}" \
  -config "${openssl_config}"

openssl x509 -in "${cert_pem}" -outform der -out "${cert_der}"

p12_password="$(uuidgen | tr -d '-')"
openssl pkcs12 \
  -export \
  -inkey "${key_pem}" \
  -in "${cert_pem}" \
  -name "${common_name}" \
  -out "${p12_path}" \
  -passout "pass:${p12_password}"

security import "${p12_path}" \
  -k "${keychain}" \
  -P "${p12_password}" \
  -T /usr/bin/codesign \
  -T /usr/bin/security >/dev/null

security add-trusted-cert \
  -r trustRoot \
  -p codeSign \
  -k "${keychain}" \
  "${cert_der}" >/dev/null 2>&1 || true

rm -f "${p12_path}" "${openssl_config}"
chmod 600 "${key_pem}"

echo "Created self-signed code signing identity: ${common_name}"
echo "Public certificate: ${cert_der}"
