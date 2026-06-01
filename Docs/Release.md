# Release

Rork Hook ships as a source-only SwiftPM package. There are no XCFrameworks,
checksums, or generated project artifacts in the release flow.

## Versioning

Use bare semantic-version tags such as `0.1.0`. Avoid `v0.1.0` tags so SwiftPM
URLs and release notes stay consistent with Rork's source package convention.

`0.x` releases may refine API names and behavior while the package hardens.
Breaking C ABI changes should increment `RORK_HOOK_ABI_VERSION` in
`RorkHookVersion.h`.

## Prepare

Update:

- `Sources/RorkHook/include/RorkHookVersion.h`
- `CHANGELOG.md`

Then verify:

```bash
swift test
Scripts/smoke-client-package.sh
```

## Publish

Commit the version update, tag the commit, and push:

```bash
git tag 0.1.0
git push origin HEAD --tags
```

The CI workflow validates the package and downstream smoke test on pushes and
pull requests. A GitHub release can be created from the tag with the changelog
entry as release notes.
