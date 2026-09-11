# Stock Plugin UI Icon Rules

This file governs every UI icon change under `Plugins/Stock`. It is based on
`C:\Users\xiongaox\Downloads\AI_UI_Icon_Rules.md` and is kept beside the code so
future UI work has a versioned, reviewable contract.

## Required Workflow

Before adding or modifying a UI icon:

1. Search for an existing use of `Icons::Draw` and reuse its icon ID and button
   treatment where the meaning matches.
2. Search `Icons/lucide/` for a Lucide icon that expresses the requested action.
3. If Lucide lacks a suitable icon, evaluate Fluent UI System Icons, then Material
   Design Icons. Record the source and version beside the imported asset.
4. Add the source SVG to `Icons/lucide/`, update the reviewed geometry in
   `IconsData.h`, and expose it through `Icons::Id` in `Icons.h`/`Icons.cpp`.
5. Render it only through `Icons::Draw`. Do not reproduce geometry in a window,
   panel, dialog, or button implementation.

The checked-in source set uses `lucide-static v1.43.0`, whose ISC license is
included in each SVG source file. Run `python Icons/gen_icons.py` after changing
the source set; the script validates that SVGs stay within the renderer's
supported line-art subset.

## Rendering Contract

`Icons::Draw(Gdiplus::Graphics&, Icons::Id, bounds, color, alpha, strokeWidth)`
is the sole operational-icon renderer. It scales Lucide's 24 x 24 viewBox,
uses Lucide's 2px round-cap/round-join stroke treatment, and accepts the active
UI color. Callers retain ownership of hover, pressed, disabled, and selected
backgrounds, while icon color must follow the same state color as nearby text.

Recommended visible bounds:

- Dense controls: 14-16 physical px
- Ordinary button controls: 16-18 physical px
- Toolbar or title controls: 18-20 physical px
- Large action surfaces: 20-24 physical px

Use `g_data.DPI()` or `g_data.RDPI()` for all size and spacing calculations.

## Button Rules

- Reuse the owning surface's existing owner-draw background, border, hover,
  pressed, and disabled behavior.
- Use icon plus text for named actions where text is already part of the UI.
  Preserve the current `确定` / `取消` text-only treatment unless a deliberate
  dialog-wide design update changes it.
- Keep icon-text spacing consistent within the same button group.
- Do not use emoji, Unicode glyphs, manually drawn GDI/GDI+ paths, temporary
  SVG paths, font glyphs, or raster images for a standard UI action.
- Do not introduce Qt, ImGui, a font dependency, a network fetch, or a sidecar
  resource requirement solely to show an icon. The plugin ships as one DLL.

## Current Standard Mappings

| Action | Icons::Id | Lucide source |
|---|---|---|
| Close / dismiss | `X` | `x.svg` |
| Confirmed / selected | `Check` | `check.svg` |
| Add | `Plus` | `plus.svg` |
| Delete | `Trash2` | `trash-2.svg` |
| Edit | `Pencil` | `pencil.svg` |
| Move up | `ArrowUp` | `arrow-up.svg` |
| Move down | `ArrowDown` | `arrow-down.svg` |
| Sort order | `ArrowUpDown` | `arrow-up-down.svg` |
| Refresh | `RefreshCw` | `refresh-cw.svg` |
| Expand / collapse | `ChevronUp`, `ChevronDown` | matching SVG |
| Expand / collapse secondary panel | `ChevronsUp`, `ChevronsDown` | matching SVG |
| Open / close left panel | `PanelLeftOpen`, `PanelLeftClose` | matching SVG |

## Data Visualization Exemptions

The following are intentional data encodings or chart annotations, not general
UI action icons. They remain in their chart-specific drawing code and may not
be reused for a new UI action:

- Price direction and trend strength symbols in `StatusBarPanel` and
  `OrderBookPanel`.
- Buy/sell signals, high/low annotations, crosshairs, and series markers in
  `TimelineChart`, `KLineChart`, and `IndicatorChart`.
- Market health color dots and series/legend markers.
- The plugin application icon (`IDI_STOCK`) and OS-provided cursors and message
  box icons.

A new exception requires an explicit review note that explains why an existing
Lucide/Fluent/Material icon is semantically wrong for the data representation.
