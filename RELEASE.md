# PHASES — Release Process

Phases (slug `tnn1t1s-phases`, modules `Phase` and `Expr`) is not yet on the
VCV Library. This file is the checklist for the first submission and the
process for updates after it. An item is done when its check passes, with
the evidence written next to it.

## 0. Repository

The Library builds from the public repository `tnn1t1s/tnn1t1s-phases`, whose
history starts at the release tree. Development history stays in the private
`tnn1t1s/tnn1t1s-loops`; none of it is pushed to the public repository, which
receives squashed commits only. Version `2.0.0` (Rack 2, first release).

## 1. Code and manifest hygiene

- [x] `plugin.json` URLs point at `tnn1t1s/tnn1t1s-phases`, and each one
      resolves without a login once the repository is public. (All return
      200 unauthenticated, 2026-10-10.)
- [x] Every module tag is on Rack's tag list; on load, `log.txt` shows no tag
      or manifest warnings for `tnn1t1s-phases`. (`tools/headless-check.sh`.)
- [ ] Per-module `keywords` present (they are) and read back in the module
      browser search.
- [x] No shipping code refers to modules that do not ship. The directory
      `src/lives/` and the C++ namespace `Lives` keep their internal names;
      neither is visible to users.
- [x] No debug logging on hot paths: `grep -n 'DEBUG(\|INFO(' src` is empty;
      the one `WARN` (Expr.cpp, a backwards saved domain) stays.
- [x] The Rack SDK version is 2.6.6 in both `build.yml` and `stress.yml`.
- [x] `make` with no `RACK_DIR` override fails with a clear message on a
      machine without `~/Developer/vcv-rack`; CI and the Library set
      `RACK_DIR` themselves.

## 2. Provenance and naming

- [ ] Fonts: Inter and Saira Condensed (SIL OFL), Latin Modern Math (GUST
      Font License, shipped unmodified), each with its license file in
      `res/fonts/` and listed in `res/fonts/README.md`. ShareTechMono comes
      from Rack itself (`asset::system`) and is not bundled.
- [ ] Panels are original artwork rendered by panelkit; no third-party marks.
- [x] The EXPR example equations in the context menu are listed with their
      source in `doc/examples.md`.
- [ ] Nothing in the published git history is media, sample data or anything
      else outside this list.

## 3. Compilation plan

The Library build farm compiles one named commit for four targets. Prove each
before submitting.

