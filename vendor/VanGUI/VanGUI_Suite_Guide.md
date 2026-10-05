# VanGUI Suite — Functions Guide

The complete public surface of the VanGUI SDK: the immediate-mode core, the
fluent `van::` facade, and every enhancement pillar with the enable-macro it
rides on. Header paths are relative to `include/vangui/`.

- **Core** is the immediate-mode API, in namespace `VanGui`.
- **Suite** functions live in `VanGui::` (and some in `van::`), and are declared
  only when their `VANGUI_ENABLE_*` macro is defined. In this SDK the whole set
  is defined for you by `VanGUI::suite` / `VanGUISDKConfig.cmake` — see the
  frozen-config contract in the README. When a macro is *off* (source builds),
  the same headers declare `inline` no-op shims, so call sites compile unchanged
  and cost nothing.

---

## 1. Core

```cpp
#include <vangui/vangui.h>       // everything in namespace VanGui::
```

The immediate-mode surface: `Begin/End`, `Button`, `SliderFloat`,
`InputText`, `BeginTable`, docking, draw lists, fonts, `ShowDemoWindow()`, etc.
Types are `VanVec2`, `VanVec4`, `VanColor`, `VanGuiID`, `VanGuiWindowFlags`, …
Compile-time configuration is in `vanconfig.h` (frozen for the prebuilt libs).

---

## 2. The `van::` facade  (header-only)

```cpp
#include <vangui/van.h>
using namespace van;
```

RAII scopes, fluent widget results, references instead of pointers, and
designated-initializer option structs. Zero per-frame allocation; wraps the core
without reimplementing any widget.

### Scopes — `if (auto s = van::xxx(...))`
`window(title, WindowOpts)` · `child(id, ChildOpts)` · `group()` ·
`disabled(on)` · `popup(id)` · `modal(title,&open)` · `menu(label)` ·
`menu_bar()` · `main_menu_bar()` · `tab_bar(id)` · `tab(label)` · `tooltip()` ·
`combo(label,preview)` · `list_box(label)` · `table(id,cols,TableOpts)` ·
`tree(label)` · `style_color(idx,col)` · `style_var(idx,val)` · `id(...)` ·
`item_width(w)` · `indent()`.

The scope's `bool` is the open/visible state; it closes itself on block exit
(`End`/`Pop` runs unconditionally where the core requires it, conditionally
otherwise).

### Widgets — return a fluent `Response`
`button(label, ButtonOpts)` · `small_button` · `checkbox(label, bool&)` ·
`radio` · `selectable` · `menu_item` · `slider(float&|int&, …)` ·
`drag(...)` · `input(float&|int&)` · `input_text(char*/std::string&)` ·
`color_edit(Vec4&|Color&)` · `combo(label,int&,items,count)`.

`Response` methods: `clicked()` / `changed()`, `hovered()`, `active()`, and the
chainable `on_click(fn)`, `on_change(fn)`, `on_hover(fn)`, `tooltip(text)`.

```cpp
button("Save", { .primary = true }).on_click(save).tooltip("write to disk");
checkbox("V-Sync", cfg.vsync);
slider("Volume", cfg.volume, 0.f, 100.f, "%.0f%%");
```

### Layout & text
`row([&]{…})` / `column([&]{…})` auto-arrange `van::` widgets (no manual
`SameLine`); `grid(columns, count, [&](int i){…})`; `separator`, `spacing`,
`same_line`, `new_line`, `dummy`; `text`, `text_colored`, `text_disabled`,
`heading`, `bullet`.

### `std::expected` helpers (when `<expected>` is available)
`on_value(exp, fn)` · `on_error(exp, fn)` · `value_or(exp, fallback)`.

The facade also exposes thin `van::` wrappers for the pillars and the Tier 1–3
modules below whenever the matching module is linked — e.g. `toast_*`, `spinner`,
`list_view`, `node_graph`, and for the tooling modules `property_grid` /
`property(...)`, `curve_editor` / `gradient_editor`, `date_picker` / `calendar` /
`time_picker`, `markdown`, `rich_text`, `plot` / `plot_values`, `timeline` /
`timeline_track`, `data_grid`, `banner` / `snackbar`, `perf_hud`, `reorder_item`,
`tr(...)`, plus the `van::Console` / `van::HexEditor` / `van::Repl` aliases. RAII
scope wrappers (`property_grid`, `property_category`, `plot`, `timeline`) close
themselves like the other `van::` scopes.

---

## 3. Enhancement pillars

Each entry lists the macro (defined for you in this SDK), the header, and the
primary entry points. The header is authoritative for exact signatures.

### 3.1 Animation substrate — `VANGUI_ENABLE_ANIM`  ·  `misc/vangui_anim.h`
The one module with a core hook (`Anim::NewFrameUpdate()` in `VanGui::NewFrame`).
Pure easing + ID-keyed tween/spring state; no per-frame allocations at steady
state; evicts idle slots.
```cpp
float   VanGui::Anim::AnimFloat(VanGuiID id, float target, const VanAnimParams& = {});
VanVec4 VanGui::Anim::AnimColor(VanGuiID id, VanVec4 target, const VanAnimParams& = {});
float   VanGui::Anim::AnimBool (VanGuiID id, bool open,      const VanAnimParams& = {});
float   VanGui::Anim::SpringFloat(VanGuiID id, float target, const VanSpringParams& = {});
float   VanGui::Anim::Ease(VanEasing fn, float t);   // constexpr, noexcept
bool    VanGui::Anim::IsAnimating();                 // drives idle/power integration
```

