#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly release_signing_fingerprint="7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D"
readonly sbom_experimental_value="ca494ed3-b261-4205-a01f-603c95e4cae0"

die() {
    echo "error: $*" >&2
    exit 2
}

log() {
    echo "==> $*"
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

python_command() {
    if command -v python3 >/dev/null 2>&1; then
        command -v python3
    elif command -v python >/dev/null 2>&1; then
        command -v python
    else
        return 1
    fi
}

usage() {
    cat <<'EOF'
Usage: scripts/release.sh <command> [options]

Commands:
  verify-tag --tag <tag> [--trusted-ref <ref>] [--public-key <path>]
      Verify the annotated tag signature, master ancestry, and all project versions.

  build [--package-dir <dir>] [--require-signing]
      Build and validate signed TGZ/ZIP install packages and an SPDX SBOM.

Environment for build:
  VCPKG_ROOT           vcpkg checkout used for manifest dependencies
  CMAKE_TOOLCHAIN_FILE explicit dependency toolchain (overrides VCPKG_ROOT)
  GPG_SIGNING_KEY      imported private-key fingerprint or key id
  GPG_PASSPHRASE_FILE optional passphrase file for noninteractive signing
EOF
}

read_metadata_versions() {
    local metadata_dir="$1"
    local python_bin
    python_bin="$(python_command)" || die "Python is required to read release metadata"

    "${python_bin}" - "${metadata_dir}" <<'PY'
import ast
import json
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])

cmake_text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
project = re.search(r"project\s*\(\s*cbor_tags\b(?P<body>.*?)\)", cmake_text, re.IGNORECASE | re.DOTALL)
if not project:
    raise SystemExit("Could not find cbor_tags project declaration")
version = re.search(r"\bVERSION\s+([^\s\)]+)", project.group("body"), re.IGNORECASE)
if not version:
    raise SystemExit("Could not find cbor_tags project version")
cmake_version = version.group(1)

with (root / "vcpkg.json").open(encoding="utf-8") as stream:
    vcpkg_version = json.load(stream).get("version")
if not isinstance(vcpkg_version, str) or not vcpkg_version:
    raise SystemExit("Could not find vcpkg.json version")

conan_tree = ast.parse((root / "conanfile.py").read_text(encoding="utf-8"), filename="conanfile.py")
conan_version = None
for node in conan_tree.body:
    if not isinstance(node, ast.ClassDef) or node.name != "CborTagsConan":
        continue
    for statement in node.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if isinstance(target, ast.Name) and target.id == "version" and isinstance(statement.value, ast.Constant) and isinstance(statement.value.value, str):
            conan_version = statement.value.value
            break
if not conan_version:
    raise SystemExit("Could not find CborTagsConan.version")

print(f"cmake_version={cmake_version}")
print(f"vcpkg_version={vcpkg_version}")
print(f"conan_version={conan_version}")
PY
}

