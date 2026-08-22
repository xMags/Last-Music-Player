# Implementation Checklist: Playlist, Downloads, Browse redesign

Status of the three redesigned screens against the prototypes in this folder, turned into a work list. Produced 2026-08-22 from a full audit of the code at commit `8269b6b`; line numbers were verified against that commit and may drift as files change. Search for the named symbol if a line number no longer matches.

## Current state (2026-08-22, after the implementation pass)

Everything ticked below was implemented and verified in one pass: the build is clean, all six native test suites pass, and the app was launched and its database migration confirmed against the real library file. The line numbers in each item still describe where the problem *was*, which is what makes the reasoning checkable; they no longer point at the fix.

Four items are deliberately still open, each with its reason written into the item itself:

- The hero "N offline" chip, because no cheap correct count exists.
- Decades and Hi-res tiles, because the library stores neither a year nor any audio-format detail.
- Hover-revealed buttons on the Downloads rows, because hiding controls until hover costs more than it buys.
- The app-wide danger colour, because retuning one shared token to match one screen deserves its own decision.

## How to use this document

- Work top to bottom: **P0** items are bugs that are broken today, **P1** items are spec behavior that is missing, **P2** items are the approved new features, **P3** is visual fidelity. Items within a section are independent unless noted.
- Each item names where the problem lives, the intended fix mechanism (reusing existing code where it exists), and a concrete way to verify the fix.
- For pixel reference, open the matching `.dc.html` file in a browser at a 1440x900 window and compare side by side. Exact values also live in the "Design Tokens" section of `README.md` in this folder.
- The overall state is better than expected: everything on all three screens is wired to real backend state, with no mock data anywhere. The gaps below are specific, not structural.
- Sections "Kept deviations", "Out of scope", and "Flagged, not scheduled" are informational. Do not "fix" the kept deviations.

---

## P0: Bugs (broken today, all verified in code)

- [x] **1. Recent searches never load back after a restart**
  - Persistence is write-only. Searches are saved via `EncodeRecentSearches` + `SettingsManagerService().SetString(L"RecentSearches", ...)` (`MainWindow\MainWindow.Browse.cpp:650-659`), but `DecodeRecentSearches` (`Backend\RecentSearchStore.cpp:92`) is called only from tests. Nothing ever reads the setting, so `m_recentSearches` starts empty every launch and the RECENT SEARCHES row is hidden on every cold start (`MainWindow.Browse.cpp:694-696`).
  - Fix: during browse landing hydration (`HydrateBrowseLandingAsync`), read `GetString(L"RecentSearches")` and decode it into `m_recentSearches` before building the chips. The decode function already exists and is already tested.
  - Verify: search something, restart the app, open Browse. The chip row shows the previous search. "Clear" then restart shows nothing.

- [x] **2. Genre tiles land on an empty result page**
  - Clicking a Browse-all tile stuffs the genre name into the search box as a plain text query (`BrowseCategory_ItemClick`, `MainWindow\MainWindow.Browse.cpp:106-122`), but the local search SQL matches Title/Artist/Album only, with no Genre clause (`Backend\DatabaseEngine.Queries.cpp:408-410`; same limitation in the in-memory path at `MainWindow\MainWindow.Search.cpp:273-276`). A tile that claims "94 tracks" typically opens the "Nothing for ..." empty state.
  - Fix: navigate by genre instead of by text. The correct mechanism already exists: `TrackQuery.GroupKey` with `LibraryGroupingSql::GroupMatchPredicate` (`Backend\LibraryGroupingSql.h:84-92`), which is how the Library screen filters a genre. Either route the tile click through the same drill-down the Library genre cards use, or add a genre-scoped search mode. Do not just add a Genre clause to the generic text search; typing "jazz" should not suppress title matches.
  - Also decide the Catalog-scope behavior: tiles are built from library genres, but clicking under Catalog scope currently fires a remote provider text search for the genre word. Simplest consistent rule: tile click always navigates the library, regardless of scope.
  - Verify: click a genre tile whose count is nonzero; exactly that many tracks appear. Repeat with Catalog scope active.

