#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <llvm-version> <package> [<package> ...]" >&2
  exit 2
fi

readonly llvm_version="$1"
shift

if [[ ! "${llvm_version}" =~ ^[0-9]+$ ]]; then
  echo "LLVM version must be numeric: ${llvm_version}" >&2
  exit 2
fi

# The workflows using this helper are pinned to Ubuntu 24.04 so every tested
# LLVM release has a stable, explicit apt.llvm.org suite.
source /etc/os-release
if [[ "${ID:-}" != "ubuntu" || "${VERSION_CODENAME:-}" != "noble" ]]; then
  echo "unsupported runner: expected Ubuntu 24.04 (noble)" >&2
  exit 1
fi

readonly key_fingerprint="6084F3CF814B57C1CF12EFD515CF4D18AF4F7421"
readonly key_url="https://apt.llvm.org/llvm-snapshot.gpg.key"
readonly repository_url="https://apt.llvm.org/${VERSION_CODENAME}/"
readonly repository_suite="llvm-toolchain-${VERSION_CODENAME}-${llvm_version}"
readonly keyring_path="/usr/share/keyrings/apt.llvm.org.gpg"
readonly source_path="/etc/apt/sources.list.d/apt-llvm-${llvm_version}.list"

temporary_directory="$(mktemp -d)"
readonly temporary_directory
trap 'rm -rf -- "${temporary_directory}"' EXIT

readonly downloaded_key="${temporary_directory}/apt.llvm.org.asc"
readonly verified_keyring="${temporary_directory}/apt.llvm.org.gpg"
readonly gnupg_home="${temporary_directory}/gnupg"
mkdir -m 0700 "${gnupg_home}"

sudo env DEBIAN_FRONTEND=noninteractive apt-get update
sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates curl gnupg

curl --fail --location --silent --show-error \
  "${key_url}" \
  --output "${downloaded_key}"

mapfile -t actual_fingerprints < <(
  gpg --batch --homedir "${gnupg_home}" --show-keys --with-colons \
    "${downloaded_key}" |
    awk -F: '
      $1 == "pub" { primary = 1; next }
      primary && $1 == "fpr" { print $10; primary = 0 }
    '
)
readonly actual_fingerprints

if [[ "${#actual_fingerprints[@]}" -ne 1 || "${actual_fingerprints[0]:-}" != "${key_fingerprint}" ]]; then
  echo "apt.llvm.org signing-key fingerprint mismatch" >&2
  echo "expected: ${key_fingerprint}" >&2
  echo "actual:   ${actual_fingerprints[*]:-<missing>}" >&2
  exit 1
fi

gpg --batch --yes --homedir "${gnupg_home}" --dearmor \
  --output "${verified_keyring}" \
  "${downloaded_key}"
sudo install -m 0644 "${verified_keyring}" "${keyring_path}"

printf 'deb [signed-by=%s] %s %s main\n' \
  "${keyring_path}" \
  "${repository_url}" \
  "${repository_suite}" |
  sudo tee "${source_path}" >/dev/null

sudo env DEBIAN_FRONTEND=noninteractive apt-get update
sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "$@"