verify_tag() {
    local tag=""
    local trusted_ref="refs/remotes/origin/master"
    local public_key="${repo_root}/.github/release-signing-key.asc"

    while (($#)); do
        case "$1" in
            --tag)
                [[ $# -ge 2 ]] || die "--tag requires a value"
                tag="$2"
                shift 2
                ;;
            --trusted-ref)
                [[ $# -ge 2 ]] || die "--trusted-ref requires a value"
                trusted_ref="$2"
                shift 2
                ;;
            --public-key)
                [[ $# -ge 2 ]] || die "--public-key requires a value"
                public_key="$2"
                shift 2
                ;;
            -h | --help)
                usage
                return 0
                ;;
            *) die "unknown verify-tag option: $1" ;;
        esac
    done

    [[ -n "${tag}" ]] || die "--tag is required"
    [[ "${tag}" == v* ]] || die "release tag must start with 'v': ${tag}"
    git check-ref-format "refs/tags/${tag}" >/dev/null || die "invalid release tag: ${tag}"

    local tag_ref="refs/tags/${tag}"
    local tag_type
    tag_type="$(git cat-file -t "${tag_ref}" 2>/dev/null || true)"
    [[ "${tag_type}" == "tag" ]] || die "release tag ${tag} must be an annotated tag; got ${tag_type:-missing}"
    git rev-parse --verify --quiet "${trusted_ref}^{commit}" >/dev/null || die "trusted ref does not resolve to a commit: ${trusted_ref}"
    [[ -f "${public_key}" ]] || die "release public key not found: ${public_key}"

    local gnupg_home
    gnupg_home="$(mktemp -d)"
    chmod 700 "${gnupg_home}"
    trap 'rm -rf -- "${gnupg_home}"' RETURN

    GNUPGHOME="${gnupg_home}" gpg --batch --quiet --no-autostart --import "${public_key}"

    local imported_fingerprint
    imported_fingerprint="$(GNUPGHOME="${gnupg_home}" gpg --batch --no-autostart --with-colons --fingerprint | awk -F: '/^fpr:/ { print toupper($10); exit }')"
    [[ "${imported_fingerprint}" == "${release_signing_fingerprint}" ]] ||
        die "release public key fingerprint is ${imported_fingerprint:-missing}, expected ${release_signing_fingerprint}"

    local verification_output
    if ! verification_output="$(GNUPGHOME="${gnupg_home}" git verify-tag --raw "${tag_ref}" 2>&1)"; then
        printf '%s\n' "${verification_output}" >&2
        die "release tag ${tag} does not have a valid signature"
    fi

    local tag_fingerprint
    tag_fingerprint="$(awk '/^\[GNUPG:\] VALIDSIG / { print toupper($3); exit }' <<<"${verification_output}")"
    [[ "${tag_fingerprint}" == "${release_signing_fingerprint}" ]] ||
        die "release tag ${tag} was signed by ${tag_fingerprint:-unknown}, expected ${release_signing_fingerprint}"

    local tag_commit
    tag_commit="$(git rev-list -n 1 "${tag_ref}")"
    git merge-base --is-ancestor "${tag_commit}" "${trusted_ref}" ||
        die "release tag ${tag} commit ${tag_commit} is not contained in ${trusted_ref}"

    local metadata_dir
    metadata_dir="$(mktemp -d)"
    trap 'rm -rf -- "${metadata_dir}" "${gnupg_home}"' RETURN
    git show "${tag_commit}:CMakeLists.txt" >"${metadata_dir}/CMakeLists.txt"
    git show "${tag_commit}:vcpkg.json" >"${metadata_dir}/vcpkg.json"
    git show "${tag_commit}:conanfile.py" >"${metadata_dir}/conanfile.py"

    local cmake_version=""
    local vcpkg_version=""
    local conan_version=""
    local key value
    while IFS='=' read -r key value; do
        case "${key}" in
            cmake_version) cmake_version="${value}" ;;
            vcpkg_version) vcpkg_version="${value}" ;;
            conan_version) conan_version="${value}" ;;
            *) die "unexpected metadata key: ${key}" ;;
        esac
    done < <(read_metadata_versions "${metadata_dir}")

    local tag_version="${tag#v}"
    [[ "${cmake_version}" == "${tag_version}" ]] || die "release tag ${tag} does not match CMake project version ${cmake_version:-missing}"
    [[ "${vcpkg_version}" == "${tag_version}" ]] || die "release tag ${tag} does not match vcpkg.json version ${vcpkg_version:-missing}"
    [[ "${conan_version}" == "${tag_version}" ]] || die "release tag ${tag} does not match conanfile.py version ${conan_version:-missing}"

    printf 'release_tag=%s\n' "${tag}"
    printf 'release_commit=%s\n' "${tag_commit}"
    printf 'release_version=%s\n' "${tag_version}"
}

canonical_build_path() {
    local requested="$1"
    local parent name candidate
    name="$(basename -- "${requested}")"
    parent="$(dirname -- "${requested}")"
    mkdir -p -- "${parent}"
    parent="$(cd -- "${parent}" && pwd -P)"
    candidate="${parent}/${name}"
    case "${candidate}" in
        "${repo_root}/build/"*) printf '%s\n' "${candidate}" ;;
        *) die "generated release paths must be below ${repo_root}/build: ${candidate}" ;;
    esac
}