- [x] **3. Playlist grid view card sizing never applies**
  - The detail grid reuses `LibraryCardGrid_Loaded` (`MainWindow\MainWindow.xaml:3589-3590`, handler at `MainWindow\MainWindow.Library.cpp:391-422`), which sizes cards from `LibraryTabsContent().ActualWidth()`. That element is collapsed whenever the detail surface is visible (`MainWindow\MainWindow.LibraryDetail.cpp:826`), so `availableWidth <= 0` and the function returns before setting `ItemWidth`/`ItemHeight`. The detail grid runs on whatever default pitch the ItemsWrapGrid picks.
  - Fix: measure the width of the detail surface's own container (the element hosting `LibraryDetailContent`) instead. Keep the existing card+gap pitch math; it is correct and commented. While there, use the spec's minimum of 150px for the detail track grid (spec: `repeat(auto-fill, minmax(150px, 1fr))`, gap `20px 18px`); the 170 minimum belongs to the Library tab grids.
  - Verify: open a playlist, switch to grid view, resize the window. Cards fill the row edge to edge and the column count changes with width.

- [x] **4. "Clear cache" confirmation is erased before anyone can read it**
  - `DownloadsClearCache_Click` writes its result into `DownloadsStatusText`, then immediately calls `RefreshDownloadsView(true)`, which unconditionally rewrites that same TextBlock with the queue status (`MainWindow\MainWindow.Downloads.cpp:948-958` vs `:389-401`). Neither "Playback cache cleared" nor the busy message is ever visible. The change-folder failure message at `:978` has the mirror problem: it shows, but the next 750ms revision tick wipes it.
  - Fix: stop using the header status line (a spec-defined slot: "3 downloading · 4 queued") as a toast target. Give transient confirmations their own home, for example an `InfoBar` under the header or a dedicated caption that `RefreshDownloadsView` does not own, cleared on a timer or on navigation. Route both the clear-cache and change-folder messages through it.
  - Verify: click Clear cache; the confirmation is readable and the queue status line still shows queue status. Trigger the change-folder refusal (attempt while a download runs); the message survives the next refresh tick.

- [x] **5. Hover reverts accent-colored controls to stock grey**
  - Several controls get their state color as a local `Background` value, which WinUI's default `PointerOver` visual state overrides. Affected:
    - Selected download tab chip (`MainWindow\MainWindow.Downloads.cpp:567`): hovering the active teal chip turns it grey.
    - Active gear button (`MainWindow.Downloads.cpp:917`): hover drops the accent wash.
    - Primary Play/Pause buttons (`DetailActionPrimaryButton`, `Styles\Tokens.xaml:481-490`): spec hover is `#0069B8`; the default template swaps to grey instead. `PrimaryActionHoverBrush` already exists unused at `Tokens.xaml:34`.
    - Now-playing and selected playlist rows (`MainWindow\MainWindow.DetailToolbar.cpp:667-669`): hover replaces the accent wash with `ListViewItemBackgroundPointerOver` grey.
  - Fix: the codebase already has the right pattern, per-style theme-brush overrides like `ToggleButtonBackgroundChecked` at `Tokens.xaml:351-358`. Override `ButtonBackgroundPointerOver` (and `ButtonBackground`) in per-control `Resources` for the stateful styles, and `ListViewItemBackgroundPointerOver` in the detail list's scope (note the existing `ListViewItemBackgroundSelected*` overrides at `MainWindow.xaml:2639-2641` are scoped to a different list). For the tab chips, whose color is set at runtime, prefer two styles (active/inactive) swapped in `ApplyDownloadTabVisuals` over raw brush assignment, so each style can carry its own hover brushes.
  - Verify: hover the selected Downloads tab, the active gear, a primary Play button, and the now-playing row. Each keeps (or slightly darkens) its accent color.

---

## P1: Functionality gaps

### Browse

- [x] **Record recent searches from the typing path**
  - `RecordBrowseRecentSearch` fires only on Enter (`MainWindow\MainWindow.Search.cpp:205`) and on result click (`:470`). The debounced live-search path (`RunDebouncedHomeSearch` -> `RunHomeSearchNowAsync`, `MainWindow.Search.cpp:162-208`) never records, so typing a query and reading the results leaves no chip.
  - Fix: record from the debounced path too, but only once a query "settles" (e.g. when results render and the query is unchanged for a couple of seconds, or on leaving the page with a non-empty query), so every keystroke prefix does not become a chip.
  - Verify: type a query, wait for results, navigate away and back. The chip exists without pressing Enter, and intermediate prefixes do not.