| Target | Where | Command | Passes when |
|---|---|---|---|
| mac arm64 | laptop | `make clean && make dist` | `dist/tnn1t1s-phases-2.0.0-mac-arm64.vcvplugin` exists |
| mac x64, mac arm64 | GitHub Actions `build.yml` | runs on push | both jobs green, artifacts attached |
| win x64, lin x64 | GitHub Actions `build.yml` (qno toolchain container) | runs on push | both jobs green, no warnings from `src/` |
| Library toolchain | [rack-plugin-toolchain](https://github.com/VCVRack/rack-plugin-toolchain) | `build.yml` uses a community image of it (qno); a run in the official repo is the stronger check | same as above |

Portability traps ghost hit: case-sensitive include paths on Linux, a
`RACK_DIR` default that resolves wrongly once `plugin.mk` is included, mac-only
flags in the default target, a Makefile that runs git, anything under `dep/`.
The `deploy` target stays mac-only and is never used for release testing.

`build.yml` runs on push, so it first runs when the release repository
exists. Push the release branch, read the four jobs, fix, repeat.

## 4. Test plan

### Automated, every change

```sh
make -C tests/wire run      # adjacency wire and PHASOR lap counter, ASan/UBSan
make -C tests/expr run      # EXPR compiler, runtime, conditioning, preview
uv run pytest               # panel specs to rendered SVG anchors
```

All pass with zero sanitizer reports.

### Pre-release gate, once per release candidate

`gh workflow run stress.yml` on the candidate commit: the same unit tests
on Linux with LeakSanitizer, plus cppcheck and clang-tidy. Read the
uploaded reports. `cppcheck` runs with `--error-exitcode=0`, so a green job
does not mean a clean report; triage every finding.

Triage of the 2.0.0 candidate: fixed the history ring's pointer walk across
`HistoryBin` fields (cppcheck objectIndex), a cloned branch in the tokenizer
and a dead store in the display. Left as is: `unusedStructMember` (members
used from other headers, a false positive of per-file analysis), unchecked
`snprintf` returns (fixed-size buffers sized for their formats), and style
suggestions (pass-by-reference, enum base type, `std::accumulate`).

### Manual pass in Rack, about 15 minutes

Run this once on the candidate. Install `dist/tnn1t1s-phases-2.0.0-mac-arm64.vcvplugin`
built from that commit (not the `deploy` copy) and restart Rack. Each step
says what to do and what you should see; write a line for anything else.

1. Open `patches/demos/phases-ghost-groove.vcv` and start the clock. The
   groove plays and EXPR's playhead crosses its slice in time with the
   beat. Turn on Engine → CPU meter and note EXPR's and PHASOR's readings.
2. Turn the clock's BPM to 180 and back to 116: the playhead stays on the
   beat. Stop the clock: playhead and voice hold. Start it: they resume.
3. On PHASOR, flip DIR: the curve plays backwards. Turn LEN to 50 %: the
   two grey bars on EXPR's display close in.
4. On EXPR, right-click → Examples and play three Patterns and one Basic.
   Each one plays and the display redraws its curve.
5. Click EXPR's display, type `y = sin(` and press Cmd+Enter: an error
   with line and column, and the sound does not change. Make it
   `y = a sin(bx)` and press Cmd+Enter: it swaps in without a click and
   knobs 1 and 2 get orange rings.
6. Right-click → Show equation; drag and resize the window. Right-click →
   Layout → Wide display, then back: the modules to the right move out of
   the way and return without overlapping.
7. File → Save as a new file, then open it: the equation, knobs, window
   position and layout come back.
8. Open `patches/demos/phases-visualizer.vcv`. Both scopes draw a closed
   figure once a lap; turning a, b and k changes it. EXPR's jacks read X
   then Y.
9. Set View → Zoom to 50 % and then 200 %: panels, labels and the display
   draw cleanly.
10. Help → Open user folder → `log.txt`: no warning or error line that
    names `tnn1t1s-phases`.

Not part of the pass: a sample-rate and thread matrix, a multi-hour soak,
and an Instruments session. The stress gate's LeakSanitizer run covers
leaks in the compiler, runtime and preview code; the audio path has no
allocation, locks or compilation and costs 15–165 ns a sample on an M3
(a standalone benchmark of the evaluator and conditioner, under 1 % of a
48 kHz sample). A spike that shows in step 1 gets an Instruments Time
Profiler capture of that patch.

### Platforms not tested by hand

Windows and Linux are build-verified only. Font rendering, the floating
equation window and the wide layout have not been seen running there. Either
borrow a machine for an hour or state it in the submission.

## 5. Docs and patches

- [x] `README.md`, `doc/PhasesUserManual.md` describe the two shipping
      modules and nothing else. There are no screenshots; the Library shows
      the panel SVGs.
- [ ] `CHANGELOG.md` 2.0.0 entry names PHASES PHASOR and PHASES EXPR (done)
      and gets a date on the day of the tag.
- [x] Two demo patches built against `tnn1t1s-phases`, both passing
      `tools/headless-check.sh`: `phases-ghost-groove.vcv` and
      `phases-visualizer.vcv`. The Examples menu's patterns cover the other
      ideas.
- [x] `res/README.md`, `src/README.md`, `tests/README.md` match the tree.

## 6. Local release verification

Last step before submission, on the exact commit to be submitted:

```sh
make clean && make dist
```

Install that `.vcvplugin`, restart Rack, and confirm: brand TNN1T1S in the
browser, both modules add, a demo patch saves and restores, `log.txt` clean.

## Versioning

`plugin.json` `version` is `MAJOR.MINOR.REVISION`, no `v` prefix. MAJOR tracks
the Rack major version (`2.x.x` for Rack 2). REVISION for fixes, MINOR for new
modules or notable features.

## First submission

1. Repository public; final commit on `main`; tag `v2.0.0` pushed;
   `gh release create v2.0.0`.
2. Capture the exact hash: `git rev-parse HEAD`. The build farm builds a
   commit, not a tag or branch.
3. Open exactly one issue on [VCVRack/library](https://github.com/VCVRack/library),
   titled with the slug alone: `tnn1t1s-phases`. Body, as for ghost
   (VCVRack/library#919): one-line description, source URL, version, exact
   commit hash, license, the platforms it builds on.

## Cutting an update (after publication)

1. Land on `main`; bump `version`; dated CHANGELOG entry.
2. Commit, tag `vX.Y.Z`, push both.
3. `gh release create vX.Y.Z`.
4. Comment the new version and exact commit hash on the plugin's Library
   thread.

A maintainer builds per-OS and closes the thread when live. `configParam`
default changes apply only to newly added modules; saved patches keep stored
values.