reset_generated_dir() {
    local path="$1"
    case "${path}" in
        "${repo_root}/build/"*) ;;
        *) die "refusing to reset path outside ${repo_root}/build: ${path}" ;;
    esac
    rm -rf -- "${path}"
    mkdir -p -- "${path}"
}

generate_test_signing_key() {
    local gnupg_home="$1"
    mkdir -p -- "${gnupg_home}"
    chmod 700 "${gnupg_home}"
    cat >"${gnupg_home}/key-parameters" <<'EOF'
Key-Type: EDDSA
Key-Curve: Ed25519
Key-Usage: sign
Name-Real: cbor_tags release test
Name-Email: release-test@cbor-tags.invalid
Expire-Date: 1d
%no-protection
%commit
EOF
    GNUPGHOME="${gnupg_home}" gpg --batch --generate-key "${gnupg_home}/key-parameters"
    GNUPGHOME="${gnupg_home}" gpg --batch --with-colons --list-secret-keys |
        awk -F: '/^fpr:/ { print toupper($10); exit }'
}

validate_sbom() {
    local sbom_file="$1"
    local expected_version="$2"
    local python_bin
    python_bin="$(python_command)" || die "Python is required to validate the release SBOM"

    "${python_bin}" - "${sbom_file}" "${expected_version}" <<'PY'
import json
import sys

path, expected_version = sys.argv[1:]
with open(path, encoding="utf-8") as stream:
    document = json.load(stream)

if document.get("@context") != "https://spdx.org/rdf/3.0.1/spdx-context.jsonld":
    raise SystemExit("release SBOM has an unexpected SPDX context")

spdx_documents = [item for item in document.get("@graph", []) if item.get("type") == "SpdxDocument"]
if len(spdx_documents) != 1:
    raise SystemExit(f"release SBOM must contain exactly one SpdxDocument, found {len(spdx_documents)}")

spdx_document = spdx_documents[0]
if spdx_document.get("name") != "cbor_tags":
    raise SystemExit(f"release SBOM document name is {spdx_document.get('name')!r}, expected 'cbor_tags'")
if spdx_document.get("dataLicense") != "MIT":
    raise SystemExit(f"release SBOM data license is {spdx_document.get('dataLicense')!r}, expected 'MIT'")

roots = [item for item in spdx_document.get("rootElement", []) if item.get("name") == "cbor_tags"]
if len(roots) != 1:
    raise SystemExit(f"release SBOM must contain exactly one cbor_tags root element, found {len(roots)}")
if roots[0].get("software_packageVersion") != expected_version:
    raise SystemExit(
        f"release SBOM version is {roots[0].get('software_packageVersion')!r}, expected {expected_version!r}"
    )
if roots[0].get("software_homePage") != "https://github.com/jkammerland/cbor_tags":
    raise SystemExit("release SBOM has an unexpected homepage")
PY
}

sign_sidecar() {
    local sidecar="$1"
    local package_dir
    local sidecar_name
    package_dir="$(dirname -- "${sidecar}")"
    sidecar_name="$(basename -- "${sidecar}")"

    local gpg_args=(--batch --yes --pinentry-mode loopback --local-user "${GPG_SIGNING_KEY}")
    if [[ -n "${GPG_PASSPHRASE_FILE:-}" ]]; then
        gpg_args+=(--passphrase-file "${GPG_PASSPHRASE_FILE}")
    fi
    gpg "${gpg_args[@]}" --output "${sidecar}.sig" --detach-sign "${sidecar}"
    (
        cd -- "${package_dir}"
        sha256sum "${sidecar_name}" >"${sidecar_name}.sha256"
        sha512sum "${sidecar_name}" >"${sidecar_name}.sha512"
        sha256sum -c "${sidecar_name}.sha256"
        sha512sum -c "${sidecar_name}.sha512"
    )
    gpg --verify "${sidecar}.sig" "${sidecar}" >/dev/null
}