### 3.2 Loading effects — `VANGUI_ENABLE_LOADING`  ·  `misc/vangui_loading.h`
`Spinner`, `SpinnerDots`, `SpinnerBars`, `IndeterminateBar`, `ProgressRing`,
`Skeleton`, `SkeletonText`, `BeginLoadingOverlay`/`EndLoadingOverlay`. Spinners
are stateless (phase from global time); only interpolating widgets touch anim.
Facade: `van::spinner(...)`, `van::progress_ring(...)`, `van::loading_overlay(...)` (RAII).

### 3.3 Notifications — `VANGUI_ENABLE_NOTIFY`  ·  `misc/vangui_notify.h`
Toasts with progress bars and four anchor corners.
```cpp
VanGui::NotifyInfo/NotifySuccess/NotifyWarning/NotifyError("%s", msg);
VanGui::RenderNotifications();        // called for you by RenderExtras()
```
Facade: `van::toast_info/success/warning/error(msg)`, `van::toast_on_error(exp)`.

### 3.4 Theme engine — `VANGUI_ENABLE_THEME_ENGINE`  ·  `misc/vangui_theme_engine.h`
12 semantic tokens, animated `TransitionToTheme`, push/pop scopes, file
hot-reload. `RenderThemeTransition()` runs from `NewFrameExtras()`.
`GenerateTheme(accent, dark)` (`van_kit.h` / `vangui_theme_gen.h`) builds a full
token set from one accent color (pure math, unit-testable).

### 3.5 Named themes — `VANGUI_ENABLE_THEMES`  ·  `misc/vangui_themes.h`
Presets (dark, light, classic, dracula, nord, monokai, gruvbox) + save/load.
`VanGui::LoadTheme(id|name)`, `SaveThemeToFile`, `LoadThemeFromFile`.
Facade: `van::load_theme(...)`, `van::save_theme_to_file(...)`.

### 3.6 Stylesheet (.vss) — `VANGUI_ENABLE_STYLESHEET`  ·  `misc/vangui_style_sheet.h`
Declarative QSS-like front-end over the theme engine. Parser errors return
`std::expected` with line numbers; hot-reload cross-fades via the theme engine.
```cpp
std::expected<void,const char*> VanGui::LoadStyleSheet(const char* path);
std::expected<void,const char*> VanGui::LoadStyleSheetFromMemory(const char*, size_t);
void VanGui::PollStyleSheetChanges();   // from NewFrameExtras()
```
(The `.vss` grammar implementation ships compiled — the parser `.inl` stays in
the private source.)

### 3.7 Standard dialogs — `VANGUI_ENABLE_DIALOGS`  ·  `misc/vangui_dialogs.h`
```cpp
VanDialogResult VanGui::MessageBox(title, message, buttons = Ok);
std::expected<std::string,VanDialogResult> VanGui::GetOpenFileName(filters = nullptr);
std::expected<std::string,VanDialogResult> VanGui::GetSaveFileName(default_name, filters);
```
Facade: `van::confirm(title)`.

### 3.8 Layout helpers — `VANGUI_ENABLE_LAYOUT`  ·  `misc/vangui_layout.h`
`BeginHBox`/`BeginVBox`/`BeginGrid`/`EndBox`, `Stretch(weight)`. Pure cursor
math, no retained layout tree.

### 3.9 Signals / slots — `VANGUI_ENABLE_SIGNALS`  ·  `misc/vangui_signals.h` (header-only)
```cpp
VanGui::VanSignal<Args...>;         // emit(...), connect(slot)->VanConnection, disconnect_all()
VanGui::VanConnection;              // move-only, auto-disconnects in destructor (RAII)
```
Instance-based, no global state, no codegen. With `VANGUI_ENABLE_THREAD` also
linked, cross-thread `connect_queued()` activates.

### 3.10 Model/View — `VANGUI_ENABLE_VIEWS`  ·  `misc/vangui_views.h`
`VanListModel` / `VanTreeModel` (struct-of-function-pointers), virtualized
through the core list clipper (a million rows render O(visible)).
`BeginListView`/`EndListView`, `BeginTreeView`/`EndTreeView`.
Facade: `van::list_view(...)`, `van::tree_view(...)`.

### 3.11 Actions registry — `VANGUI_ENABLE_ACTIONS`  ·  `misc/vangui_actions.h`
Retained metadata unifying menu items, global shortcuts, and palette entries
behind one `Action { Id, Label, Category, Icon, Shortcut, Run, UserData,
Enabled }`. `Register`/`Invoke`/`Find`/`SetEnabled`/`ProcessShortcuts()`.
Function-pointer callbacks — no `std::function`, no allocations at steady state.

### 3.12 Command palette — `VANGUI_ENABLE_COMMAND_PALETTE`  ·  `misc/vangui_command_palette.h`
Ctrl+K registry with fuzzy subsequence match and animated open.
`RenderCommandPalette()` runs from `RenderExtras()`.

### 3.13 Node graph — `VANGUI_ENABLE_NODE_GRAPH`  ·  `misc/vangui_node_graph.h`
`CreateNodeGraphContext`/`Destroy…`, `BeginNodeGraph`/`EndNodeGraph`,
`BeginNode`/`EndNode`, `NodeTitle`, `NodePin`, `NodeLink`, `IsLinkCreated/Deleted`,
`IsNodeSelected`. Facade mirrors each under `van::`.

### 3.14 Extended widgets — `VANGUI_ENABLE_WIDGETS_EXT`  ·  `misc/vangui_widgets_ext.h`
`Toggle`, `SegmentedControl`, `Chip`, `Badge`, `Breadcrumb`, `Stepper`,
`StarRating`, `SearchBox`. Header-only companions in `misc/vangui_widgets_pack.h`
(no macro): `Segmented`, `ToggleSwitch`, `Chips`, `RatingStars`, `Sparkline`,
`Breadcrumbs`, `Debounced`, `Throttled`, `NotificationCenter`.

