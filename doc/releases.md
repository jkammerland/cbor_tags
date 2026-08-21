# Releases

The `Tagged Release` workflow is the only supported publisher for new GitHub
releases. It accepts an existing signed tag, validates it without access to
release secrets, builds and validates unsigned assets, waits for approval on
the protected `release` environment, and then signs and publishes an immutable
GitHub release.

Existing releases through `v0.22.0` predate this workflow. They use lightweight
tags and GitHub-generated source archives; they are not retroactively converted.

## Release Assets

For version `X.Y.Z`, the workflow publishes exactly 13 assets:

- `cbor_tags-X.Y.Z-cmake.tar.gz` and `.zip` CMake install trees;
- detached `.sig`, `.sha256`, and `.sha512` files for both archives;
- `cbor_tags-X.Y.Z-cmake.spdx.json`, with its detached signature and checksums;
- `cbor_tags-release-public-key.asc` for offline signature verification.

The archives contain the default C++20 package configuration. Consumers still
provide fmt, nameof, and tl::expected; these dependencies are declared by the
installed CMake package. The workflow extracts both archives, compares every
file digest and symlink target, and builds `test_package` against each one
before publication.

The SBOM is SPDX 3 JSON-LD generated from the installed target. The release
driver checks its document name, CC0-1.0 data license, MIT project license,
root package, direct dependency packages, dependency versions, and `dependsOn`
relationship before signing it as a standalone asset.

## Trust Model

The release workflow enforces all of these conditions:

- it is dispatched from the repository default branch;
- the input is an annotated tag signed by exactly
  `7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D`;
- the tagged commit is contained in `master`;
- the tag version matches `CMakeLists.txt`, `vcpkg.json`, and `conanfile.py`;
- the annotated tag object and peeled commit are rechecked after environment
  approval and immediately before publication;
- the exact `target_install_package.cmake` v7.1.0 release archive is pinned by
  SHA-256 and its signature is verified against the committed release key
  before release configuration;
- an existing draft for the verified commit is deleted and recreated, while an
  existing published release is audited without mutation;
- every uploaded asset name and GitHub-computed SHA-256 digest matches the
  signed local asset set before publication;
- GitHub's release attestation and every uploaded asset are verified after
  publication.

The public key is committed at `.github/release-signing-key.asc`. Private key
material is available only to jobs approved through the protected `release`
environment. The publishing job does not receive it until all CMake
configuration, dependency fetching, compilation, and package validation have
completed. The protected `Prepare Signed Release Tag` workflow may also use the
key to produce a tag bundle for administrator verification and push; it cannot
push tags itself. `target_install_package.cmake` is consumed from the signed
archive of its verified immutable `v7.1.0` release, with the exact archive
SHA-256 pinned in CMake.

## One-Time Repository Setup

Complete this setup before merging the release workflow or publishing the next
version:

1. Enable immutable releases under **Settings > General > Releases**.
2. Create an environment named `release` under **Settings > Environments**.
3. Require the release owner as a reviewer, allow self-review, disable
   administrator bypass, and allow deployments only from the selected `master`
   branch.
4. Add `GPG_PRIVATE_KEY`, `GPG_SIGNING_KEY`, and `GPG_PASSPHRASE` as environment
   secrets. Do not keep repository-level duplicates.
5. Create an active tag ruleset for `v*` that restricts tag creation, update,
   and deletion. Give repository administrators an explicit bypass for the
   controlled signed-tag operation.

The signing key currently used by the workflow can be installed with:

```bash
gpg --armor --export-secret-keys \
  7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D |
  gh secret set GPG_PRIVATE_KEY \
    --repo jkammerland/cbor_tags \
    --env release

gh secret set GPG_SIGNING_KEY \
  --repo jkammerland/cbor_tags \
  --env release \
  --body 7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D

gh secret set GPG_PASSPHRASE \
  --repo jkammerland/cbor_tags \
  --env release
```

The passphrase command prompts without placing the secret in shell history.
Confirm the environment secret names with:

```bash
gh secret list --repo jkammerland/cbor_tags --env release
```

## Publishing A Release

1. Update the version in `CMakeLists.txt`, `vcpkg.json`, and `conanfile.py`.
2. Merge that version bump and wait for required `master` checks.
3. Dispatch the protected tag-signing workflow from `master` and approve the
   `release` environment:

   ```bash
   gh workflow run sign-release-tag.yml \
     --repo jkammerland/cbor_tags \
     --ref master \
     -f tag=vX.Y.Z
   ```

4. Download the resulting `signed-release-tag-vX.Y.Z` artifact, verify its
   checksum and tag, and push only that tag as a ruleset-bypass administrator:

   ```bash
   gh run download RUN_ID \
     --repo jkammerland/cbor_tags \
     --name signed-release-tag-vX.Y.Z \
     --dir build/release-tag
   (cd build/release-tag && sha256sum -c vX.Y.Z.bundle.sha256)

   git fetch build/release-tag/vX.Y.Z.bundle \
     refs/tags/vX.Y.Z:refs/tags/vX.Y.Z
   git fetch origin master
   bash scripts/release.sh verify-tag \
     --tag vX.Y.Z \
     --trusted-ref origin/master
   git push origin refs/tags/vX.Y.Z
   ```

5. Dispatch the publishing workflow from `master` and approve the `release`
   environment:

   ```bash
   gh workflow run release.yml \
     --repo jkammerland/cbor_tags \
     --ref master \
     -f tag=vX.Y.Z
   ```

Do not create the GitHub release manually. Publishing makes the release, its
assets, and its tag immutable. If an erroneous immutable release is deleted,
advance to a new version rather than attempting to recreate the same tag.
A rerun for an existing published release performs a read-only rebuild and
audit. A failed run that left a draft safely deletes and recreates only that
draft; it never deletes the signed tag.

## Local Package Validation

With CMake 4.4.x, Ninja, GPG, and the manifest's vcpkg baseline available:

```bash
VCPKG_ROOT=/path/to/vcpkg bash scripts/release.sh build
bash scripts/release.sh sign
```

The build command never reads GPG key material. Without `GPG_SIGNING_KEY`, the
local signing command generates an ephemeral test key. CI adds
`--require-release-key`, which requires the dedicated release fingerprint.
