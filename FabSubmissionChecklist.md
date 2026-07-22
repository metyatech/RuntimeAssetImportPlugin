# Fab Submission Checklist

## Repository Gates

- [x] Copyright and repository-history audit passed
- [x] Assimp 6.0.5 tag and commit provenance recorded
- [x] Assimp DLL, import library, generated headers, and licenses bundled
- [x] Six Blueprint-callable functions verified
- [x] StaticMesh and latent APIs removed
- [x] Runtime import and construction automation tests pass on UE 5.4 through 5.8
- [x] Fab package structure validation passes

## Unreal Engine Package Matrix

- [x] UE 5.4 Automation Tests
- [x] UE 5.4 `PackageForFab.ps1`
- [x] UE 5.4 `BuildPlugin` Win64 Rocket build
- [x] UE 5.5 Automation Tests, Fab package validation, and `BuildPlugin`
- [x] UE 5.6 Automation Tests, Fab package validation, and `BuildPlugin`
- [x] UE 5.7 Automation Tests, Fab package validation, and `BuildPlugin`
- [x] UE 5.8 Automation Tests, Fab package validation, and `BuildPlugin`

## Packaged Shipping Smoke

- [ ] Import FBX in a packaged Shipping build
- [ ] Import OBJ in a packaged Shipping build
- [ ] Import glTF in a packaged Shipping build
- [ ] Import GLB in a packaged Shipping build
- [ ] Import DAE in a packaged Shipping build
- [ ] Visually verify DynamicMesh geometry, material, and collision
- [ ] Confirm the packaged executable contains the Assimp DLL
- [ ] Confirm there is no Assimp DLL load error

## Media

- [ ] Thumbnail
- [ ] Runtime screenshot
- [ ] Blueprint node screenshot
- [ ] Formats and limitations graphic

## Fab Portal

- [ ] Public Project File Link that does not require login
- [ ] Seller, payout, and tax setup
- [ ] Preview Listing
- [ ] Manual activation selected
- [ ] Submit for Review

Do not Submit for Review while the packaged runtime smoke, Media, or Project File Link is incomplete.