### 3.15 Charts — `VANGUI_ENABLE_CHARTS`  ·  `misc/vangui_charts.h`
`Sparkline`, `BarChart`, `Gauge`. Facade: `van::sparkline/bars/gauge`.

### 3.16 Feedback — `VANGUI_ENABLE_FEEDBACK`  ·  `misc/vangui_feedback.h`
`AnimatedValue`, `RippleButton`, `ElevatedButton` (ride the anim substrate).

### 3.17 Forms — `VANGUI_ENABLE_FORMS`  ·  `misc/vangui_forms.h`
`BeginForm`/`EndForm`, `FormRow`, `FieldError`, `FieldHint`, `PushInvalid`/
`PopInvalid`, `ValidNotEmpty`, `ValidInRange`. Facade: `van::form(...)` (RAII),
`van::invalid_if(bool)`.

### 3.18 Panels / app chrome — `VANGUI_ENABLE_PANELS`  ·  `misc/vangui_panels.h`
`Splitter`, `AccordionSection`/`AccordionEnd`, `BeginStatusBar`/`EndStatusBar`,
`BeginToolbar`/`EndToolbar`, `ToolbarSeparator`.

### 3.19 Toolbar builder — `VANGUI_ENABLE_TOOLBAR`  ·  `misc/vangui_toolbar.h`
Declarative toolbar with automatic overflow into a `">>"` popup:
`tb.Button/Toggle/Sep/Text/Render()`.

### 3.20 Drop zone — `VANGUI_ENABLE_DROPZONE`  ·  `misc/vangui_dropzone.h`
Bordered drop target. Internal payloads via `BeginDragDropTarget`; OS file drops
via a backend-called `NotifyFilesDropped(paths, count)` (e.g. from
`glfwSetDropCallback`). `HasPendingDrop()`.

### 3.21 Wizard / stepper — `VANGUI_ENABLE_WIZARD`  ·  `misc/vangui_wizard.h`
Multi-page modal: numbered header stepper, Back/Next/Cancel/Finish, per-step
`SetStepValid(bool)` gate, terminal `WizardResult { None, Cancelled, Finished }`.

### 3.22 Shortcuts — `VANGUI_ENABLE_SHORTCUTS`  ·  `misc/vangui_shortcuts.h`
Keyboard-shortcut registry; optionally drives the command palette.

### 3.23 .vui loader — `VANGUI_ENABLE_VUI`  ·  `misc/vangui_vui.h`
Declarative UI loader (parser + renderer over the core widgets). Parser `.inl`
ships compiled — private source.

### 3.24 Thread pool — `VANGUI_ENABLE_THREAD`  ·  `misc/vangui_thread.h`
`InitThreadPool`/`ShutdownThreadPool`, `PostToMainThread`, `Async`,
`AsyncFuture<T>`, `VanFuture<T>`, `DrainMainThreadQueue()` (from
`NewFrameExtras()`). Facade: `van::init_threads/async/async_future/post`. Without
the macro, everything runs synchronously with identical signatures.

### 3.25 Theme editor — `VANGUI_ENABLE_THEME_EDITOR`  ·  `misc/vangui_theme_editor.h`
`ShowThemeEditor(&open)`, `ShowThemeEditorWindow(&open)`.

---

## 4. Header-only utilities (`van_kit.h`)

Available whenever their headers are present — no link dependency.

- **`VanGui::VanUndoStack<T>`** / **`VanCommandStack`** — snapshot and
  command-based undo/redo (`misc/vangui_undo.h`).
- **`VanGui::VanSettings`** — typed INI key/value store with two-way `bind`/
  `pull`/`push` (`misc/vangui_settings.h`).
- **`VanGui::GenerateTheme(accent, dark)`** — semantic theme from one color
  (`misc/vangui_theme_gen.h`).
- **`van::Inspect(title, obj)` + `van::Reflector`** — auto-inspector that builds
  an editing UI from a struct's field types via an ADL `van_describe`, or the
  `VAN_REFLECT_BEGIN/VAN_FIELD/VAN_GROUP/VAN_REFLECT_END` macros
  (`misc/vangui_reflect.h`).
- **`vgu::` RAII scope guards** — `misc/vangui_scoped.h` (no macro): a guard for
  every Begin/End pair.
- **`vgu::` shorthand** — `misc/vangui_shorthand.h`: `Shortcut`, `Section`,
  `SearchBox`, `Kbd`, `HelpMarker`, `Property/Bind`, and subsystem-gated
  `Toast::*`, `Confirm/AskYesNo`, `Async` helpers.

---

## 5. Per-frame drivers (`misc/vangui_enhance.h`)

```cpp
VanGui::NewFrame();
VanGui::NewFrameExtras();     // theme transition + .vss reload + thread drain
// ... your UI ...
VanGui::RenderExtras();       // toasts + command palette
VanGui::Render();

bool VanGui::EnhanceWantsRedraw();     // true while animating or work pending
void VanGui::ShowEnhanceMetrics(&open);// anim pool + thread pool metrics
```

All fully guarded — with a module unlinked, its step is compiled out.

---

## 6. Tooling & application modules (Tier 1–3)

Later additions aimed at real tools and desktop-app surfaces. Same rules as the
pillars: each is gated by a `VANGUI_ENABLE_*` macro (defined for you in this
SDK), keyed by `VanGuiID`, no per-frame heap at steady state, draw-list based.
Header is authoritative for exact signatures.