build_release() {
    local package_dir="${repo_root}/build/release-packages"
    local require_signing=false

    while (($#)); do
        case "$1" in
            --package-dir)
                [[ $# -ge 2 ]] || die "--package-dir requires a value"
                package_dir="$2"
                shift 2
                ;;
            --require-signing)
                require_signing=true
                shift
                ;;
            -h | --help)
                usage
                return 0
                ;;
            *) die "unknown build option: $1" ;;
        esac
    done

    require_command cmake
    require_command cpack
    require_command gpg
    require_command ninja
    require_command sha256sum
    require_command sha512sum

    local toolchain_file="${CMAKE_TOOLCHAIN_FILE:-}"
    if [[ -z "${toolchain_file}" && -n "${VCPKG_ROOT:-}" ]]; then
        toolchain_file="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    fi
    [[ -f "${toolchain_file}" ]] || die "CMAKE_TOOLCHAIN_FILE or VCPKG_ROOT must identify a vcpkg toolchain"

    package_dir="$(canonical_build_path "${package_dir}")"
    local work_dir="${repo_root}/build/release"
    local build_dir="${work_dir}/package-build"
    local extract_dir="${work_dir}/extract"
    local consumer_build_dir="${work_dir}/consumer-build"
    reset_generated_dir "${work_dir}"
    reset_generated_dir "${package_dir}"

    local test_gnupg_home=""
    if [[ -z "${GPG_SIGNING_KEY:-}" ]]; then
        [[ "${require_signing}" == false ]] || die "--require-signing requires GPG_SIGNING_KEY to name an imported private key"
        test_gnupg_home="${work_dir}/gnupg"
        export GNUPGHOME="${test_gnupg_home}"
        GPG_SIGNING_KEY="$(generate_test_signing_key "${test_gnupg_home}")"
        export GPG_SIGNING_KEY
    fi
    gpg --batch --list-secret-keys "${GPG_SIGNING_KEY}" >/dev/null || die "GPG_SIGNING_KEY does not select an imported private key"

    local cmake_version=""
    local vcpkg_version=""
    local conan_version=""
    local key value
    while IFS='=' read -r key value; do
        case "${key}" in
            cmake_version) cmake_version="${value}" ;;
            vcpkg_version) vcpkg_version="${value}" ;;
            conan_version) conan_version="${value}" ;;
            *) die "unexpected metadata key: ${key}" ;;
        esac
    done < <(read_metadata_versions "${repo_root}")
    [[ "${cmake_version}" == "${vcpkg_version}" && "${cmake_version}" == "${conan_version}" ]] ||
        die "project versions disagree: CMake=${cmake_version}, vcpkg=${vcpkg_version}, Conan=${conan_version}"

    log "Configure release package"
    cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=20 \
        -DCMAKE_CXX_STANDARD_REQUIRED=ON \
        -DCMAKE_CXX_EXTENSIONS=OFF \
        -DCMAKE_TOOLCHAIN_FILE="${toolchain_file}" \
        -DCMAKE_INSTALL_PREFIX=/ \
        -DCPACK_PACKAGE_DIRECTORY="${package_dir}" \
        -DCPM_SOURCE_CACHE="${repo_root}/build/cpm_cache" \
        -DCBOR_TAGS_BUILD_TESTS=OFF \
        -DCBOR_TAGS_INSTALL=ON \
        -DCBOR_TAGS_USE_SYSTEM_EXPECTED=ON \
        -DCBOR_TAGS_ENABLE_CPACK=ON \
        -DCBOR_TAGS_ENABLE_SBOM=ON \
        -DCBOR_TAGS_REQUIRE_SIGNING=ON \
        -DCMAKE_EXPERIMENTAL_GENERATE_SBOM="${sbom_experimental_value}" \
        -DGPG_SIGNING_KEY="${GPG_SIGNING_KEY}" \
        -DGPG_PASSPHRASE_FILE="${GPG_PASSPHRASE_FILE:-}"

    log "Build and package release install tree"
    cmake --build "${build_dir}" --parallel
    cpack --config "${build_dir}/CPackConfig.cmake" --verbose

    local package_stem="cbor_tags-${cmake_version}-cmake"
    local tgz_package="${package_dir}/${package_stem}.tar.gz"
    local zip_package="${package_dir}/${package_stem}.zip"
    local package_file package_name
    for package_file in "${tgz_package}" "${zip_package}"; do
        package_name="$(basename -- "${package_file}")"
        [[ -f "${package_file}" ]] || die "missing release archive: ${package_name}"
        [[ -f "${package_file}.sig" ]] || die "missing detached signature for ${package_name}"
        [[ -f "${package_file}.sha256" ]] || die "missing SHA256 checksum for ${package_name}"
        [[ -f "${package_file}.sha512" ]] || die "missing SHA512 checksum for ${package_name}"
        gpg --verify "${package_file}.sig" "${package_file}" >/dev/null
        (
            cd -- "${package_dir}"
            sha256sum -c "${package_name}.sha256"
            sha512sum -c "${package_name}.sha512"
        )
    done

    log "Compare TGZ and ZIP contents"
    cmake -E tar tf "${tgz_package}" | sort >"${work_dir}/tgz-files.txt"
    cmake -E tar tf "${zip_package}" | sort >"${work_dir}/zip-files.txt"
    diff -u "${work_dir}/tgz-files.txt" "${work_dir}/zip-files.txt"

    mkdir -p -- "${extract_dir}"
    (
        cd -- "${extract_dir}"
        cmake -E tar xzf "${tgz_package}"
    )
    local archive_root="${extract_dir}/${package_stem}"
    [[ -d "${archive_root}" ]] || archive_root="${extract_dir}"

    local extracted_prefix=""
    if [[ -f "${archive_root}/share/cmake/cbor_tags/cbor_tagsConfig.cmake" ]]; then
        extracted_prefix="${archive_root}"
    elif [[ -f "${archive_root}/usr/share/cmake/cbor_tags/cbor_tagsConfig.cmake" ]]; then
        extracted_prefix="${archive_root}/usr"
    else
        die "release archive is missing cbor_tagsConfig.cmake"
    fi
    [[ -f "${extracted_prefix}/share/doc/cbor_tags/LICENSE" ]] || die "release archive is missing LICENSE"

    shopt -s nullglob
    local packaged_sbom_files=("${extracted_prefix}/share/sbom/cbor_tags"/*.spdx.json)
    shopt -u nullglob
    (( ${#packaged_sbom_files[@]} == 1 )) || die "expected one packaged SPDX SBOM, found ${#packaged_sbom_files[@]}"

    local sbom_sidecar="${package_dir}/${package_stem}.spdx.json"
    cmake -E copy "${packaged_sbom_files[0]}" "${sbom_sidecar}"
    validate_sbom "${sbom_sidecar}" "${cmake_version}"
    sign_sidecar "${sbom_sidecar}"

    local public_key_file="${package_dir}/cbor_tags-release-public-key.asc"
    gpg --armor --export "${GPG_SIGNING_KEY}" >"${public_key_file}"
    [[ -s "${public_key_file}" ]] || die "failed to export release public key"

    local dependency_prefix="${build_dir}/vcpkg_installed/${VCPKG_DEFAULT_TRIPLET:-x64-linux}"
    [[ -d "${dependency_prefix}" ]] || die "release dependency prefix not found: ${dependency_prefix}"

    log "Build installed-package consumer from extracted archive"
    cmake -S "${repo_root}/test_package" -B "${consumer_build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=20 \
        -DCMAKE_PREFIX_PATH="${extracted_prefix};${dependency_prefix}"
    cmake --build "${consumer_build_dir}" --parallel
    "${consumer_build_dir}/test_package"

    log "Release artifacts validated"
    find "${package_dir}" -maxdepth 1 -type f -printf '%f\n' | sort
}

command_name="${1:-}"
case "${command_name}" in
    verify-tag)
        shift
        verify_tag "$@"
        ;;
    build)
        shift
        build_release "$@"
        ;;
    "" | -h | --help | help)
        usage
        ;;
    *)
        usage >&2
        die "unknown release command: ${command_name}"
        ;;
esac
