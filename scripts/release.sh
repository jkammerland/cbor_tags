#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly release_signing_fingerprint="7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D"
readonly release_cmake_series="4.4"
readonly sbom_experimental_value="2d856d6d-53e8-488b-a17f-d486d2cac317"
readonly spdx_schema_url="https://spdx.org/schema/3.0.1/spdx-json-schema.json"
readonly spdx_schema_sha256="582c64e809d5b3ef9bd0c4de13a32391b47b0284a3e8d199569fb96f649234b1"

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
             [--expected-fingerprint <fingerprint>]
      Verify the annotated tag signature, master ancestry, and all project versions.

  build [--package-dir <dir>]
      Build and validate unsigned TGZ/ZIP install packages and an SPDX SBOM.

  sign [--package-dir <dir>] [--require-release-key]
      Sign validated release payloads and export the public verification key.

  verify-assets [--package-dir <dir>] [--version <version>]
      Verify the exact signed release asset set, checksums, key, and signatures.

Environment:
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
    local expected_fingerprint="${release_signing_fingerprint}"

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
            --expected-fingerprint)
                [[ $# -ge 2 ]] || die "--expected-fingerprint requires a value"
                expected_fingerprint="${2^^}"
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
    [[ "${imported_fingerprint}" == "${expected_fingerprint}" ]] ||
        die "release public key fingerprint is ${imported_fingerprint:-missing}, expected ${expected_fingerprint}"

    local verification_output
    if ! verification_output="$(GNUPGHOME="${gnupg_home}" git verify-tag --raw "${tag_ref}" 2>&1)"; then
        printf '%s\n' "${verification_output}" >&2
        die "release tag ${tag} does not have a valid signature"
    fi

    local tag_fingerprint
    tag_fingerprint="$(awk '/^\[GNUPG:\] VALIDSIG / { print toupper($3); exit }' <<<"${verification_output}")"
    [[ "${tag_fingerprint}" == "${expected_fingerprint}" ]] ||
        die "release tag ${tag} was signed by ${tag_fingerprint:-unknown}, expected ${expected_fingerprint}"

    local tag_commit
    tag_commit="$(git rev-list -n 1 "${tag_ref}")"
    local tag_object
    tag_object="$(git rev-parse "${tag_ref}")"
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
    printf 'release_tag_object=%s\n' "${tag_object}"
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
profiles = set(spdx_document.get("profileConformance", []))
expected_profiles = {"core", "software", "simpleLicensing"}
if not expected_profiles.issubset(profiles):
    raise SystemExit(f"release SBOM profiles are {sorted(profiles)!r}, expected at least {sorted(expected_profiles)!r}")

roots = [item for item in spdx_document.get("rootElement", []) if item.get("name") == "cbor_tags"]
if len(roots) != 1:
    raise SystemExit(f"release SBOM must contain exactly one cbor_tags root element, found {len(roots)}")
root = roots[0]
if root.get("type") != "software_Package":
    raise SystemExit(f"release SBOM root type is {root.get('type')!r}, expected 'software_Package'")
if root.get("software_packageVersion") != expected_version:
    raise SystemExit(
        f"release SBOM version is {root.get('software_packageVersion')!r}, expected {expected_version!r}"
    )
if root.get("software_homePage") != "https://github.com/jkammerland/cbor_tags":
    raise SystemExit("release SBOM has an unexpected homepage")
if root.get("software_primaryPurpose") != "library":
    raise SystemExit(f"release SBOM primary purpose is {root.get('software_primaryPurpose')!r}, expected 'library'")

elements = spdx_document.get("element", [])
license_expressions = {
    item.get("spdxId"): item.get("simplelicensing_licenseExpression")
    for item in elements
    if item.get("type") == "simplelicensing_LicenseExpression"
}
data_license_id = spdx_document.get("dataLicense")
if license_expressions.get(data_license_id) != "CC0-1.0":
    raise SystemExit("release SBOM data license must resolve to CC0-1.0")
package_license_ids = [identifier for identifier, expression in license_expressions.items() if expression == "MIT"]
if len(package_license_ids) != 1:
    raise SystemExit(f"release SBOM must contain exactly one MIT package license expression, found {len(package_license_ids)}")
package_license_id = package_license_ids[0]

expected_dependencies = {
    "tl-expected:tl::expected": "urn:tl-expected:tl::expected#Package",
    "fmt:fmt": "urn:fmt:fmt#Package",
    "nameof:nameof": "urn:nameof:nameof#Package",
}
dependencies = {item.get("name"): item for item in elements if item.get("name") in expected_dependencies}
if set(dependencies) != set(expected_dependencies):
    raise SystemExit(
        f"release SBOM dependency names are {sorted(dependencies)!r}, expected {sorted(expected_dependencies)!r}"
    )
for name, expected_id in expected_dependencies.items():
    dependency = dependencies[name]
    if dependency.get("type") != "software_Package":
        raise SystemExit(f"release SBOM dependency {name} has unexpected type {dependency.get('type')!r}")
    if dependency.get("spdxId") != expected_id:
        raise SystemExit(f"release SBOM dependency {name} has unexpected SPDX id {dependency.get('spdxId')!r}")
    if not dependency.get("software_packageVersion"):
        raise SystemExit(f"release SBOM dependency {name} has no version")

expected_dependency_ids = set(expected_dependencies.values())
relationships = [
    item
    for item in document.get("@graph", [])
    if item.get("type") == "Relationship"
    and item.get("from") == root.get("spdxId")
    and item.get("relationshipType") == "dependsOn"
]
if len(relationships) != 1:
    raise SystemExit(f"release SBOM must contain one root dependsOn relationship, found {len(relationships)}")
actual_dependency_ids = set(relationships[0].get("to", []))
if actual_dependency_ids != expected_dependency_ids:
    raise SystemExit(
        f"release SBOM dependency relationship is {sorted(actual_dependency_ids)!r}, "
        f"expected {sorted(expected_dependency_ids)!r}"
    )

for relationship_type in ("hasDeclaredLicense", "hasConcludedLicense"):
    license_relationships = [
        item
        for item in document.get("@graph", [])
        if item.get("type") == "Relationship"
        and item.get("from") == root.get("spdxId")
        and item.get("relationshipType") == relationship_type
    ]
    if len(license_relationships) != 1 or license_relationships[0].get("to") != [package_license_id]:
        raise SystemExit(f"release SBOM must contain one {relationship_type} relationship to the MIT license")
PY
}

validate_sbom_schema() {
    local sbom_file="$1"
    local schema_file="$2"
    local python_bin
    python_bin="$(python_command)" || die "Python is required to validate the SPDX schema"
    curl --fail --silent --show-error --location "${spdx_schema_url}" --output "${schema_file}"
    printf '%s  %s\n' "${spdx_schema_sha256}" "${schema_file}" | sha256sum -c - >/dev/null
    "${python_bin}" - "${schema_file}" "${sbom_file}" <<'PY'
import json
import sys

try:
    from jsonschema import Draft202012Validator
except ImportError as error:
    raise SystemExit("Python package jsonschema is required for SPDX schema validation") from error

schema_path, document_path = sys.argv[1:]
with open(schema_path, encoding="utf-8") as stream:
    schema = json.load(stream)
with open(document_path, encoding="utf-8") as stream:
    document = json.load(stream)
errors = sorted(Draft202012Validator(schema).iter_errors(document), key=lambda item: list(item.absolute_path))
if errors:
    details = "\n".join(f"/{'/'.join(map(str, error.absolute_path))}: {error.message}" for error in errors[:20])
    raise SystemExit(f"release SBOM failed SPDX 3.0.1 JSON Schema validation:\n{details}")
PY
}

metadata_version() {
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
    printf '%s\n' "${cmake_version}"
}

expected_unsigned_assets() {
    local version="$1"
    local stem="cbor_tags-${version}"
    printf '%s\n' \
        "${stem}.spdx.json" \
        "${stem}.spdx.json.sha256" \
        "${stem}.spdx.json.sha512" \
        "${stem}.tar.gz" \
        "${stem}.tar.gz.sha256" \
        "${stem}.tar.gz.sha512" \
        "${stem}.zip" \
        "${stem}.zip.sha256" \
        "${stem}.zip.sha512"
}

expected_signed_assets() {
    local version="$1"
    local stem="cbor_tags-${version}"
    expected_unsigned_assets "${version}"
    printf '%s\n' \
        "${stem}.spdx.json.sig" \
        "${stem}.tar.gz.sig" \
        "${stem}.zip.sig" \
        "cbor_tags-release-public-key.asc"
}

require_exact_assets() {
    local package_dir="$1"
    local version="$2"
    local asset_kind="$3"
    local expected_assets actual_assets

    case "${asset_kind}" in
        unsigned) expected_assets="$(expected_unsigned_assets "${version}" | sort)" ;;
        signed) expected_assets="$(expected_signed_assets "${version}" | sort)" ;;
        *) die "unknown asset set: ${asset_kind}" ;;
    esac
    actual_assets="$(find "${package_dir}" -maxdepth 1 -type f -printf '%f\n' | sort)"
    if [[ "${expected_assets}" != "${actual_assets}" ]]; then
        diff -u <(printf '%s\n' "${expected_assets}") <(printf '%s\n' "${actual_assets}") || true
        die "${package_dir} does not contain the exact ${asset_kind} release asset set"
    fi
}

