# ZODAR UI — Design specification (Tooling & UI track)

Source of truth for values implemented in `Engine/UI`. Every value is marked:

- **Confirmed** — approved by the project owner, or authored in the approved Figma file
  ("ZODAR UI Design", Foundations page) and unchanged since.
- **Unconfirmed** — chosen during implementation; needs a design decision before it is final.

Figma screens and the HTML prototypes are **visual references**. No screen is claimed to be a
pixel-perfect match until every relevant measurement below is confirmed.

## Colour tokens (sRGB)

| Token | Value | Status | Use |
| --- | --- | --- | --- |
| `brand/background` | `#111111` | Confirmed (official) | Window background |
| `brand/surface` | `#181818` | Confirmed (official) | Panels, sidebar, cards, inputs |
| `brand/primary` | `#FFFFFF` | Confirmed (official) | Primary text, logo |
| `brand/accent` | `#18B77A` | Confirmed (official) | Primary action, selection, progress |
| `brand/error` | `#B7181B` | Confirmed (official) | Errors |
| `ui/surface-raised` | `#1F1F1F` | Confirmed (UI addition) | Thumbnails, selected nav, secondary buttons, hover |
| `ui/border` | `#2A2A2A` | Confirmed (UI addition) | Outlines and dividers |
| `ui/text-secondary` | `#9A9F9C` | Confirmed (UI addition) | Secondary text, icons, inactive nav |
| `ui/text-muted` | `#5C615E` | Confirmed (UI addition) | Hints, placeholders, build number, C++ label |
| `ui/accent-hover` | `#1FCB88` | Confirmed (UI addition) | Hover state of accent elements only |

## Typography

Approved faces, exactly: **Michroma Regular 400, Rajdhani Light 300, Rajdhani Regular 400,
Rajdhani Bold 700**. Files: `Resources/UI/Fonts` (OFL), pinned in `Resources/UI/MANIFEST.sha256`.

| Style | Face | Size | Line height | Letter spacing | Case | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Brand/Display | Michroma Regular | 28 | 1.20 | 8% | as typed | Confirmed (Figma) |
| Brand/Heading | Michroma Regular | 18 | 1.30 | 6% | as typed | Confirmed (Figma) |
| Brand/Label | Michroma Regular | 11 | 1.30 | 24% | as typed | Confirmed (Figma) |
| UI/Title | Rajdhani Bold | 20 | 1.20 | 2% | as typed | Face confirmed; size/spacing from Figma (authored for SemiBold) |
| UI/Body | Rajdhani Regular | 16 | 1.30 | 1% | as typed | Face confirmed; size/spacing from Figma (authored for Medium) |
| UI/Caption | Rajdhani Regular | 13 | 1.30 | 4% | as typed | Face confirmed; size/spacing from Figma (authored for Medium) |
| UI/Overline | Rajdhani Bold | 12 | 1.30 | 18% | UPPER | Face confirmed; size/spacing from Figma (authored for SemiBold) |
| UI/Button | Rajdhani Bold | 14 | 1.00 | 12% | UPPER | Face confirmed; size/spacing from Figma (authored for SemiBold) |
| UI/Path | Rajdhani Regular | 13 | 1.40 | 0% | as typed | Face confirmed (replaces the former monospaced style) |

Rajdhani Light 300 is loaded but not used by any Stage 1 style. Figma text styles still use
Medium/SemiBold and will be updated to the approved faces later.

## Metrics

| Metric | Value | Status |
| --- | --- | --- |
| Corner radius | 2 | Confirmed (Figma) |
| Border width | 1 | Confirmed (Figma) |
| Control height (button, input, select) | 40 | Confirmed (Figma) |
| Button horizontal padding | 20 | Confirmed (Figma) |
| Button icon slot (18 px, gap 10) | — | Not implemented in Stage 1 |

## Button states

| Variant | Default | Hover | Pressed | Disabled |
| --- | --- | --- | --- | --- |
| Primary | fill `brand/accent`, text `brand/background` | fill `ui/accent-hover` (confirmed) | fill accent × 0.88 (**unconfirmed**) | 35% opacity (**unconfirmed**; prototype used 35%) |
| Secondary | fill `ui/surface-raised`, border `ui/border`, text `brand/primary` | border `ui/text-muted` (prototype) | fill `brand/surface`, border `ui/text-muted` (**unconfirmed**) | 35% opacity (**unconfirmed**) |
| Ghost | text `ui/text-secondary` | text `brand/primary` (prototype) | + fill `ui/surface-raised` (**unconfirmed**) | 35% opacity (**unconfirmed**) |

## Scaling

- Continuous scale, clamped to 0.5–4.0. `Density` policy: display scale × user scale (launcher,
  settings). `FitDesign` policy: fit a design canvas into the window (loading window, 960×540).
- Test profiles: 1.0×, 1.33×, 2.0× (plus odd scales in unit tests). Not the only supported scales.
- Glyph atlases are baked per pixel size (quarter-pixel steps); SVGs are rasterized per scale.

## Logos

Original Brand Pack files, byte-for-byte (SHA-256 in `Resources/UI/MANIFEST.sha256`):