### 6.1 Log / console — `VANGUI_ENABLE_CONSOLE` · `misc/vangui_console.h`
Retained, filterable, virtualized log view. `VanConsole` object owns its buffer;
`AddLog(level, fmt, ...)`, `Draw(title,&open)` / `DrawContents()`, level mask +
search filter, auto-scroll. Rendering is O(visible rows) via `VanGuiListClipper`.

### 6.2 Property grid — `VANGUI_ENABLE_PROPERTY_GRID` · `misc/vangui_property_grid.h`
Two-column inspector over core tables: `BeginPropertyGrid`/`EndPropertyGrid`,
collapsible `BeginPropertyCategory`, and typed rows `PropertyFloat/Int/Bool/
Color/Text/Combo` + read-only `PropertyLabel`. Complements `van_kit.h`'s reflect.

### 6.3 Curve & gradient editor — `VANGUI_ENABLE_CURVE_EDITOR` · `misc/vangui_curve_editor.h`
Draggable control points: `CurveEditor(...)` + `CurveValue(...)` (piecewise
linear), `GradientEditor(...)` + `GradientSample(...)` + display-only `GradientBar`.
Double-click adds, right-click removes, double-click a stop opens a color picker.

### 6.4 Date/time picker — `VANGUI_ENABLE_DATETIME` · `misc/vangui_datetime.h`
`CalendarWidget`, `DatePicker` (calendar popup), `TimePicker`, plus pure helpers
`VanDaysInMonth` / `VanDayOfWeek` (Sakamoto). Structs `VanDate` / `VanTime`.

### 6.5 Markdown — `VANGUI_ENABLE_MARKDOWN` · `misc/vangui_markdown.h`
`Markdown(text, on_link, ud)` — headings, bold/italic, inline + fenced code,
lists, blockquotes, rules, `[text](url)` links. Font-independent; for tooltips,
About boxes, help. Off-shim renders the raw text.

### 6.6 Hex editor — `VANGUI_ENABLE_HEX_EDITOR` · `misc/vangui_hex_editor.h`
`VanHexEditor`: address/hex/ASCII columns, `VanGuiListClipper`-virtualized,
click-to-edit bytes, highlight range. `Draw(...)` / `DrawContents(...)`.

### 6.7 Real-time plot — `VANGUI_ENABLE_PLOT` · `misc/vangui_plot.h`
`BeginPlot`/`EndPlot`, `PlotXY`, `PlotValues` (x=index), `PlotHLine` — framed
axes, grid, hover crosshair with readout. One active plot at a time; caller owns
the data (maintain your own ring for scrolling telemetry).

### 6.8 Timeline / sequencer — `VANGUI_ENABLE_TIMELINE` · `misc/vangui_timeline.h`
`BeginTimeline` (draggable playhead) / `TimelineTrack` (draggable keyframes;
double-click a lane to add, right-click a key to remove) / `EndTimeline`.

### 6.9 Data grid — `VANGUI_ENABLE_DATA_GRID` · `misc/vangui_data_grid.h`
`DataGrid(id, cols, col_count, model, size, &selected)` over a callback model:
filter box + click-to-sort + selection, virtualized via a per-id cached
filtered/sorted index (O(visible) steady state). `VanDataGridColumn` /
`VanDataGridModel`.

### 6.10 Banners & snackbars — `VANGUI_ENABLE_BANNERS` · `misc/vangui_banners.h`
In-flow `Banner(id, type, text, action, &open)` (severity-colored, optional
action/close) and a managed `Snackbar(text, action, duration)` queue drawn by
`RenderSnackbars()` (returns a clicked snackbar id). Companion to `notify`.

### 6.11 Command console / REPL — `VANGUI_ENABLE_REPL` · `misc/vangui_repl.h`
`VanRepl`: scrolling output + input line with history (Up/Down) and Tab
completion. `AddOutput`, `SetCompletions`, `Draw(id, size, on_exec, ud)`.

### 6.12 Rich text — `VANGUI_ENABLE_RICHTEXT` · `misc/vangui_richtext.h`
`RichText("<c=RRGGBB>..</c> <b>..</b> <i>..</i> <u>..</u> <code>..</code>")` +
`CalcRichTextSize`. Font-independent (faux-bold, tinted italic). For legends,
labels, status lines.

### 6.13 Perf HUD — `VANGUI_ENABLE_PERF_HUD` · `misc/vangui_perf_hud.h`
`PerfHUD(&open)` corner overlay / `PerfHUDContents()` — FPS, frame-time, min/max,
a rolling graph and a 60 fps reference line. Fixed ring, no per-frame allocation.

### 6.14 Drag-to-reorder — header-only · `misc/vangui_reorder.h`
`ReorderItem(payload_type, index, &from, &to)` right after an item turns it into
a drag source + drop target; `ReorderApply(arr, count, from, to)` performs the
move. `ReorderItemLive(...)` reports each move while dragging, so the list makes
room as the item passes; give rows stable IDs and wrap them in `Anim::BeginGlide`
/ `van::glide` so the others slide aside (§7.8). No macro gate.

### 6.15 Localization-lite — header-only · `misc/vangui_i18n.h`
`VanStringCatalog` (`Set`/`Tr`/`Has`/`LoadFromMemory`) + process-wide
`GlobalCatalog()` and `Tr("key")`. Label swapping — not full i18n (RTL/shaping
stay out of scope). No macro gate.

---

## 7. Drawing, effects, icons and the lightweight widgets

