# Releases

Protected workflows prepare signed tags and release candidates. An administrator
publishes the verified candidate with `scripts/publish-release.sh`.

## Release Assets

For version `X.Y.Z`, the workflow publishes exactly 12 assets:

- `cbor_tags-X.Y.Z.tar.gz` and `.zip` reproducible source trees;
- `.sha256` and `.sha512` files for both archives;
- `cbor_tags-X.Y.Z.spdx.json`, with both checksum files;
- `cbor_tags-release-public-key.asc` for offline signature verification;
- `cbor_tags-X.Y.Z.release-manifest` and its detached `.sig`.

One signed manifest binds all assets to the repository, version, signed tag, and
commit. It replaces per-payload signatures. Verification requires the expected
release identity; a downloaded manifest cannot choose it.

The archives reproduce the tracked source at the signed release commit, not a
pre-installed CMake tree. The SPDX 3 JSON-LD SBOM describes the project and its
direct dependencies. Verification rejects altered, missing, or unexpected assets.

Historical releases keep their original formats. Releases through `v0.22.0` use
lightweight tags and GitHub source archives; install-package assets through
`v0.24.0` use the `-cmake` filename suffix. The current verifier accepts only the
manifest-based format.

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

To verify a download, run the release notes' verification commands with Bash in
the directory containing all release assets. They require the fingerprint above;
the downloaded public key is not itself a source of trust.

## One-Time Repository Setup

Before publishing:

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

`GPG_PRIVATE_KEY` is the armored private key, `GPG_SIGNING_KEY` is the fingerprint
above, and `GPG_PASSPHRASE` is its passphrase. Enter secrets through GitHub settings
or `gh secret set` prompts, not shell command arguments.

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
       --tag-object "${tag_object}" \
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
bash scripts/release.sh sign \
  --repository jkammerland/cbor_tags \
  --version X.Y.Z \
  --tag vX.Y.Z \
  --tag-object FULL_ANNOTATED_TAG_OBJECT_ID \
  --commit FULL_PEELED_COMMIT_ID
```

Without `GPG_SIGNING_KEY`, local signing uses an ephemeral test key. Such assets
are for testing only; production signing requires the release key.

Use the verified tag's full object and commit IDs for the identity placeholders.
