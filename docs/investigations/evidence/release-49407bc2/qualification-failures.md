# Qualification failures for source `49407bc2`

## Stale replay-bridge source identity

- Authored replay map: **Silver Wolves' Haven**
- Requested row: fresh targeted stock outcome control
- Terminal error: `stock replay qualification mod source does not match the current source commit`
- Cause: HorseMod was rebuilt after commit `49407bc2`, but
  `REPLAY_QUALIFICATION_SOURCE_COMMIT` is a CMake configure-time definition and
  the excluded replay-bridge target still contained the prior commit identity.
  The runner rejected the run before it could become qualification evidence.
- Repair: reconfigure the existing
  `build_cmake_LessEqual421__Shipping__Win64` tree with
  `CMAKE_BUILD_TYPE=LessEqual421__Shipping__Win64`, explicitly rebuild both
  `HorseMod` and `ReplayQualificationMod`, and rerun all local tests.
- Rebuilt identities:
  - HorseMod DLL: `8D02EEE4A93A9F803FCDB5942ED551C4AF1B0529C49D642F71A125595D27A9D9`
  - Replay bridge: `4D123DB9E7EAC882DDEFFBB5F1A550B44FCFB5D30053EDE784687ED21F42E84F`
  - Schema: `7158092911E439573F921A3A91A4296FA86049A699DB0C938526D317F9CC8A76`
- Cleanup: SC6 was absent and every diagnostic flag was restored to `false`.
- Regression: 8/8 C++ tests and 68/68 Python tests passed after the paired
  artifact rebuild.