Same rules again: one `VANGUI_ENABLE_*` macro each (all defined in this SDK),
draw-list only, no steady-state allocation. Everything that moves reads the
animation clock, so `ReduceMotion`, `Scale` and `TimeScale` (§3.1) govern it, and
anything still moving keeps an idle-sleeping app drawing via `EnhanceWantsRedraw()`.

### 7.1 Vector drawing — `VANGUI_ENABLE_VECTOR` · `misc/vangui_vector.h`
Strokes with real caps and joins, tessellated as one mesh so a 50%-alpha stroke
is 50% everywhere: `StrokeLine/Rect/Circle/Ellipse/Arc/Path`, cubic and quadratic
Béziers, `StrokeFillet` / `FilletPoints` (rounded corners), `StrokeDashed`
(marching ants), `FillPath` / `FillContours` (holes), `PathLength` /
`PointAtLength` (draw-on trimming), and the treatments `GlowPath`, `Breathe`,
`HueCycle`.

### 7.2 CPU rasterizer — `VANGUI_ENABLE_RASTER` · `misc/vangui_raster.h`
Draw data to pixels without a GPU: `RasterizeDrawData`, `RasterizeDrawList`,
`RasterizeCoverage` (glyph baking), `CaptureFrame`, `CropImage`, `CompareImages`,
and self-contained PNG `WritePNG`/`ReadPNG`/`EncodePNG`/`DecodePNG` (`VanImage`).
Powers headless screenshots, golden tests and `CaptureWindow` (§7.7).

### 7.3 SVG — `VANGUI_ENABLE_SVG` · `misc/vangui_svg.h`
`ParseSvg` → a `VanSvg` of `VanSvgShape`s, drawn with `DrawSvg` or placed as a
widget with `SvgImage`; `AppendSvgPath` turns a path's `d` attribute into points. Paths, basic shapes, fills and strokes
through the vector module (needs `VECTOR`).

### 7.4 Effects — `VANGUI_ENABLE_EFFECTS` · `misc/vangui_effects.h`
Shadows (`VanShadow`, drop / `DrawGlowRect` / `DrawInnerShadowRect`), gradients
(`FillRectGradient`, `FillCircleGradient`, `FillConvexGradient`,
`GradientColorAt`), group opacity (`PushOpacity`/`PopOpacity`), transforms
(`PushTransform`/`PopTransform`), anti-aliased clip shapes
(`PushClipRoundedRect`/`PushClipCircle`/`PushClipConvex`), `DrawShimmerRect`,
and `DrawBackdropBlur` (frosted glass). The blur runs on the GPU through a
renderer hook; install one or the panel falls back to a tint:

| Renderer | Call once after the backend's Init |
|---|---|
| DirectX 11 | `VanGui_ImplDX11_InitBlur(device)` · `backends/vangui_impl_dx11_blur.h` |
| DirectX 12 | `VanGui_ImplDX12_InitBlur()` (InitInfo form of Init), then `VanGui_ImplDX12_SetRenderTarget(res, rtv)` every frame before `RenderDrawData` |
| OpenGL 3 | `VanGui_ImplOpenGL3_InitBlur(glsl_version)` · `backends/vangui_impl_opengl3_blur.h` |

### 7.5 Drawn icons — `VANGUI_ENABLE_ICONS` · `misc/vangui_icons.h`
256 line icons on a 24-unit grid (`VanIcon_*`, `FindIcon("save")`), drawn not
bitmapped: Outline / Duotone / Filled styles, any size and weight, draw-on trim,
rotation. `DrawIcon`, `Icon`, `IconButton`, `IconLabelButton`, `IconToggleButton`
with `DrawIconMorph` (menu↔close, play↔pause, eye↔eye-off, …),
`DrawIconTransition`, `AnimatedIcon` (spin, pulse, …), `RegisterIcon` /
`RegisterIconSvg` for your own, and `AddIconFont` to use them inside text
(`VANICON_*` UTF-8 literals). A mesh cache makes repeated icons cheap. Needs
`VECTOR`; the icon font needs `RASTER`.

### 7.6 Gallery — `VANGUI_ENABLE_GALLERY` · `misc/vangui_gallery.h`
`ShowEnhanceGallery(&open)` — every module that is built, live, with its knobs,
in tabs: Loading, Motion, Effects, Vector, Icons, Feedback, Everyday.
`ShowGallerySection(section)` embeds one page in your own window.

### 7.7 Everyday widgets — `VANGUI_ENABLE_EXTRAS` · `misc/vangui_extras.h`
| Function | What it does |
|---|---|
| `CopyButton(id, text)` | copies `text`; the copy icon turns into a check for a second |
| `Keycap("Ctrl+Shift+P")` | key caps; `"Ctrl++"` is Ctrl and the plus key |
| `Avatar(name, size, image, ring)` | round image, or initials on a colour picked from the name |
| `EmptyState(icon, title, hint, action)` | the centred "nothing here yet" block; true when its action is clicked |
| `PasswordInput(label, buf, size)` | password field with an eye toggle |
| `TextEllipsis(text, width)` | one line cut with "…", full text in a tooltip |
| `KeyValue(key, value)` / `KeyValueF` | dimmed key and value in two columns |
| `DividerText("OR")` | separator with a centred label |
| `BadgeDot(count)` | dot (count ≤ 0) or count pill on the last item's corner; pops when it rises |
| `RollingNumber(id, value, decimals, prefix, suffix)` | odometer digits roll to each new value and land exactly on it |
| `BeginCached(id, dirty)` / `EndCached()` | replays last frame's drawing while nothing changed (see below) |
| `VirtualList(count, row_height, fn)` | draws only the rows in view (header-only, no macro needed) |
| `CaptureWindow(name, png)` / `CaptureScreen(png)` | after `Render()`: a window or the frame as PNG (needs `RASTER`) |