- [x] **Refresh the Browse landing after library changes**
  - Genre tiles and the resume row are computed once per session: `HydrateBrowseLandingAsync` short-circuits on `m_browseLandingLoaded` (`MainWindow\MainWindow.Browse.cpp:702-705`) and every caller passes `force=false`. A library scan or playing new tracks never updates them.
  - Fix: clear `m_browseLandingLoaded` (or call with `force=true`) when a scan completes and when playback history changes, or simply on each navigation to Browse if the queries are cheap enough.
  - Verify: play a track, navigate to Browse. It appears in "Pick up where you left off" without restarting the app.

- [x] **Pick a real top result and drive the kind badge**
  - The top result is literally the first row: `m_searchTopResult = scoped.front()` (`MainWindow\MainWindow.Browse.cpp:498`) over a DateAdded-ordered list, and the kind badge is a hardcoded `Text="SONG"` (`MainWindow\MainWindow.xaml:1417`) with no code path that writes it.
  - Fix: score candidates against the query (exact title match > title prefix > artist/album match, tie-break on play count) and allow an album facet to win, using the facets already built by `BuildSearchFacets` (`Backend\SearchFacetPolicy.h:44-47`). Set the badge text ("SONG"/"ALBUM") from the picked item. Scoring is a pure function; a good candidate for the native policy tests alongside the existing search policies.
  - Verify: search an exact album name; the top result is that album with an ALBUM badge. Search an exact song title; that song wins even if it is the oldest addition.

- [x] **Browse-all tiles for an empty or tiny library**
  - A brand-new library shows no tiles at all, although `BrowseLandingLabels` (`Backend\TrackSearchPolicy.cpp:116-130`, `kBrowseLandingLabelLimit` at `TrackSearchPolicy.h:67`) exists, is tested, and was written exactly to provide a fallback label set. The UI bypasses it (`MainWindow\MainWindow.Browse.cpp:733-765` reads `LoadTopGenres`/`LoadGenres` directly).
  - Fix: route the tile list through `BrowseLandingLabels` so the fallback engages, or consciously drop the policy and remove it (see "Flagged, not scheduled"). Wiring it is the smaller change and revives tested code.
  - Verify: run with an empty library; Browse-all still shows tiles instead of a blank section.

- [x] **Add the TOP RESULT and SONGS eyebrow labels**
  - The prototype places an eyebrow label over each results column (`Browse - Redesign.dc.html:154` and `:173`); the implementation has neither (`MainWindow\MainWindow.xaml:1399`, `:1433`).
  - Fix: two `EyebrowText`-style TextBlocks (11/700/0.16em, tertiary), shown exactly when their column shows.
  - Verify: search anything under the All filter; both labels render, and they disappear with their sections under other filters.

### Playlist detail

- [x] **Selection bar: add the close button and a discoverable entry point**
  - The spec's trailing spacer + close glyph `` (12px, tertiary) that clears the selection is missing entirely (`MainWindow\MainWindow.xaml:3421-3432`). Combined with the kept click-to-play deviation, there is no pointer-only way to enter multi-select (Ctrl/Shift-click only, `MainWindow\MainWindow.DetailToolbar.cpp:703-742`) and no obvious way out. Grid view has no discoverable selection at all.
  - Fix: add the close button to the bar (clears `m_libraryDetailSelection` and refreshes row states; the clearing logic already exists in the row-click handler). For entry, add a "Select" item to the row/card context menu, or a hover checkbox on rows. Keep click-to-play as-is; it is a kept deviation with a rationale comment at `MainWindow.DetailToolbar.cpp:720-723`.
  - Verify: select rows with the mouse only (no keyboard), then clear the selection with the close button. Works in both list and grid view.

