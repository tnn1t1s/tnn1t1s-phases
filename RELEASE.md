# PHASES — Release Process

Phases (slug `tnn1t1s-phases`, modules `Phase` and `Expr`) is not yet on the
VCV Library. This file is the checklist for the first submission and the
process for updates after it. Each numbered item is done when its check
passes; open one issue per item, as tnn1t1s-ghost did (its issues #1–#17),
and close it with the evidence.

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

### Manual QA in Rack, installed from the `.vcvplugin`

Install the packaged plugin (`dist/*.vcvplugin` into the plugins folder),
never the `deploy` copy, and restart Rack.

PHASOR
- [ ] Adds from the browser; panel, labels, lights and tooltips correct.
- [ ] Ramp follows a clock at 60, 120 and 180 BPM; LAP changes the lap
      length; the wire freezes when the clock stops and resumes on the next
      pulse; RST snaps to zero; DIRECTION reverses; START and LEN window.
- [ ] RATE up to audio rate stays stable.

EXPR
- [ ] Adds from the browser; the default equation sounds.
- [ ] Typing a valid equation swaps it in with no click; an invalid one
      shows the error and changes nothing.
- [ ] Every context-menu example compiles and plays.
- [ ] Knob letters, CV inputs, X, GAIN, LIMIT, LOOP, POS and their CVs act
      as documented.
- [ ] Show equation window opens, moves, resizes, closes, and comes back in
      the same place after save and reload.
- [ ] Layout → Wide display and back to Rack panel; neighbours move out of
      the way and nothing overlaps on return.
- [ ] Fault light and fault policy behave on an equation with undefined
      regions.

Both modules
- [ ] Save, close, reopen: every param, the equation, loopRepeats, the
      window and the layout restore.
- [ ] Initialize, randomize, duplicate, and bypass behave.
- [ ] Sample rates 44.1, 48, 96 and 192 kHz; engine threads 1 and 4.
- [ ] Rack's dark-panel setting and UI zoom 50%–400% render correctly.
- [x] `patches/demos/` patches load from the packaged build. The one demo,
      `phases-ghost-groove.vcv`, passes `tools/headless-check.sh`.
- [ ] `log.txt` has no warnings or errors from `tnn1t1s-phases` after a
      session.

### Stability and cost

- [ ] Soak: a patch with PHASOR, two EXPRs and the equation window open runs
      for 4 hours (a parameter, not a measured requirement), with an
      equation edit every few minutes. Rack's memory in Activity Monitor is
      flat after the first 10 minutes; no audio dropouts in the log.
- [ ] CPU: record each module's meter reading (Rack's CPU meter) at 48 kHz
      for the default equation and for the heaviest example. Record the
      numbers in the CHANGELOG entry or an issue.
- [ ] Instruments (Leaks and Time Profiler) during rapid equation edits, the
      known hot path: no leaks, no audio-thread allocation.

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
- [x] One demo patch, `patches/demos/phases-ghost-groove.vcv`, built against
      `tnn1t1s-phases`; the Examples menu's patterns cover the other ideas.
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
