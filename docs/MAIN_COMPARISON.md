# Historical branch comparison — 13 October 2026

> Archived review, not the current integration state. The repository now contains
> the later `f6d6d08` merge of upstream receiver changes. The observations below
> describe the pre-merge snapshot and must not be used as current build instructions.

The comparison followed `git fetch origin`, with local base `5b0be255` and
upstream main at `9af05b867e1cf7f03c777dd569eed1d62d2f8e7a`. The local branch
was ten commits behind. The review itself did not merge, reset, check out or
overwrite files. Its three-way simulation was written to the local build artifact
`build/main-comparison/results.json`, including untracked additions and CRLF/LF
normalization. That temporary artifact is not distributed with the repository.

## Conflicts identified at that snapshot

| Area | Files | Finding and recommendation at the time |
| --- | --- | --- |
| Build | `.gitignore`, `CMakeLists.txt` | Text conflicts. Preserve the native OpenProspero build, SDL, FFmpeg avformat, DLNA and local assets; adapt upstream tests. |
| Desktop | `src/platform/desktop.cpp` | Both branches changed input, rendering and receiver control. Preserve the HUD and two reception modes while reviewing upstream checks separately. |
| Receiver | `src/receiver/airplay.cpp`, `airplay.hpp` | Different connection tracking, PIN handling and callbacks. Review stream-end handling, initialized callback outputs and PIN log redaction; preserve the console RNG check and distinct PC/PS5 identities. |
| Decoder | `src/receiver/decoder.cpp`, `decoder.hpp` | Upstream added EAGAIN handling and failed-allocation cleanup. Preserve FrameConverter, color conversion, SIMD padding, slice threading and the shared latest-frame mailbox. |
| UI | `src/ui/screen.cpp`, `screen.hpp` | Independently added files. Upstream used render_screen/ScreenUiState and icon fonts; local code used Screen/UiAssets and PNG prompts. The APIs were not interchangeable. |
| Video upload | `src/platform/video_texture.cpp`, `video_texture.hpp` | Independently added implementations with different APIs. Both reused staging buffers and fences; review host contracts before choosing individual changes. |
| Tests | `tests/decoder_test.cpp` | Independently added tests; combine coverage after stabilizing the decoder contract. |
| Documentation | `README.md` | Independently added documents. Keep instructions consistent with the chosen build and actual console evidence. |

The simulation found eight conflicts in tracked files and five add/add collisions.

## Overlaps beyond text conflicts

- Upstream added `src/platform/ps5_main.cpp` and `scripts/build-ps5*.ps1`;
  local code used `src/platform/ps5.cpp`, `cmake/ps5.cmake` and
  `tools/build-ps5*.sh`. These were parallel console ports. The local host had
  already displayed the UI on hardware and requested shell shutdown; the other
  host returned from main, contrary to the project's shutdown requirement.
- Upstream screen-model/render tests depended on its UI API. State and lifecycle
  tests needed separate review: the local State retained frame_after for
  compatibility, while rendering used latest_frame and shared immutable frames.
- Upstream changed UxPlay crypto, pairing, RAOP and request handlers for ephemeral
  keys, malformed pairing requests and partial initialization cleanup. These
  changes could merge textually while still affecting protocol interoperability;
  they needed pairing tests, iPad runs and documented upstream modifications.
- Upstream added Figtree/FontAwesome resources; local UI assets used Inter and an
  image atlas. Preserve the licenses of whichever resources remain in use.
- Upstream's PacBrew build path did not by itself establish console entropy
  availability. The R5 log showed file-not-found for native RNG module paths;
  R6 tried KERN_ARND through a validated libkernel gate with explicit error checks.

## Integration sequence recommended by that review

1. Save the local work, including untracked files.
2. Preserve the local UI, DLNA and console host; align build inputs.
3. Adapt decoder, callback, stream-end and PIN logging changes.
4. Bring in pairing changes with documentation and relevant tests.
5. Run adapted checks and test pairing/video on the actual console.

For current behavior, use [README.md](../README.md), [BUILDING.md](BUILDING.md),
[RELEASING.md](RELEASING.md) and [CONSOLE_TESTING.md](CONSOLE_TESTING.md).
