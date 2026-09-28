# VR UI alpha investigation

## Paused at user request — 2026-09-28

All experimental rendering changes, diagnostic switches and the custom shader
project have been removed. Keep this investigation out of the VR fixes PR.
The historical entries below describe removed code, not active behavior.

Resume point: the user confirmed that adding `$blendtintbybasealpha 1` to the
original vest body material restores visibility. The working scrolling section
already uses that parameter. This supports texture alpha being mistaken for UI
coverage; a permanent fix must preserve tinting and genuine transparency.
Particle artifacts are not yet explained by that result.

Experiment 3's DLL and both pixel-shader profiles compiled; the final compiler
reported zero errors. Nothing was deployed or tested in-game. Runtime shader
compatibility and appearance remain unverified. Do not call this a working fix.

Recoverable local archive of candidate source, original investigation notes,
generated project files and compiled pixel shaders (outside the PR):
`C:\Users\ajsig\AppData\Local\Temp\tf2-vr-alpha-paused-090baa63d7774a4dbdb0474bd31cd3d8`.
This temporary archive can be removed by OS cleanup; the findings in this note
are the durable resume reference. No cosmetic VMTs were changed by this cleanup.

## Change ledger

### Experiment 3: opaque Phong shader coverage (2026-09-27, staged only)

- User confirmed `$blendtintbybasealpha 1` restores the vest. Do not ship that
  workaround as a material fix: it changes tint semantics as well as alpha.
- `skin_dx9_helper.cpp`: opt-in `vr_ui_opaque_model_alpha` (default 0, not
  archived). Only fully opaque materials targeting `_rt_OpenVRUI` set formerly
  unused shader constant 27.y to one. All other draws set it to zero.
- `skin_ps20b.fxc` (also compiled as ps30): use that constant to output alpha 1
  without modifying RGB/tint/lighting. Disable depth-to-alpha output for that
  coverage path. Alpha-tested, translucent, additive and flashlight paths are
  excluded by existing bFullyOpaque classification. Particles are NOT fixed.
- `game_shader_generic_vrui_tf.vpc`, `vr_ui_shader_support.cpp`, and the project
  registration in `src/vpc_scripts/projects.vgc` build a DLL registering only
  VertexLitGeneric, rather than replacing every standard shader.
- Generated solution: `src/vr_ui_shader.sln`. Generate from src with:
  `devtools/bin/vpc.exe /tf /win64 /define:SOURCESDK +game_shader_generic_vrui /mksln vr_ui_shader`.
- Release/win64 DLL compiled successfully with `PostBuildEventUseInBuild=false`.
  It is NOT deployed. Do not deploy the broader `game_shader_generic_std.dll`
  produced during the initial build-system check.
- Pixel shader compilation started using ShaderCompile2, four threads, profiles
  20b then 30, with shaderpath `src/materialsystem/stdshaders`, input
  `skin_ps20b.fxc`. Exec session 78119. Initial compiler estimate was about an
  hour for 20b; completion and errors must be checked before deployment.
  Earlier session 72214 was cancelled after a source refinement; do not use
  partial output from it. Generated .inc changes are compiler artifacts.
- Runtime compatibility remains unverified. This replaces the installed
  VertexLitGeneric implementation with SDK source even when the switch is off;
  baseline appearance, skins, flexes, cloaking and weapon effects need testing.
  Shared stock skin bytecode names also require compatibility checks before
  shipping. Source compilation is not proof of runtime shader compatibility.
- Undo: remove the convar/include and coverage constant logic from the helper,
  restore the original alpha/depth expression in the FXC, remove the new VPC,
  support source and project registration. No cosmetic VMTs were edited.
  No client DLL or runtime assets have been replaced in this experiment.

### Experiment 2: built-in VGUI offscreen model path (2026-09-20)

**Removed on 2026-09-20: user reports the transparency issue persists.** The
loadout screenshot does not establish where alpha is corrupted in this path.
No per-draw or intermediate-target capture was collected for this experiment.
Its switch, includes, Paint override and declaration have been removed.

- Files: `src/game/client/game_controls/basemodel_panel.cpp`, its `.h`, and this ledger.
- Switch: `vr_ui_isolated_model_test`, default **0**, not archived. Set 1 to
  test, 0 to restore the resource-defined render path on the next paint.
- Adds two includes, one ConVar, one Paint override and its declaration. The
  override temporarily enables `m_bRenderToTexture` only when it was disabled
  and the active target is `_rt_OpenVRUI`; an RAII scope restores the flag.
- Undo those additions only. Preserve any unrelated edits in these files.
- Uses the existing Begin3DPaint/End3DPaint path, paints once, and does not
  modify native state, stencil, shader materials, or particle simulation.
