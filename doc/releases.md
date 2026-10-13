# Releases

Protected workflows prepare signed tags and release candidates. An administrator
publishes the verified candidate with `scripts/publish-release.sh`.

## Release Assets

For version `X.Y.Z`, the workflow publishes exactly 13 assets:

- `cbor_tags-X.Y.Z.tar.gz` and `.zip` reproducible source trees;
- detached `.sig`, `.sha256`, and `.sha512` files for both archives;
- `cbor_tags-X.Y.Z.spdx.json`, with its detached signature and checksums;
- `cbor_tags-release-public-key.asc` for offline signature verification.

The archives reproduce the tracked source at the signed release commit, not a
pre-installed CMake tree. The SPDX 3 JSON-LD SBOM describes the project and its
direct dependencies. Verification rejects altered, missing, or unexpected assets.

## Trust Model

- Release tags are signed, annotated tags on `master`, with successful CI for
  the tagged commit.
- The tag version must match `CMakeLists.txt`, `vcpkg.json`, and `conanfile.py`.
- Tags and assets use the release key committed at
  `.github/release-signing-key.asc`, fingerprint
  `7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D`.
- Signing requires approval through the protected `release` environment;
  package builds have no access to signing secrets.
- Publication requires immutable releases to be enabled and verifies the
  published assets and GitHub release attestation.

## Publishing A Release

1. Update the version in `CMakeLists.txt`, `vcpkg.json`, and `conanfile.py`.
2. Merge the version bump and wait for all required `master` CI checks to pass.
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
   (cd build/release-tag && sha256sum -- vX.Y.Z.bundle | cmp - vX.Y.Z.bundle.sha256)

   git fetch build/release-tag/vX.Y.Z.bundle \
     refs/tags/vX.Y.Z:refs/tags/vX.Y.Z
   git fetch origin \
     "+refs/heads/master:refs/remotes/origin/master"
   bash scripts/release.sh verify-tag \
     --tag vX.Y.Z \
     --trusted-ref origin/master
   git push origin refs/tags/vX.Y.Z
   ```

5. Dispatch the candidate workflow from `master` and approve the `release`
   environment:

   ```bash
   gh workflow run release.yml \
     --repo jkammerland/cbor_tags \
     --ref master \
     -f tag=vX.Y.Z
   ```

6. Publish the signed candidate from a clean detached worktree at the release
   commit, using an administrator's authenticated GitHub CLI. Run this on Linux
   with Bash 4 or newer and GNU `sha256sum`:

   ```bash
   git fetch origin \
     "+refs/heads/master:refs/remotes/origin/master" \
     "+refs/tags/vX.Y.Z:refs/tags/vX.Y.Z"
   tag_object="$(git rev-parse refs/tags/vX.Y.Z)"
   commit="$(git rev-list -n 1 refs/tags/vX.Y.Z)"
   release_worktree="$(mktemp -d /tmp/cbor-tags-release.XXXXXX)"
   git worktree add --detach "${release_worktree}" "${commit}"
   cd "${release_worktree}"

   bash scripts/release.sh verify-tag \
     --tag vX.Y.Z \
     --trusted-ref origin/master

   gh run download RUN_ID \
     --repo jkammerland/cbor_tags \
     --name cbor-tags-vX.Y.Z-signed \
     --dir build/release-publish/release-packages

   GH_REPO=jkammerland/cbor_tags \
     bash scripts/publish-release.sh notes \
       --tag vX.Y.Z \
       --commit "${commit}" \
       --version X.Y.Z \
       --output build/release-publish/release-notes.md

   GH_REPO=jkammerland/cbor_tags \
     bash scripts/publish-release.sh publish \
       --tag vX.Y.Z \
       --tag-object "${tag_object}" \
       --commit "${commit}" \
       --version X.Y.Z \
       --package-dir build/release-publish/release-packages \
       --notes-file build/release-publish/release-notes.md
   ```

Use the publisher script, not ad-hoc `gh release create` commands. If a failed
publish leaves a draft, inspect and explicitly delete that draft before retrying.
An existing published release is only audited, never modified. Corrections to
an immutable release require a new version, even if the release was deleted.

## Local Package Validation

With CMake 4.4.x, Ninja, GPG, and the project's vcpkg baseline available:

```bash
VCPKG_ROOT=/path/to/vcpkg bash scripts/release.sh build
bash scripts/release.sh sign
```

Without `GPG_SIGNING_KEY`, local signing uses an ephemeral test key. Such assets
are for testing only; production signing requires the release key.