**Cached regions.** `if (BeginCached("stats", changed)) { ...; EndCached(); }`
runs the body only when it has to: the first time, when `dirty`, while hovered
(and half a second after, so hover fades finish), during keyboard navigation, or
when the position, clip, style, font or atlas changed. Otherwise the recorded
vertices are appended again and the contents are skipped. Keep child windows out
of a cached region, and pass `dirty = true` while anything inside animates.

Icons come from `ICONS` when built (text fallbacks otherwise); motion from `ANIM`.

### 7.8 Motion additions — part of `VANGUI_ENABLE_ANIM` · `misc/vangui_anim.h`
Scoped transitions; the contents fade through style alpha (child windows too) and
move by offsetting their vertices, so layout and clicks never shift:

- `BeginAppear(id)` / `EndAppear()` — fades and rises in the first time it is drawn,
  and again after about half a second unseen.
- `BeginTransition(id, key)` / `EndTransition()` — when `key` changes, the new
  contents slide in from the right (key grew) or left (key shrank). The wizard's
  pages use it.
- `BeginGlide(id, duration)` / `EndGlide()` — layout animation: contents that land
  somewhere else (reordered, something inserted above, reflow) glide from where
  they were. Scrolling is not a move.

Built into the core widgets, with switches and durations in `VanMotionConfig`:
`NavGlide` / `Focus` (the keyboard focus outline glides to the next item),
`TabContent` + `TabContentSlide` / `TabContents` (a newly selected tab's contents
fade and slide from its side), and touch scrolling — `TouchScroll`,
`TouchFriction`, `TouchBounce`, `TouchOverscroll`, `DragScrollMouse`: with
`io.MouseSource == VanGuiMouseSource_TouchScreen`, dragging scrolls (a press on a
button gives way once it travels; sliders and scrollbars keep theirs), a flick
coasts, and pulling past an end stretches and springs back. Tree nodes and
collapsing headers already ease open and shut (`TreeReveal`).