- Scope: test isolation, NOT a complete coverage solution. The engine owns the
  offscreen target; this is not yet a custom target with explicit alpha handling.
- Risks: resource clear/background colors designed for direct painting may
  become visible rectangles; preview allocation may limit resolution; original
  bad alpha may survive the final copy. Do not label this a confirmed fix.
- Tests: compare vest/Flatliner, scoreboard, loadout, HUD portrait, and high-res
  layouts with 0/1. Disable if surrounding UI changes or backgrounds become solid.
- Release/win64 build and deployment succeeded; diff whitespace check passed.
  Runtime test did not resolve the issue. No success claim for transparency.

### Experiment 1: opaque merged-model stencil coverage (2026-09-19)

**Removed on 2026-09-20 after a runtime regression.** User screenshots showed
missing loadout item panels, scoreboard contents, and class-menu text. The
experiment affected UI beyond cosmetic coverage. Depth/stencil or other render
state interference is a hypothesis, not a confirmed internal cause. Do not reuse
this shared-target replay approach without isolating and validating its state.
The includes, helper, switch, and call site below have all been removed.

- Changed only `src/game/client/tf/vgui/tf_playermodelpanel.cpp` plus this ledger.
- Switch: `vr_ui_opaque_coverage_test`, default **0**, not archived. Toggle to 1
  for testing; toggle back to 0 to stop the extra pass immediately.
- Adds three includes (`itexture.h`, `imaterialvar.h`, `istudiorender.h`), the
  switch and `RepairVROpaqueModelCoverage` helper after `DECLARE_BUILD_FACTORY`,
  and a guarded call at the start of `RenderingMergedModel`.
- Undo precisely those additions; preserve all other edits in that file and the
  rest of the worktree. No VMT files, public interfaces, hooks, or GPU bridge
  code are modified. The named DepthWrite material is created in memory.
- Replays merged geometry with existing bones into stencil, then calls
  `ClearBuffersObeyStencilEx(false, true, false)` to fill alpha under the mask.
- Restricted to direct `_rt_OpenVRUI` rendering and entirely opaque material
  lists. Skips translucent, alpha-tested, additive, alpha-modulated, actively
  cloaked models, and existing forced-material overrides.
- Does NOT fix particles or root-model alpha. Does NOT implement the proposed
  full separate-target solution. It is a bounded coverage proof of concept.
- Risks to test: DepthWrite's depth bias/writes can affect later model draws;
  private UI stencil is cleared and returned to disabled/default settings, not
  restored from an arbitrary incoming stencil state; mixed-material models are
  deliberately skipped; additional geometry/material queries may cost time.
- Original build/deployment succeeded; runtime test failed as described above.
- Test vest in class selection/loadout after map load, then compare Flatliner,
  adjacent weapons/cosmetics, and player-status HUD. Compare switch 0 versus 1.

2026-09-19: Added this document only. No rendering, shader, material, configuration,
or diagnostic commands were added in this investigation step. To undo this step,
remove this file. Existing unrelated worktree changes are not part of this step.

For future experiments, record exact files, the opt-in switch (default off),
build/deployment status, observed results, and specific reversal instructions here.
Do not use a whole-worktree reset to undo an experiment.

## Established observations

### Installed vest material comparison (2026-09-27)

- Read the original VMTs directly from the installed TF2 `tf2_misc_dir.vpk`,
  without extracting or modifying game assets. Both team variants agree on
  the relevant difference: `sum24_hazardous_vest_1` (scrolling section) enables
  `$blendtintbybasealpha 1`; the main vest material does not. Neither original
  model material enables `$translucent` or `$alphatest`.
- In `skin_dx9_helper.cpp`, this parameter selects BLENDTINTBYBASEALPHA when
  self illumination is disabled. In `skin_ps20b.fxc`, that combination skips
  multiplying output alpha by base-texture alpha. This is a concrete candidate
  explanation for the two sections' different visibility, not runtime proof.
- User's previous tests: adding translucent/alphatest changed underlying UI
  visibility without restoring the cosmetic; removing bumpmap did not fix it.
- Next single-variable diagnostic: restore the original body VMT, then add only
  `$blendtintbybasealpha 1` to the body material for the tested team. Restart and
  compare after map load. This can change tint appearance and is NOT a proposed
  permanent asset fix. Leave the scrolling material unchanged. Undo by removing
  that one line. No asset edits or rendering changes were made in this step.
- Particle blending remains a separate unresolved case; this comparison does
  not establish its cause.

- Pre-map desktop-captured menus work; in-map UI rendered into `_rt_OpenVRUI`
  exhibits the reported artifacts.