- `LOGO_NoBackGround/White/SVG/512 X 512 LOGO_NoBackGround.svg` — icon only.
- `LOGO_TEXT_NoBackGroundBlack/White/SVG/512 X 512 LOGO_TEXT_NoBackGroundBlack.svg` — icon + wordmark.
- `Loading/Loading.svg` — original loading screen design (reference for Stage UI-2).

Content boxes used for cropping (SVG units, from the Figma brand file): icon `59,87 394×338`,
lockup `32,110 450×290` — **unconfirmed** as official clear-space rules.

## Loading window (Stage UI-2)

Design canvas 960×540, scaled to fit the window (`ScalePolicy::FitDesign`).

| Item | Value | Status |
| --- | --- | --- |
| Window sizes | 1920×1080 → 960×540, 2560×1440 → 1280×720, 3840×2160 → 1920×1080 | Confirmed (spec) |
| Other monitors | Largest profile ≤ half the monitor; < 1080p → 960×540; shrunk to 90% of the usable area if needed | **Unconfirmed** |
| Window style | Borderless, centred in the usable area, never fullscreen | Confirmed (spec) |
| Launcher window after loading | Largest 16:9 fitting 85% of the usable area, capped at 1920×1080 × display scale, centred, bordered + resizable | **Unconfirmed** |
| Artwork | Original `Loading.svg` (pattern + logo) with the "C++" tag removed (shapes inside 870,25 60×25) | Confirmed (spec: remove C++ tag) |
| Stages | Analyzing system · Preparing shaders · Extracting resources · Configuring settings · Preparing engine · Initializing components | Confirmed (spec + rename) |
| Progress bar | 2 units high, x 64–896, `brand/accent`; track `brand/surface`; smooth toward reported values only | Confirmed (spec); height/track colour from prototype |
| No exact progress | Indeterminate sweep (28% segment, 1.4 s), no percentage, no counts | Confirmed (spec) |
| Status text | Rajdhani Regular 13, 24% spacing, uppercase, `ui/text-secondary`; counts right-aligned, `ui/text-muted` | Face confirmed; size/spacing from prototype |
| First-launch hint | "First launch — shaders are being cached", after 30 s in Preparing shaders, `brand/accent`, Rajdhani Regular 12 | Text/trigger confirmed; style from prototype |
| Version | "ZODAR Engine v0.1  ·  C++" (version from the build), Rajdhani Regular 11, `ui/text-muted`, bottom left | Confirmed (spec + C++ placement); size from prototype |
| Percentage | Bottom right, only with exact overall progress | From prototype |
| Failure | Bar turns `brand/error`; "<Stage> failed" in `brand/primary`; Open log (Secondary) + Retry (Primary) on the status row | **Unconfirmed**: text colour (error red on #111 is low contrast for small text), button size (standard 40-unit controls) |
| Log panel | x 64–896, y 40 to 16 above the bar, `brand/surface` + `ui/border`; newest lines at the bottom; errors `brand/error` | **Unconfirmed** |
| Completed | Bar full; window becomes the launcher after 0.6 s | **Unconfirmed** (delay) |

### SVG mask investigation (Loading.svg)

- nanosvg has no `<mask>` support and draws a mask's contents as ordinary shapes. Unhandled,
  `Loading.svg` renders four solid white 962×130 bars over the artwork.
- Fix: `<mask>` definitions are removed from the in-memory SVG text before parsing
  (`stripMaskDefinitions`). The file on disk is unchanged (SHA-256 checked).
- Remaining difference: the pattern outlines use masks to keep only the inner half of a 2 px
  stroke. Without the mask the full 2 px stroke is drawn centred on the outline, so lines are
  ~1 px wider and ~0.5 px further out: 22,651 vs 17,450 lit pattern pixels at 960×540 (+30%).
  Colour is identical (`#151515` on `#111111`); the difference is visible only with strong
  contrast enhancement.
- Whole artwork vs the designer's export (`Loading.png`, 960×540, status area and C++ tag
  excluded): mean difference 0.22/255; 1.04% of pixels differ by more than 3/255; 0.02% by more
  than 32/255 (anti-aliasing on logo edges).
- Not done (needs a decision): halving masked stroke widths would match the line weight but not
  its 0.5 px inward offset.

## Known limitations

- Text: Latin + Latin-1 + common punctuation only; no complex shaping (no HarfBuzz). Missing
  characters render as U+FFFD.
- Layout: a wrapping container reports its wrapped height only when its main size is Fixed.
- nanosvg does not support SVG `mask`/`clip-path`. Masks are stripped before parsing; masked
  elements render unmasked (see the Loading.svg investigation above).
- Mock startup data exists only in the sandbox/tests; no engine progress source exists yet, so
  the real loading window cannot run until `IStartupProgress` is implemented by the engine.
- Window placement is demonstrated only in the sandbox (SDL); the engine's `Platform::Window`
  cannot yet be borderless, resized or centred (deferred).
- Temporary SDL backend: no anti-aliasing, so 2 px rounded corners show small steps at high
  scale. Colours are blended in sRGB space. The Vulkan UI pass will replace it.