Reduced motion: `ReduceMotion` snaps movement and keeps shortened fades;
`FollowSystem = true` keeps it in step with the OS setting (Windows "Animation
effects", read about once a second), `SystemPrefersReducedMotion()` asks
directly, and `SetSystemReducedMotion(bool)` lets other platforms push theirs.

### 7.9 Human-readable numbers — header-only · `misc/vangui_format.h`
`FormatBytes(1536000)` → "1.5 MB" (binary: "1.5 MiB"), `FormatDuration(7512)` →
"2h 5m", `FormatRelativeTime(180)` → "3 min ago" (negative: "in 3 min"),
`FormatRelativeTimeSince(time_t)`. Buffer-taking forms, or one-argument forms that
return from a small rotating pool. English wording; wrap in `Tr()` to translate.

### 7.10 In `van.h`
`van::copy_button`, `kbd`, `avatar`, `empty_state`, `password_input`,
`text_ellipsis`, `key_value`, `divider_text`, `badge_dot`, `rolling_number`,
`cached` (a scope), `virtual_list`, `capture_window`, `capture_screen`;
`van::appear`, `transition`, `glide` (scopes); `van::reduce_motion`,
`follow_system_motion`, `system_prefers_reduced_motion`; `van::format_bytes`,
`format_duration`, `relative_time`, `relative_time_since`.

```cpp
if (auto t = van::transition("page", page)) DrawPage(page);
van::rolling_number("balance", balance, 2, "$");
if (auto c = van::cached("stats", stats_changed)) DrawStats();
van::button("Inbox"); van::badge_dot(unread);
van::key_value("Size", van::format_bytes(bytes));
```

---

## 8. Spending less: idle sleep, power, quality, and more touch

### 8.1 Idle sleep — part of `VANGUI_ENABLE_ANIM` · `misc/vangui_anim.h`
Most of an immediate-mode app's cost is drawing frames nobody needed. The loop:

```cpp
while (running) {
    van::wait_for_input();      // returns at once while something moves; otherwise sleeps until input
    PumpMessages(); DrawFrame();
}
```

`Anim::WaitForEvents(wants_redraw)` sleeps in the Win32 message queue for up to
`Anim::IdleWaitTimeout(wants_redraw)` and returns at once when input arrives.
The timeout is `MaxIdleWait` (1 s) with nothing moving, shorter while an item is
hovered (tooltip delays), text is being edited (caret blink) or a mouse button
is held (repeats, long presses). Other platforms pass `IdleWaitTimeout()` to
their own wait (`SDL_WaitEventTimeout`, `glfwWaitEventsTimeout`, `ALooper_pollOnce`).

### 8.2 Low power and automatic quality — `Anim::GetPerfConfig()`
- **Power:** `Auto` (low power on battery, or in the background once the backend
  reports focus loss), `Normal`, or `Low`. In low power, anything moving is drawn
  at `LowPowerFps` (20) rather than the display rate. `SystemOnBattery()` reads
  Windows; `SetSystemOnBattery()` lets other platforms report it.
- **Quality:** with `AutoQuality`, frames slower than `FrameBudget` (1/50 s) for about
  a second step effects down one level (`Full` → `Reduced` → `Low`), and three
  comfortable seconds step back up. Reduced halves shadow rings and blur radius.
  Low draws the backdrop blur as a tint, shadows as a single soft edge, holds
  shimmer and skips inner shadows. Frames after an idle sleep don't count.
  `ReportFrameTime()` feeds a measured frame time when `io.DeltaTime` isn't it.
- **Core fix:** `IsAppFocused()` is the lasting focus state. `io.AppFocusLost` only
  lasts one frame, and a later `AddFocusEvent(true)` used to be dropped as a duplicate.

### 8.3 Core additions — `vangui.h`
| Function | What it does |
|---|---|
| `SetNextWindowRefreshWhenIdle(interval, on_hover, on_focus)` | keeps a window's last drawing while it isn't hovered or focused; `Begin()` returns false then. It still refreshes every `interval` seconds, while appearing, and when it moves or resizes. |
| `SetScrollXSmooth` / `SetScrollYSmooth` / `SetScrollHereYSmooth` / `ScrollToItemSmooth` | programmatic scrolling that eases like the wheel. `VanMotionConfig::SmoothScrollTo` makes the plain calls ease too. |
| `GetWindowOverscroll()` / `CancelItemPress()` | for gestures: the rubber-band stretch, and taking over a press |
| `RegisterUserTexture` / `UnregisterUserTexture` | textures you fill yourself, uploaded by the backend (public now) |

Buttons also shrink slightly while held (`VanMotionConfig::PressScale`, 0.96).

### 8.4 More everyday widgets — `VANGUI_ENABLE_EXTRAS` · `misc/vangui_extras.h`
| Function | What it does |
|---|---|
| `BeginLazy(id, estimated_height)` / `EndLazy()` | skips building an off-screen section and reserves its last size |
| `BeginAutoHeight(id)` / `EndAutoHeight()` | a region whose height eases as its contents change |
| `BeginLoadingSwap(id, ready, lines)` / `EndLoadingSwap()` | skeleton until ready, then the contents fade in over it |
| `PrebakeGlyphs(text)` / `PrebakeGlyphRange(a, b)` | bakes glyphs ahead of time so a panel doesn't stall on first open |
| `SliderRange` / `SliderRangeInt` | two-handle slider |
| `TagInput(id, &tags)` + `VanTags` | chips plus an input: Enter or a comma adds, Backspace removes |
| `StatCard(label, value, delta)` | label, large number, change arrow coloured by whether up is good |
| `TextHighlight(text, query)` | search matches marked and bold |
| `GetImage(path)` / `ImageFile(path, size)` / `ClearImageCache()` | PNG files loaded on a worker thread when the pool is running (on the spot otherwise), uploaded by the backend, cached, and freed when unused for ~10 s |

### 8.5 Touch gestures — `VANGUI_ENABLE_TOUCH` · `misc/vangui_touch.h`
Built on the core's touch scrolling; the mouse drives them too.

| Function | What it does |
|---|---|
| `PullToRefresh(id, &refreshing)` | pull a list past its top and let go; a spinner row holds it open while refreshing |
| `BeginSwipeRow(id, leading, trailing)` / `EndSwipeRow()` | swipe a row to reveal an action; past 40% on release, End returns which side fired |
| `IsItemLongPressed(seconds)` / `BeginPopupContextItemTouch(id)` | hold instead of right-click; letting go doesn't also click |
| `BeginCanvas(id, &view, size)` / `EndCanvas()` + `VanCanvasView` | pan with momentum, zoom about the pointer (wheel; touchpad pinch arrives as Ctrl+wheel); `AddPinchEvent(scale, centre)` feeds two-finger pinches the platform reports |

### 8.6 In `van.h`
`van::wait_for_input`, `idle_timeout`, `wants_redraw`, `low_power`,
`low_power_auto`, `is_low_power`, `quality`, `refresh_when_idle`,
`scroll_y_smooth`, `scroll_here_smooth`, `scroll_to_item_smooth`; `van::lazy`,
`auto_height`, `loading_swap` (scopes), `prebake_glyphs`, `range_slider`,
`tag_input` (+ `van::Tags`), `stat_card`, `highlight_match`, `image`;
`van::pull_to_refresh`, `swipe_row`, `long_pressed`, `context_menu`, `canvas`.

```cpp
van::refresh_when_idle(1.0f);                       // a status window: at most once a second unless touched
if (auto w = van::window("Status")) DrawStatus();
if (auto l = van::lazy("advanced", 400)) DrawAdvancedSettings();
if (van::pull_to_refresh("inbox", loading)) StartReload();
van::swipe_row("mail", archive, del, [&] { van::text(subject); });
van::image("avatars/ada.png", {48, 48}, 24.0f);
```

## 9. Transform gizmos

### 9.1 The gizmo — `VANGUI_ENABLE_GIZMO` · `misc/vangui_gizmo.h`
Move, rotate and scale handles drawn over a 3D view, into a VanGUI draw list, so
they run on every renderer with no graphics code of their own. Needs the vector
module; uses icons, anim and touch when they are built.

```cpp
VanGizmoOptions o;
o.SetViewport(GetItemRectMin(), GetItemRectMax());   // the rectangle the scene is drawn in
o.Space = VanGizmoSpace_Local;
o.Snap = snapping;                                    // Ctrl flips it for one drag
if (VanGui::Gizmo("move", view, proj, object, VanGizmoOp_Translate, o))
    ApplyToSelection(object);
```

Matrices are 16 floats with the translation in elements 12–14: the memory layout
of `glm::mat4` and of DirectXMath's `XMFLOAT4X4`, so either passes straight in.
Right- or left-handed, OpenGL (-1..1) or Direct3D (0..1) depth, perspective or
orthographic: the gizmo reads which from the projection. The one it reads the
wrong way round is a reversed-Z orthographic projection (the dimmed half of each
ring swaps).

| Handles | `VanGizmoOp_*` |
|---|---|
| an arrow per axis, a square per plane, a centre dot moving in the screen plane | `TranslateX/Y/Z`, `TranslateYZ/ZX/XY`, `TranslateScreen` (all: `Translate`) |
| a ring per axis, a view-facing ring, a trackball inside the rings | `RotateX/Y/Z`, `RotateScreen`, `RotateFree` (all: `Rotate`) |
| a box per axis, a uniform handle at the centre | `ScaleX/Y/Z`, `ScaleUniform` (all: `Scale`) |
| the bounding box with corner and edge handles; the opposite corner or edge stays put | `Bounds` (set `VanGizmoOptions::Bounds`) |
| move + rotate + scale together | `Universal` |

- **Space:** world or local axes. Scaling is always along the object's own axes.
- **Snapping** lands the *result* on the grid: the position (per axis step), the
  axis length, the box size in world units. Rotation snaps the angle turned.
  `VanGizmoFlags_SnapRelative` snaps the change instead.
- **While dragging:** the value beside the pointer (`X +1.250`, `Z 45.0°`,
  `×1.500`), a pie for the angle swept, a guide line along the axis, the other
  handles stepping back. Escape puts the matrix back (`Gizmo()` returns true that
  frame and `IsGizmoCancelled()` says so).
- **Seen from the side:** an axis seen end-on fades out, the back half of each ring
  is dimmed (`HideBack` hides it), an arrow pointing away from the camera flips
  to point at it (`NoAxisFlip` keeps it).
- **Motion:** hover eases in, a snap landing pulses, the handles grow in when they
  appear; ReduceMotion keeps the fades and drops the movement.
- **Touch:** handles 1.3× bigger, hit areas 2.2× (`VanGizmoStyle::TouchScale`, `TouchHitScale`).
- **Drawn as an overlay:** scene geometry never hides a handle.

| State | What it says |
|---|---|
| `IsGizmoOver()` / `IsGizmoUsing()` | a handle is under the pointer / being dragged, any gizmo. Skip your own click-to-select and camera keys then: `if (IsItemClicked() && !IsGizmoOver()) Pick();` |
| `GetGizmoPart()` | the part hovered or dragged by the last call |
| `IsGizmoDragStarted()` / `IsGizmoDragEnded()` / `IsGizmoCancelled()` | this frame's drag events for the last call |
| `GetGizmoDragMatrices(before, after)` | the matrix when the drag began and now |
| `GetGizmoDelta(delta)` | this frame's change, `new = old * delta`: apply it to the rest of a selection |
| `GizmoPushUndo(stack, apply)` | on drag end, one `perform(redo, undo)` step on a `VanCommandStack` (`vangui_undo.h`) |

A handle under the pointer takes the mouse from whatever the window drew there
first (the view's image or `InvisibleButton`), so dragging it never also orbits
the camera or moves the window. A view already being dragged keeps the mouse:
sweeping across a handle while orbiting grabs nothing. For a scene drawn behind
every window, `VanGizmoFlags_Background` draws into the background list and takes
the mouse only where no window is.

### 9.2 2D gizmo — `Gizmo2D(id, &transform, ops, canvas, options)`
The same handles for a `VanGizmoTransform2D` (position, rotation, scale) on the
touch module's pan/zoom canvas (`nullptr` = canvas units are pixels): the X and Y
arrows, the XY square and centre dot, the ring (`RotateZ` or `RotateScreen`), the
X/Y/uniform scale handles and the bounds box. Rotation is radians clockwise with
y down, like the draw list's arcs. Dragging a handle never pans the canvas.

### 9.3 View cube — `ViewCube(id, view, proj, pivot_distance, pos, size, flags)`
A cube turned like the camera. Click a face, edge band or corner to swing the
camera round (0.3 s, instant under ReduceMotion) to look from that side, about
the point `pivot_distance` in front of it; drag it to orbit with the horizon kept
level. `VanViewCubeFlags_Axes` draws an axis compass instead (click a ball);
`ZUp` labels the faces for Z-up worlds. It writes `view` and returns true on
every frame it changed it — an orbit camera kept as yaw/pitch reads them back.

### 9.4 Toolbar, shortcuts, matrix helpers
- `GizmoToolbar(id, &ops, &space, &snap, vertical)`: move / rotate / scale /
  universal / bounds, world/local, snap — icons with the icon module, words without.
- `GizmoShortcuts(&ops, &space)`: W E R T pick the tool, X swaps world/local
  (ignored while typing or with Ctrl/Alt held).
- `GizmoIdentity`, `GizmoMultiply`, `GizmoInverse`, `GizmoCompose` /
  `GizmoDecompose` (Euler X then Y then Z, degrees), `GizmoLookAt`,
  `GizmoPerspective`, `GizmoOrthographic` (each right- or left-handed, -1..1 or
  0..1 depth), `GizmoWorldToScreen` — enough for an app without a maths library.

### 9.5 Core addition — `vangui.h`
| Function | What it does |
|---|---|
| `OverlayHitArea(id, min, max, flags, &hovered, &held)` | a hit area for something drawn over what the window already holds: no layout, and it takes the hover (and a press made this frame, with the mouse button's ownership) from an item submitted earlier underneath |

### 9.6 In `van.h`
`van::gizmo`, `gizmo_2d`, `view_cube`, `gizmo_toolbar`, `gizmo_over`,
`gizmo_using`; `van::Gizmo::Translate` / `Rotate` / `Scale` / `Universal` /
`Bounds`; `van::GizmoOptions`, `GizmoSpace`, `GizmoPart`, `GizmoMatrix`,
`Transform2D`.

```cpp
if (van::gizmo("move", view, proj, object, van::Gizmo::Universal)) Changed(object);
van::view_cube("cube", view, proj, orbit_distance, {view_max.x - 110, view_min.y + 10});
```