write_checksums() {
    local payload="$1"
    local package_dir payload_name
    package_dir="$(dirname -- "${payload}")"
    payload_name="$(basename -- "${payload}")"
    (
        cd -- "${package_dir}"
        sha256sum "${payload_name}" >"${payload_name}.sha256"
        sha512sum "${payload_name}" >"${payload_name}.sha512"
    )
}

verify_checksums() {
    local payload="$1"
    local package_dir payload_name
    package_dir="$(dirname -- "${payload}")"
    payload_name="$(basename -- "${payload}")"
    [[ -f "${payload}.sha256" ]] || die "missing SHA-256 checksum for ${payload_name}"
    [[ -f "${payload}.sha512" ]] || die "missing SHA-512 checksum for ${payload_name}"
    (
        cd -- "${package_dir}"
        sha256sum -c "${payload_name}.sha256"
        sha512sum -c "${payload_name}.sha512"
    )
}

sign_payload() {
    local payload="$1"
    local gpg_args=(--batch --yes --pinentry-mode loopback --local-user "${GPG_SIGNING_KEY}")
    if [[ -n "${GPG_PASSPHRASE_FILE:-}" ]]; then
        gpg_args+=(--passphrase-file "${GPG_PASSPHRASE_FILE}")
    fi
    gpg "${gpg_args[@]}" --output "${payload}.sig" --detach-sign "${payload}"
}