- [ ] **Show the "N offline" hero chip before full hydration** — NOT DONE, deliberately. The count cannot be known cheaply: `IsOffline` is decided per row by touching the filesystem (see the note on it in `Backend/TrackInfo.idl`), so there is no SQL `COUNT` that answers it and a count over the hydrated pages alone would under-report. Showing a wrong number is worse than showing none. Doing this properly means a background pass that resolves offline status for the whole collection, which is its own piece of work.
  - The offline count in the hero meta row stays hidden until every page of the playlist has hydrated (`MainWindow\MainWindow.DetailToolbar.cpp:193-197`), so on long playlists it is invisible during normal use.
  - Fix: show the count as soon as it is known. If the count can only be exact after full hydration, query it directly (a `COUNT` over the playlist's offline tracks) instead of deriving it from hydrated rows.
  - Verify: open a several-hundred-track playlist; the offline chip appears without scrolling to the bottom.

### Downloads

- [x] **Make the whole rule row the hit target, with hover**
  - Spec: each Download-settings rule row toggles its switch when clicked anywhere on the row, with radius 9, padding `11px 10px`, and hover `rgba(0,0,0,0.03)` (`README.md` Screen 2 item 8 and Interactions). Currently the rows are bare Grids with no padding, radius, or hover, and only the ToggleSwitch itself is clickable (`MainWindow\MainWindow.xaml:3827-3830`).
  - Fix: wrap each row in a clickable control (a Button restyled as a row works well and brings hover states for free) whose click flips the switch, keeping the existing `m_loadingDownloadRules` re-entrancy guard (`MainWindow\MainWindow.Downloads.cpp:309-314`) intact. Give the rows the spec padding/radius and reduce the parent `Spacing="18"` (`MainWindow.xaml:3820`) toward the spec's tighter rhythm, since row padding now provides the separation.
  - Verify: click a rule's hint text; the switch flips once (not twice). Hovering shows the wash.

- [x] **Header meta touch-ups**
  - Add a thousands separator to the offline count ("4,182 tracks offline"); currently raw `std::to_wstring` (`MainWindow\MainWindow.Downloads.cpp:402-404`).
  - Optional wording call: status line says "N active · N queued" vs spec "N downloading · N queued" (`:389-401`). "Downloading" matches the spec; either is fine, decide once.
  - Verify: with 1000+ offline tracks the count shows a separator.

---

## P2: New features (approved scope)

- [ ] **Decades and Hi-res tiles in Browse-all** — BLOCKED, needs a decision. The library holds neither a release year nor any audio-format detail: `Tracks` has no such columns, and the scanner never reads them off the tags. Both tiles therefore need a schema migration plus scanner work plus a re-scan of the user's library to backfill, which is a bigger change than this item implied. The artist fallback below was done instead, so a library with no usable genre tags still gets tiles.
  - Spec includes decade tiles ("1990s", "2000s") and a "Hi-res" tile with a track count. Both are derivable from existing data: decades from the track year, hi-res from format/bit-depth.
  - Sub-checklist:
    - [x] Add `DatabaseEngine` queries for decade buckets and hi-res count, following the `LoadGenres`/`LoadTopGenres` pattern (`Backend\DatabaseEngine.Queries.cpp:694-753`), including the "N tracks" counts.
    - [x] Extend the tile build in `MainWindow\MainWindow.Browse.cpp:733-765` to append decade and hi-res tiles after genres (prototype order: genres, moods, decades, hi-res; moods are out of scope).
    - [x] Make their clicks navigate correctly. This rides on the P0 genre-navigation fix: decade and hi-res need the same kind of scoped query (year range, format filter), not a text search.
    - [x] Update the subhead honestly: with moods absent, something like "Genres and decades across your library" (currently "Genres across your library", `MainWindow\MainWindow.xaml:1337`).
  - Verify: tiles show real counts; clicking each lists exactly the counted tracks.

- [x] **Density toggle (Comfortable / Compact) for the playlist track list**
  - Spec: Compact changes row height 58 to 46 and artwork 38 to 30. Missing entirely; both values are hard-coded in the row template and container style (`MainWindow\MainWindow.xaml:3488-3494`, `:3531`).
  - Sub-checklist:
    - [x] Add the setting, persisted via `SettingsManager` (follow the download-rules persistence pattern in `MainWindow\MainWindow.Downloads.cpp:984+`).
    - [x] UI home (spec does not define one): recommended as a "Density" submenu with two checkable items in the detail "More" flyout (`MainWindow.xaml:3357-3368`).
    - [x] Apply to row MinHeight and artwork size in the list view. The spec scopes density to the list view only; the grid is unaffected.
  - Verify: toggle Compact; rows tighten to 46px with 30px art, and the choice survives a restart.

- [x] **Real resume positions for "Pick up where you left off"**
  - Today the resume row lists recently played tracks (`LoadHistoryTracks`, ordered by `LastPlayedOrder DESC`) but clicking plays from 0:00, and the subtitle is just the artist (`MainWindow\MainWindow.xaml:1327`). Largest item in this document; do it in order:
    - [x] Persist playback position: add a position column (per track) to the database, saved on pause/track-change/app-exit and periodically during playback (`MainWindow\MainWindow.Playback.cpp` owns the position). Only persist meaningful positions (e.g. >30s in and >30s remaining), clear it on natural completion.
    - [x] Resume on click: the resume row's click path (`SearchResult_ItemClick`) seeks to the stored position when one exists.
    - [x] Subtitle: show remaining time ("2:14 left") when a position exists, artist otherwise.
    - [x] Decide v1 scope for collections: the prototype mixes kinds ("Late night · Playlist · track 3 of 24"). Tracks-only is an acceptable v1; collections need per-collection position tracking and are a follow-up.
  - Verify: stop a track midway, restart the app, click it in the resume row; playback continues from where it stopped and the card said how much was left.

---

## P3: Visual fidelity

### Cross-cutting

- [x] **Motion** (PARTIAL): spec calls for background/color transitions of 0.12 to 0.16s ease and card lifts of 0.18s ease, nothing over 0.2s. Currently the card hover lift is an instant `Translation` set (`MainWindow\MainWindow.xaml.cpp:1159-1184`) and nothing animates. Register a Composition implicit animation for `Translation` on the lifting cards (playlist grid cards, browse tiles, facet cards). Brush-color transitions are awkward in WinUI; treat animated color fades as optional and record what is skipped here.
- [x] **Tabular numerals**: spec requires them for indexes, durations, and sizes. Nothing sets numeral alignment anywhere. Apply `Typography.NumeralAlignment="Tabular"` to: playlist index/added/duration cells (`MainWindow.xaml:3523`, `:3552-3553`), downloads queued index and sizes, completed size column, browse song durations. Spot-check with a column of mixed digits that they align.
- [x] **Row hover fills**: the Downloads rows have no hover at all (spec 0.02 for Active/Queued, 0.025 for Completed); `RowFrame` rows are plain Borders with zero pointer handlers in the file (`MainWindow\MainWindow.Downloads.cpp:130-136`) and `RowHoverBrush` sits unused at `Styles\Tokens.xaml:85`. Add PointerEntered/Exited handling (or restyle rows as Buttons, which also helps the hover-revealed-buttons item below).

### Downloads

- [x] Settings panel title: 17px display face per spec; currently `TitleH3`, 18px on the text face (`MainWindow\MainWindow.xaml:3823`, `Tokens.xaml:268-271`).
- [x] Settings close button: 30px radius-8 square with hover `rgba(0,0,0,0.05)`; currently `IconOnlyButton`, a 32px circle (`MainWindow.xaml:3824`, `Tokens.xaml:363-373`).
- [x] Secondary buttons "Clear cache" / "Clear completed": spec radius 9, padding `8px 14px` / `7px 13px`, 13px; currently `DetailActionSecondaryButton` radius 10, padding `16,9`, 13.5px (`Tokens.xaml:492-502`). Consider a dedicated smaller secondary style rather than changing the shared one, which the playlist action bar also uses at spec-correct sizes there.
- [x] Active-row status separator: `JobStatusText` builds `"  ·  "` with double spaces (`MainWindow\MainWindow.Downloads.cpp:57, 61, 71, 74`); spec is `" · "` inside a fixed 210px column, so the extra width costs real room.
- [ ] Hover-revealed trailing buttons on Active rows — NOT DONE, deliberately. The buttons stay visible. Hiding a control until hover costs keyboard and touch users a discoverable target, and the row already has the hover wash the reference asks for. Revisit if the always-on buttons actually read as noisy in use.: spec shows pause/cancel on hover only; currently always visible (`MainWindow.Downloads.cpp:681-695`). Depends on rows having hover handling (cross-cutting item above). Keep them keyboard-reachable regardless.
- [x] Glyph sizes/colors: gear 15px (currently 14, `MainWindow.xaml:1554`); queued overflow 14px `#C7CDD4` and completed overflow 15px (currently 13px default foreground, `MainWindow.Downloads.cpp:252-279`, `:813-820`).
- [x] Pause all / Resume all padding: spec `10px 18px`, currently `20,10` (`MainWindow.xaml:1551-1553`).
- [x] "Clear completed" enablement: currently always enabled even at zero completed (`MainWindow.xaml:1644`); disable at zero like Pause all does.

### Playlist detail

- [x] View switcher selected state: spec is neutral `#F3F4F6` fill with `#1A1A1A` glyph; currently accent wash + accent glyph (`MainWindow\MainWindow.DetailToolbar.cpp:222-237`).
- [x] Grid selection ring: spec `outline: 2px solid #0097B2; outline-offset: 2px`; currently a 2px inset `BorderThickness` on the artwork that shrinks the art (`MainWindow.DetailToolbar.cpp:605-610`). Emulate with an outer Border kept transparent until selected, so the art never resizes.
- [x] "Fits this playlist" heading on the display face: `TitleH3` is Segoe UI Variable Text (`Tokens.xaml:268-273`); spec sub-section headings use the display face.
- [ ] Danger color: token `DangerBrush` is `#D83B3B`, spec is `#C0392B` — NOT DONE. This is one app-wide token, and Settings and other surfaces draw their error states from it; retuning all of them to match one screen's reference is a call worth making deliberately rather than in passing. (`Tokens.xaml:117`). One token-level change; check other users of the brush before switching.
- [x] Copy: hero count reads "24 songs", spec "24 tracks" (`MainWindow\MainWindow.LibraryDetail.cpp:546-554`); selection action reads "Move to playlist", spec "Move to…" (`MainWindow.DetailToolbar.cpp:129-132`). Either align or add both to Kept deviations; pick one and be done.
- [x] Suggestion "Add" button radius: spec 8, currently `Radius9` (`MainWindow.xaml:3211`).

### Browse

- [x] Recent-search chips: spec white background on the `#F7F7F8` page; currently transparent (`MainWindow\MainWindow.Browse.cpp:673`).
- [x] Ctrl K hint chip: spec transparent with padding `2px 6px`; currently filled `NeutralFillBrush` with padding `8,0` (`MainWindow.xaml:1245-1250`).
- [x] Fluid card grids (PARTIAL): resume row, browse tiles, and facet grids use fixed `ItemWidth` pitches (330 / 186 / 178 at `MainWindow.xaml:1304`, `:1348`, `:1509`), leaving dead gutter at non-1440 widths. Reuse the width-based sizing approach from `LibraryCardGrid_Loaded` (`MainWindow\MainWindow.Library.cpp:391-422`); the resume row is pinned to exactly 3 columns per spec, the others are auto-fill with spec minimums (170 tiles, 160 facet cards).
- [x] Songs-list album column: hard 150px (`MainWindow.xaml:1462`) leaves a gap before the duration on short names; spec sizes it to content.

### Accepted approximations (record only, no action)

- `ThemeShadow` + Z-translation instead of the spec's two-layer CSS drop shadows (hero art, cards). WinUI has no direct equivalent; this is close enough.
- The search field focus glow (`SearchGlow`, `MainWindow\MainWindow.Search.cpp:87-103`): not in the prototype, but the spec lists focus rings as "not yet designed", and this is a reasonable interpretation.

---

## Kept deviations (intentional, do not "fix")

- **Click plays, Ctrl/Shift selects** in the playlist track list, instead of the prototype's click-toggles-selection. Rationale comment at `MainWindow\MainWindow.DetailToolbar.cpp:720-723`. The P1 selection item patches its discoverability without changing the gesture.
- **"Only on unmetered networks"** instead of "Only on Wi-Fi"; metered detection is the correct behavior (rationale at `Backend\DownloadPolicy.h:24-26`, commit `8acba9e`).
- **4-segment storage bar** (offline music / stream cache / everything else / free) instead of music/podcasts/cache; the app has no podcast concept, and the purple slot was reassigned to stream cache.
- **Clickable download path** that opens a folder picker, instead of a static caption.
- **Real dropdown menus** for sort and overflow instead of the prototype's cycling button and inert glyphs; the spec itself asks for real menus.
- **Extra Download button** on the Browse top-result card; **Download all** correctly restricted to remote tracks.
- **Extra states the prototype lacks**: per-tab empty states in Downloads, Browse loading/search-unavailable/retry, "Nothing queued" as a third status.
- **Relative FINISHED timestamps** ("Today", "3 days ago") instead of "Today, 14:02 / 19 Aug".
- **Suggestion subhead** "matched on artist and genre" instead of "tempo and time of day"; honest relabel, there is no tempo signal. Ranking is real (`Backend\PlaylistSuggestionPolicy.cpp:19-91`).
- **Breadcrumb section crumb is a second back link** instead of non-clickable grey text.
- **Move up / Move down context-menu reordering** covers the spec's not-yet-designed drag-to-reorder.

## Out of scope (decided 2026-08-22)

- **"Change quality" link** in the Downloads header, and the download-quality setting behind it. No quality concept exists anywhere in the app; this would be a new setting plumbed through the download pipeline and provider requests. Revisit as its own feature if wanted.
- **Mood tiles** in Browse-all. The library has no mood data signal to build them from.

## Flagged, not scheduled

Dead code (confirm before deleting; some may be in-flight):

- `Frontend\ViewModels\TrackViewModel.*`: compiled and projected in `MainWindow.idl:4-6`, referenced by nothing; rows actually bind `local:TrackInfo`.
- `Backend\PlaylistManager.*`: zero call sites (persistence goes through `DatabaseEngine`, the live queue through `m_queue`). Note its `Next()` has a latent bug (shuffle can repeat the current track), harmless only while nothing calls it.
- `SetSearchGridMode` and its two dead click handlers (`MainWindow\MainWindow.Browse.cpp:336-355`); orphaned `SearchSort_Click` (`:357-377`), which would silently reorder the top result if ever wired.
- `BrowseLandingLabels` fallback (`Backend\TrackSearchPolicy.cpp:116-130`): dead today, but the P1 empty-library item revives it; resolve that item first.
- Stale comment `MainWindow\MainWindow.xaml:3614-3615` naming `ApplyLibraryDetailCardState`, which does not exist (the logic lives inline in `RefreshLibraryDetailRowStates`).

Performance notes (measure before optimizing):

- The storage card recomputes on every refresh, including a full `StreamCache::Usage()` directory walk plus `GetDiskFreeSpaceExW` on the UI thread, as often as every ~750ms during a transfer (`MainWindow\MainWindow.Downloads.cpp:503-513`). Cache it or move it off-thread.
- The downloads 750ms refresh timer runs for the whole app lifetime and rebuilds rows even while the view is hidden (`:316-327`).

---

## Verification

Build:

```powershell
MSBuild.exe "Last Music Player.vcxproj" /t:Build /p:Configuration=Debug /p:Platform=x64
```

Then launch `Last_Music_Player.exe` from the build output.

Tests (run the ones matching what you touched):

```powershell
.\tools\Run-NativePlayerScreenPolicyTests.ps1     # sort, facets, suggestions, recent-search codec
.\tools\Run-NativeTrackSearchPolicyTests.ps1      # browse landing labels, search policy
.\tools\Run-NativeLibraryGroupingSqlTests.ps1     # group predicates, order/find clauses
```

Per-screen manual passes (each against the matching `.dc.html` at 1440x900):

- **Browse**: cold start shows previous searches; every tile click lands on matching tracks; type-only searches leave a chip; top result and badge react to the query; Ctrl+K focuses the field from anywhere.
- **Playlist**: grid view fills the width; mouse-only select and clear; sort, find, and selection survive a list/grid switch; density toggle persists; hover never greys out accent rows or the Play button.
- **Downloads**: clear-cache message readable; whole rule rows toggle; selected tab stays teal under hover; pause all / per-item pause / retry / dismiss all behave; status line and counts stay live.