- The Medic Hazardous Environment Vest is visible in the captured RGB image.
  The corresponding alpha image has a nearly black vest body and white emblem.
  This establishes incorrect coverage at the final UI composite, not missing mesh
  geometry. It does not by itself identify which draw wrote the bad alpha.
- Captured Scout particles have square regions in alpha not matched by similarly
  opaque RGB. The user clarified artifacts are visible over other UI, such as
  the scoreboard, not exclusively over empty UI background.
- Sampled vest materials report error=0, translucent=0, alphatest=0,
  cloakfactor=0, cloakpassenabled=1, alpha=1. These are sampled material states,
  not a trace of every material bind during drawing.
- Flatliner is reported to work. Its supplied VMT enables both translucency and
  alpha testing; it also differs in textures, bump mapping, and lighting values.
  This is not a controlled one-variable comparison.

## Source evidence and limits

- `src/materialsystem/stdshaders/skin_ps20b.fxc` computes output alpha using
  diffuse modulation and, conditionally, base-texture alpha. Opaque RGB does not
  imply output alpha is one.
- `skin_dx9_helper.cpp` and `vertexlitgeneric_dx9_helper.cpp` enable destination
  alpha writes for fully opaque materials. Other paths differ.
- `spritecard.cpp` explicitly disables destination-alpha writes. The exact
  material/shader of the user's affected particles has not been established.
- `src/game/client/viewrender.cpp` forces alpha writes while painting the VR UI.
- `game/mod_tf/materials/vgui/inworldui.vmt` uses translucent UnlitGeneric to
  composite the resulting texture, interpreting its alpha as visibility.
- SDK shader source is evidence for the intended design, not proof that the
  installed engine/shader binaries exactly match this source revision.

## Experiments already removed

- Model-panel alpha-write overrides (normal-material and preserve-alpha variants).
- Native one-time color-write-mask changes and the temporary per-draw dispatch hook.
- Alpha/color texture dumps, material-state logging, native-state logging, and
  their console variables/helpers.
- The per-draw hook caused a reported crash and was removed. Do not reintroduce it.

Native-state diagnostics showed the expected 1280x720 UI target, queue mode zero,
and mask 15 (RGBA). A native mask change produced 7 (RGB) before model rendering,
then 15 after rendering. This establishes a reset somewhere during that interval;
it does not identify a particular draw or internal engine function responsible.

## Supported-path investigation

- `src/public/istudiorender.h` exposes `STUDIORENDER_DRAW_OPAQUE_ONLY` and
  `STUDIORENDER_DRAW_TRANSLUCENT_ONLY` on `IStudioRender::DrawModel`.
- `src/vgui2/matsys_controls/mdlpanel.cpp` draws the root and merged models through
  `CMDL::Draw`, then invokes model callbacks and `PostPaint3D` for particles.
- `src/public/tier3/mdlutils.h` exposes no draw-flags parameter on `CMDL::Draw`.
  Its implementation was not found in this checkout. Splitting the passes is
  therefore not a one-line change to the existing VGUI draw calls.
- The material-context interface exposes stencil control and alpha-only clearing
  via `ClearBuffersObeyStencilEx`. `screenspace_general.cpp` provides copy-alpha
  and premultiplied-color blending options. Their suitability and behavior in
  the installed runtime have not been tested.

## Next bounded checks before implementation

1. Establish how to reproduce the CMDL draw setup using supported public APIs,
   retaining flexes, skin/body selection, bone merges, material proxies, lighting,
   and model callbacks. Do not replace it with a simplified draw that loses these.
2. Determine whether opaque/translucent selection preserves alpha-tested cutouts
   under a coverage-material override. A blanket silhouette pass is insufficient.
3. Account separately for additive and ordinary translucent particles. Restoring
   all old alpha can hide effects outside existing UI; making all pixels opaque
   recreates black rectangles; changing only the final blend cannot repair bad
   coverage already stored in the texture.
4. Verify render-state restoration and GPU-only operation. Avoid CPU readback in
   normal rendering, duplicate particle simulation, and native draw hooks.

## Acceptance checks for any future opt-in prototype

- Vest visible in class selection and loadout after map load.
- Flatliner and other translucent/alpha-tested cosmetics retain real transparency.
- Particle effects over scoreboard do not punch holes or darken sprite rectangles.
- HUD portrait silhouettes/effects outside the panel background remain visible.
- Fresh-boot menus, both eyes, high-resolution resizing, and frame pacing unchanged.
- Disabling the experiment restores the previous behavior without restarting.

Current status: PAUSED. All three experiments removed; no rendering prototype
deployed. Vest workaround confirmed; rendering fix and particle issue unresolved.
