# Fab Submission Checklist

## Repository Gates

- [x] Copyright and repository-history audit passed
- [x] Assimp 6.0.5 tag and commit provenance recorded
- [x] Assimp DLL, import library, generated headers, and licenses bundled
- [x] Six Blueprint-callable functions verified
- [x] StaticMesh and latent APIs removed
- [x] Runtime import and construction automation tests pass on UE 5.4, 5.5, 5.7, and 5.8
- [x] Fab package structure validation passes

## Unreal Engine Package Matrix

- [x] UE 5.4 Automation Tests
- [x] UE 5.4 `PackageForFab.ps1`
- [x] UE 5.4 `BuildPlugin` Win64 Rocket build
- [x] UE 5.5 Automation Tests, Fab package validation, and `BuildPlugin`
- [ ] UE 5.6 Automation Tests, Fab package validation, and `BuildPlugin` — Stock UE 5.6.1 verification failed; no engine files were modified.
- [x] UE 5.7 Automation Tests, Fab package validation, and `BuildPlugin`
- [x] UE 5.8 Automation Tests, Fab package validation, and `BuildPlugin`

## Packaged Shipping Smoke

- [x] Import FBX in packaged Shipping
- [x] Import OBJ in packaged Shipping
- [x] Import glTF in packaged Shipping
- [x] Import GLB in packaged Shipping
- [x] Import DAE in packaged Shipping
- [x] Automated DynamicMesh geometry/material/collision verification
- [x] Packaged executable contains Assimp DLL
- [x] No Assimp DLL load error
- [x] Owner transform tracking verified

## Media

- [ ] Human visual confirmation of imported geometry/material
- [ ] Runtime screenshot
- [ ] Thumbnail
- [ ] Blueprint node screenshot
- [ ] Formats and limitations graphic

## Fab Portal

- [ ] Public Project File Link that does not require login
- [ ] Seller, payout, and tax setup
- [ ] Preview Listing
- [ ] Manual activation selected
- [ ] Submit for Review

Do not Submit for Review while Media or the Project File Link is incomplete.