verify_signed_assets() {
    local package_dir="$1"
    local version="$2"
    local expected_fingerprint="$3"
    local canonical_public_key="${4:-}"
    require_exact_assets "${package_dir}" "${version}" signed

    (
        local gnupg_home
        gnupg_home="$(mktemp -d)"
        chmod 700 "${gnupg_home}"
        trap 'rm -rf -- "${gnupg_home}"' EXIT
        if [[ -n "${canonical_public_key}" ]]; then
            [[ -f "${canonical_public_key}" ]] || die "canonical release public key not found: ${canonical_public_key}"
            cmp "${canonical_public_key}" "${package_dir}/cbor_tags-release-public-key.asc" ||
                die "release asset public key differs from the canonical committed key"
        fi
        GNUPGHOME="${gnupg_home}" gpg --batch --quiet --no-autostart --import "${package_dir}/cbor_tags-release-public-key.asc"

        local imported_fingerprints=()
        mapfile -t imported_fingerprints < <(
            GNUPGHOME="${gnupg_home}" gpg --batch --no-autostart --with-colons --fingerprint |
                awk -F: '
                    /^pub:/ { want_fingerprint = 1; next }
                    want_fingerprint && /^fpr:/ { print toupper($10); want_fingerprint = 0 }
                '
        )
        [[ ${#imported_fingerprints[@]} -eq 1 ]] ||
            die "release asset public key must contain exactly one primary key, found ${#imported_fingerprints[@]}"
        [[ "${imported_fingerprints[0]}" == "${expected_fingerprint}" ]] ||
            die "release asset public key fingerprint is ${imported_fingerprints[0]:-missing}, expected ${expected_fingerprint}"

        local stem="cbor_tags-${version}"
        local payload verification_output signature_fingerprints
        for payload in "${stem}.tar.gz" "${stem}.zip" "${stem}.spdx.json"; do
            verify_checksums "${package_dir}/${payload}"
            if ! verification_output="$(
                GNUPGHOME="${gnupg_home}" gpg --batch --no-autostart --status-fd 1 --verify \
                    "${package_dir}/${payload}.sig" "${package_dir}/${payload}" 2>&1
            )"; then
                printf '%s\n' "${verification_output}" >&2
                die "release asset ${payload} does not have a valid signature"
            fi
            mapfile -t signature_fingerprints < <(
                awk '/^\[GNUPG:\] VALIDSIG / { print toupper($3) }' <<<"${verification_output}"
            )
            [[ ${#signature_fingerprints[@]} -eq 1 ]] ||
                die "release asset ${payload} must have exactly one valid signature, found ${#signature_fingerprints[@]}"
            [[ "${signature_fingerprints[0]}" == "${expected_fingerprint}" ]] ||
                die "release asset ${payload} was signed by ${signature_fingerprints[0]:-unknown}, expected ${expected_fingerprint}"
        done
    )
}

locate_package_prefix() {
    local extract_dir="$1"
    local package_stem="$2"
    local archive_root="${extract_dir}/${package_stem}"
    [[ -d "${archive_root}" ]] || archive_root="${extract_dir}"
    if [[ -f "${archive_root}/share/cmake/cbor_tags/cbor_tagsConfig.cmake" ]]; then
        printf '%s\n' "${archive_root}"
    elif [[ -f "${archive_root}/usr/share/cmake/cbor_tags/cbor_tagsConfig.cmake" ]]; then
        printf '%s\n' "${archive_root}/usr"
    else
        die "release archive is missing cbor_tagsConfig.cmake"
    fi
}

write_tree_manifest() {
    local root="$1"
    local output="$2"
    local python_bin
    python_bin="$(python_command)" || die "Python is required to compare release archives"
    "${python_bin}" - "${root}" >"${output}" <<'PY'
import hashlib
import pathlib
import stat
import sys

root = pathlib.Path(sys.argv[1])
for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
    relative = path.relative_to(root).as_posix()
    if path.is_symlink():
        print(f"L\t{relative}\t{path.readlink()}")
    elif path.is_dir():
        print(f"D\t{relative}")
    elif path.is_file():
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        executable = bool(stat.S_IMODE(path.stat().st_mode) & 0o111)
        print(f"F\t{relative}\t{int(executable)}\t{digest}")
    else:
        raise SystemExit(f"unsupported archive entry: {relative}")
PY
}

packaged_sbom() {
    local prefix="$1"
    local sbom_files=()
    shopt -s nullglob
    sbom_files=("${prefix}/share/sbom/cbor_tags"/*.spdx.json)
    shopt -u nullglob
    (( ${#sbom_files[@]} == 1 )) || die "expected one packaged SPDX SBOM, found ${#sbom_files[@]}"
    printf '%s\n' "${sbom_files[0]}"
}

run_installed_consumer() {
    local prefix="$1"
    local dependency_prefix="$2"
    local consumer_build_dir="$3"
    cmake -S "${repo_root}/test_package" -B "${consumer_build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=20 \
        -DCMAKE_PREFIX_PATH="${prefix};${dependency_prefix}"
    cmake --build "${consumer_build_dir}" --parallel
    "${consumer_build_dir}/test_package"
}

require_release_cmake() {
    local installed_version
    installed_version="$(cmake --version | awk 'NR == 1 { print $3 }')"
    [[ "${installed_version}" == "${release_cmake_series}."* ]] ||
        die "release packaging requires CMake ${release_cmake_series}.x, found ${installed_version:-unknown}"
}

normalize_sbom_timestamps() {
    local sbom_file="$1"
    local source_epoch="$2"
    local python_bin
    python_bin="$(python_command)" || die "Python is required to normalize release SBOM timestamps"
    "${python_bin}" - "${sbom_file}" "${source_epoch}" <<'PY'
import datetime
import json
import sys

path, source_epoch = sys.argv[1:]
timestamp = datetime.datetime.fromtimestamp(int(source_epoch), datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
with open(path, encoding="utf-8") as stream:
    document = json.load(stream)

creation_info = next(item for item in document["@graph"] if item.get("type") == "CreationInfo")
spdx_document = next(item for item in document["@graph"] if item.get("type") == "SpdxDocument")
root = next(item for item in spdx_document["rootElement"] if item.get("name") == "cbor_tags")
creation_info_id = creation_info["@id"]

data_license_id = "urn:cbor_tags#DataLicenseExpression"
package_license_id = "urn:cbor_tags#PackageLicenseExpression"
license_elements = [
    {
        "creationInfo": creation_info_id,
        "simplelicensing_licenseExpression": "CC0-1.0",
        "spdxId": data_license_id,
        "type": "simplelicensing_LicenseExpression",
    },
    {
        "creationInfo": creation_info_id,
        "simplelicensing_licenseExpression": "MIT",
        "spdxId": package_license_id,
        "type": "simplelicensing_LicenseExpression",
    },
]
spdx_document["dataLicense"] = data_license_id
spdx_document["profileConformance"] = sorted(set(spdx_document.get("profileConformance", [])) | {"simpleLicensing"})
spdx_document.setdefault("element", []).extend(license_elements)
for relationship_type in ("hasDeclaredLicense", "hasConcludedLicense"):
    document["@graph"].append(
        {
            "creationInfo": creation_info_id,
            "from": root["spdxId"],
            "relationshipType": relationship_type,
            "spdxId": f"urn:cbor_tags#{relationship_type}Relationship",
            "to": [package_license_id],
            "type": "Relationship",
        }
    )

def normalize(value):
    if isinstance(value, dict):
        for key, item in value.items():
            if key in {"created", "builtTime"}:
                value[key] = timestamp
            else:
                normalize(item)
    elif isinstance(value, list):
        for item in value:
            normalize(item)

normalize(document)
with open(path, "w", encoding="utf-8") as stream:
    json.dump(document, stream, indent=2, ensure_ascii=True)
    stream.write("\n")
PY
}

create_reproducible_archives() {
    local package_stem="$1"
    local canonical_dir="$2"
    local tgz_package="$3"
    local zip_package="$4"
    local source_epoch="$5"

    local canonical_root="${canonical_dir}/${package_stem}"
    [[ -d "${canonical_root}" ]] || die "canonical package root not found: ${canonical_root}"
    local canonical_prefix canonical_sbom
    canonical_prefix="$(locate_package_prefix "${canonical_dir}" "${package_stem}")"
    canonical_sbom="$(packaged_sbom "${canonical_prefix}")"
    normalize_sbom_timestamps "${canonical_sbom}" "${source_epoch}"
    validate_sbom_schema "${canonical_sbom}" "${canonical_dir}/spdx-3.0.1.schema.json"

    find "${canonical_root}" -exec touch -h -d "@${source_epoch}" {} +
    rm -f -- \
        "${tgz_package}" "${tgz_package}.sha256" "${tgz_package}.sha512" \
        "${zip_package}" "${zip_package}.sha256" "${zip_package}.sha512"

    tar \
        --sort=name \
        --mtime="@${source_epoch}" \
        --owner=0 \
        --group=0 \
        --numeric-owner \
        --format=posix \
        --pax-option=delete=atime,delete=ctime \
        -czf "${tgz_package}" \
        -C "${canonical_dir}" \
        "${package_stem}"
    bash "${repo_root}/scripts/create-reproducible-zip.sh" \
        "${canonical_dir}" \
        "${package_stem}" \
        "${zip_package}"
    write_checksums "${tgz_package}"
    write_checksums "${zip_package}"
}

build_release() {
    local package_dir="${repo_root}/build/release-packages"

    while (($#)); do
        case "$1" in
            --package-dir)
                [[ $# -ge 2 ]] || die "--package-dir requires a value"
                package_dir="$2"
                shift 2
                ;;
            -h | --help)
                usage
                return 0
                ;;
            *) die "unknown build option: $1" ;;
        esac
    done

    require_command cmake
    require_command cmp
    require_command cpack
    require_command curl
    require_command diff
    require_command gpg
    require_command ninja
    require_command sha256sum
    require_command sha512sum
    require_command tar
    require_command touch
    require_command zip
    require_release_cmake

    log "Verify signed target_install_package.cmake release dependency"
    bash "${repo_root}/scripts/verify-target-install-package-release.sh"

    local toolchain_file="${CMAKE_TOOLCHAIN_FILE:-}"
    if [[ -z "${toolchain_file}" && -n "${VCPKG_ROOT:-}" ]]; then
        toolchain_file="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    fi
    [[ -f "${toolchain_file}" ]] || die "CMAKE_TOOLCHAIN_FILE or VCPKG_ROOT must identify a vcpkg toolchain"

    package_dir="$(canonical_build_path "${package_dir}")"
    local work_dir="${repo_root}/build/release"
    local build_dir="${work_dir}/package-build"
    local canonical_dir="${work_dir}/canonical"
    local tgz_extract_dir="${work_dir}/extract-tgz"
    local zip_extract_dir="${work_dir}/extract-zip"
    reset_generated_dir "${work_dir}"
    reset_generated_dir "${package_dir}"

    local cmake_version
    cmake_version="$(metadata_version)"
    if [[ -z "${SOURCE_DATE_EPOCH:-}" ]]; then
        SOURCE_DATE_EPOCH="$(git -C "${repo_root}" log -1 --format=%ct HEAD)"
        export SOURCE_DATE_EPOCH
    fi

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
        -DCMAKE_EXPERIMENTAL_GENERATE_SBOM="${sbom_experimental_value}"

    log "Build and package release install tree"
    cmake --build "${build_dir}" --parallel
    cpack --config "${build_dir}/CPackConfig.cmake" --verbose

    local package_stem="cbor_tags-${cmake_version}"
    local tgz_package="${package_dir}/${package_stem}.tar.gz"
    local zip_package="${package_dir}/${package_stem}.zip"
    mkdir -p -- "${canonical_dir}"
    (
        cd -- "${canonical_dir}"
        cmake -E tar xzf "${tgz_package}"
    )
    create_reproducible_archives \
        "${package_stem}" \
        "${canonical_dir}" \
        "${tgz_package}" \
        "${zip_package}" \
        "${SOURCE_DATE_EPOCH}"

    local package_file package_name
    for package_file in "${tgz_package}" "${zip_package}"; do
        package_name="$(basename -- "${package_file}")"
        [[ -f "${package_file}" ]] || die "missing release archive: ${package_name}"
        [[ -f "${package_file}.sha256" ]] || die "missing SHA256 checksum for ${package_name}"
        [[ -f "${package_file}.sha512" ]] || die "missing SHA512 checksum for ${package_name}"
        verify_checksums "${package_file}"
    done

    log "Extract and compare TGZ and ZIP contents"
    mkdir -p -- "${tgz_extract_dir}" "${zip_extract_dir}"
    (
        cd -- "${tgz_extract_dir}"
        cmake -E tar xzf "${tgz_package}"
    )
    (
        cd -- "${zip_extract_dir}"
        cmake -E tar xf "${zip_package}"
    )
    local tgz_prefix zip_prefix
    tgz_prefix="$(locate_package_prefix "${tgz_extract_dir}" "${package_stem}")"
    zip_prefix="$(locate_package_prefix "${zip_extract_dir}" "${package_stem}")"
    [[ -f "${tgz_prefix}/share/doc/cbor_tags/LICENSE" ]] || die "TGZ release archive is missing LICENSE"
    [[ -f "${zip_prefix}/share/doc/cbor_tags/LICENSE" ]] || die "ZIP release archive is missing LICENSE"
    write_tree_manifest "${tgz_prefix}" "${work_dir}/tgz-manifest.txt"
    write_tree_manifest "${zip_prefix}" "${work_dir}/zip-manifest.txt"
    diff -u "${work_dir}/tgz-manifest.txt" "${work_dir}/zip-manifest.txt"

    local tgz_sbom zip_sbom
    tgz_sbom="$(packaged_sbom "${tgz_prefix}")"
    zip_sbom="$(packaged_sbom "${zip_prefix}")"
    cmp "${tgz_sbom}" "${zip_sbom}"

    local sbom_sidecar="${package_dir}/${package_stem}.spdx.json"
    cmake -E copy "${tgz_sbom}" "${sbom_sidecar}"
    validate_sbom "${sbom_sidecar}" "${cmake_version}"
    write_checksums "${sbom_sidecar}"
    verify_checksums "${sbom_sidecar}"
    require_exact_assets "${package_dir}" "${cmake_version}" unsigned

    local dependency_prefix="${build_dir}/vcpkg_installed/${VCPKG_DEFAULT_TRIPLET:-x64-linux}"
    [[ -d "${dependency_prefix}" ]] || die "release dependency prefix not found: ${dependency_prefix}"

    log "Build installed-package consumers from both archives"
    run_installed_consumer "${tgz_prefix}" "${dependency_prefix}" "${work_dir}/consumer-tgz"
    run_installed_consumer "${zip_prefix}" "${dependency_prefix}" "${work_dir}/consumer-zip"

    log "Unsigned release artifacts validated"
    find "${package_dir}" -maxdepth 1 -type f -printf '%f\n' | sort
}

sign_release() {
    local package_dir="${repo_root}/build/release-packages"
    local require_release_key=false
    while (($#)); do
        case "$1" in
            --package-dir)
                [[ $# -ge 2 ]] || die "--package-dir requires a value"
                package_dir="$2"
                shift 2
                ;;
            --require-release-key)
                require_release_key=true
                shift
                ;;
            -h | --help)
                usage
                return 0
                ;;
            *) die "unknown sign option: $1" ;;
        esac
    done

    require_command gpg
    require_command sha256sum
    require_command sha512sum
    package_dir="$(canonical_build_path "${package_dir}")"
    local version
    version="$(metadata_version)"
    require_exact_assets "${package_dir}" "${version}" unsigned

    local test_gnupg_home=""
    if [[ -z "${GPG_SIGNING_KEY:-}" ]]; then
        [[ "${require_release_key}" == false ]] || die "--require-release-key requires GPG_SIGNING_KEY"
        test_gnupg_home="${repo_root}/build/release-signing-test"
        reset_generated_dir "${test_gnupg_home}"
        export GNUPGHOME="${test_gnupg_home}"
        GPG_SIGNING_KEY="$(generate_test_signing_key "${test_gnupg_home}")"
        export GPG_SIGNING_KEY
    fi

    local signing_fingerprint
    signing_fingerprint="$(gpg --batch --with-colons --fingerprint --list-secret-keys "${GPG_SIGNING_KEY}" | awk -F: '/^fpr:/ { print toupper($10); exit }')"
    [[ -n "${signing_fingerprint}" ]] || die "GPG_SIGNING_KEY does not select an imported private key"
    if [[ "${require_release_key}" == true && "${signing_fingerprint}" != "${release_signing_fingerprint}" ]]; then
        die "release signing key is ${signing_fingerprint}, expected ${release_signing_fingerprint}"
    fi

    local stem="cbor_tags-${version}"
    local payload
    for payload in "${stem}.tar.gz" "${stem}.zip" "${stem}.spdx.json"; do
        verify_checksums "${package_dir}/${payload}"
        sign_payload "${package_dir}/${payload}"
    done
    gpg --armor --export "${signing_fingerprint}" >"${package_dir}/cbor_tags-release-public-key.asc"
    [[ -s "${package_dir}/cbor_tags-release-public-key.asc" ]] || die "failed to export release public key"
    verify_signed_assets "${package_dir}" "${version}" "${signing_fingerprint}"
    log "Signed release assets validated"
}

verify_assets() {
    local package_dir="${repo_root}/build/release-packages"
    local version=""
    while (($#)); do
        case "$1" in
            --package-dir)
                [[ $# -ge 2 ]] || die "--package-dir requires a value"
                package_dir="$2"
                shift 2
                ;;
            --version)
                [[ $# -ge 2 ]] || die "--version requires a value"
                version="$2"
                shift 2
                ;;
            -h | --help)
                usage
                return 0
                ;;
            *) die "unknown verify-assets option: $1" ;;
        esac
    done
    require_command gpg
    require_command cmp
    require_command sha256sum
    require_command sha512sum
    package_dir="$(canonical_build_path "${package_dir}")"
    [[ -n "${version}" ]] || version="$(metadata_version)"
    verify_signed_assets \
        "${package_dir}" \
        "${version}" \
        "${release_signing_fingerprint}" \
        "${repo_root}/.github/release-signing-key.asc"
    log "Published release assets validated"
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
    sign)
        shift
        sign_release "$@"
        ;;
    verify-assets)
        shift
        verify_assets "$@"
        ;;
    "" | -h | --help | help)
        usage
        ;;
    *)
        usage >&2
        die "unknown release command: ${command_name}"
        ;;
esac
