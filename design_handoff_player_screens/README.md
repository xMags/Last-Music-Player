# Handoff: Last Music Player — Playlist, Downloads, Browse

## Overview
Three desktop screens for Last Music Player, a local-first desktop music app (Windows-style chrome, Fluent icon set). They extend an existing redesign of the Library and Settings screens and share one app shell:

- **Playlist detail** — the inside of a playlist: hero, actions, track list/grid, suggestions.
- **Downloads** — offline download manager: storage, active/queued/completed/failed, download rules.
- **Browse (search)** — search entry point with a browse-all state and a results state.

## About the Design Files
The files in this bundle are **design references created in HTML** — prototypes that show the intended look and behavior. They are not production code to copy directly.

The task is to **recreate these designs in the target codebase's existing environment** (React, Vue, Electron + web, SwiftUI, WinUI, etc.) using its established components, tokens, and patterns. If no environment exists yet, choose the most appropriate framework for a desktop music player and implement the designs there.

Each `.dc.html` file opens directly in a browser. `support.js` is only the runtime that makes these prototype files render — do not port it.

## Fidelity
**High-fidelity.** Colors, typography, spacing, radii, hover states, and interaction behavior are final and should be matched closely. All values below are exact.

One caveat: album/playlist artwork is represented by **striped gradient placeholders**. Real artwork goes in those slots; the gradients are not part of the design language beyond acting as artwork stand-ins.

---

## Shared app shell (all three screens)

Canvas: **1440 × 900**, fixed. Background `#F3F3F3`. Base font size 14px.

Font stack: `'Segoe UI Variable Text', 'Segoe UI Variable', 'Segoe UI', system-ui, sans-serif`.
Display font (page titles, hero titles): `'Segoe UI Variable Display', 'Segoe UI', sans-serif`.
Icon font: `'Segoe Fluent Icons', 'Segoe MDL2 Assets'` — glyph code points listed per component below.

Layout, top to bottom:

1. **Title bar** — absolute, full width, height 48px, transparent, `pointer-events: none`. App mark 26×26, radius 6, `linear-gradient(135deg,#0097B2,#00B8D4)`, white ♪ glyph 14px. App name "Last Music Player" 12px/600, 10px gap, 16px left padding.
2. **Body row** — flex, fills remaining height:
   - **Left nav** 240px, right border `1px solid rgba(0,0,0,0.06)`.
   - **Content** flex:1, background `#F7F7F8`, own vertical scroll, horizontal padding 20px, top padding 60–64px (clears the title bar).
   - **Right rail** 300px, `flex: none`, background `#fff`, left border `1px solid rgba(0,0,0,0.06)`, top padding 48px.
3. **Player bar** — height 90px, `flex: none`, background `#fff`, top border `1px solid rgba(0,0,0,0.06)`, horizontal padding 24px.

### Left nav
- 50px spacer, then 20px top / 16px horizontal padding.
- Section labels ("MENU", "PLAYLISTS"): 11px, weight 700, letter-spacing 0.16em, `#8A8A8A`. "MENU" has 10px bottom margin; "PLAYLISTS" has margin `24px 0 10px`.
- Nav items: height 42px, radius 8px, padding `0 12px`, flex row, gap 20px. Icon 16px `#5C5C5C`, label 14px `#1A1A1A`.
- Active item: background `rgba(0,151,178,0.12)`, icon and label `#0097B2`, label weight 600.
- Nav icons: Home `\uE80F`, Browse `\uE721`, Library `\uE8D6`, Downloads `\uE896`, Settings `\uE713`.
- Downloads carries a count badge: 11px/700, radius 999px, padding `1px 7px`. Active: white on `#0097B2`. Inactive: `#8A8A8A` on `rgba(0,0,0,0.05)`.
- Playlist rows: min-height 42px, padding `9px 12px`, gap 16px; 24×24 radius-4 swatch (heart glyph `\uEB51` on `#6B8EFE` for Liked Songs; `#` on `#F3F4F6` in `#8A8A8A` otherwise). Active playlist row uses the same active treatment as nav items and a gradient swatch.
- Footer: top border `1px solid rgba(0,0,0,0.06)`, padding `20px 16px`, 40px circle with `1.5px solid #0097B2` and person glyph `\uE77B` 22px `#0097B2`, name "Listener" 14px/600.

### Right rail — player (default on all three screens)
- Segmented "Up Next / Lyrics": margin `24px 24px 16px`, radius 8, background `#F3F4F6`, padding 3px. Selected half: `#fff`, `1px solid rgba(0,0,0,0.06)`, radius 6, padding `7px 0`, 12px/600. Unselected: same size, `#5C5C5C`.
- Body padding `8px 24px 24px`.
- "PLAYING FROM" label (11px/700/0.16em/`#8A8A8A`, 12px bottom margin), then a 36px radius-6 art swatch + playlist name 13.5px/600 + "Track 3 of 24" 12px `#8A8A8A`. 22px bottom margin.
- "NEXT IN QUEUE" label, then queue rows: radius 8, padding `7px 8px`, gap 11px, hover `rgba(0,0,0,0.03)`; 34px radius-5 art, title 13px/600, artist 11.5px `#8A8A8A`, trailing drag glyph `\uE700` 12px `#C7CDD4`.

### Player bar
Three zones: 300px / flex:1 / 300px.
- Left: 52px radius-8 artwork, 16px right margin; title 14px/600, artist 12px `#8A8A8A`; heart `\uEB51` 16px `#5C5C5C` with 16px left margin.
- Center: 400px column. Transport row, gap 24px, 8px bottom margin — shuffle `\uE8B1` 16px `#8A8A8A`, previous `\uEB9E` 20px `#5C5C5C`, play/pause 40px circle `#1A202C` with white glyph 16px (`\uE768` play / `\uE769` pause), next `\uE893` 20px `#5C5C5C`, repeat `\uE8EE` 16px `#8A8A8A`. Scrubber row: gap 12px, times 11px `#8A8A8A`, track height 4px radius 2 `rgba(0,0,0,0.10)`, fill `#0097B2`.
- Right: volume `\uE71D` 16px `#5C5C5C`, device `\uF4C3` 16px at 0.5 opacity, gap 14px.

---

## Screen 1 — Playlist detail (`Playlist - Redesign.dc.html`)

**Purpose:** play, search, reorder, curate and extend one playlist.

**Layout:** content column padded `64px 20px 0`, single scroll.

1. **Breadcrumb** — 12px, gap 8px, 18px bottom margin. "Library" (600, `#0097B2`, clickable) › chevron `\uE76C` 9px › "Playlists" `#8A8A8A` › chevron › current name `#5C5C5C`.
2. **Hero** — flex row, gap 24px, `align-items: flex-end`, 24px bottom margin.
   - Cover 180×180, radius 16, shadow `0 1px 2px rgba(0,0,0,0.06), 0 18px 34px -20px rgba(0,0,0,0.45)`; stripe overlay `repeating-linear-gradient(135deg, rgba(255,255,255,0.16) 0 1px, transparent 1px 9px)`; monospace caption "playlist art" 9.5px at bottom-left.
   - Right column, gap 6px: "PLAYLIST" eyebrow (11px/700/0.16em/`#8A8A8A`) + source badge "THIS PC" (9px/700, `#0097B2` on `rgba(0,151,178,0.12)`, radius 999, padding `2px 7px`); title display font 40px/700, line-height 1.05, letter-spacing -0.01em; description 13.5px `#5C5C5C`, max-width 560px, `text-wrap: pretty`; meta row 13px `#5C5C5C` with 3px dot separators (`#C7CDD4`): "24 tracks", "1 h 38 min", "22 offline", "edited 3 days ago" (`#8A8A8A`).
3. **Action bar** — flex row, gap 10px, 16px bottom padding, bottom border `1px solid rgba(0,0,0,0.07)`.
   - Primary Play/Pause: `#0078D4`, white, radius 10, padding `10px 20px`, 13.5px/600, gap 9px, hover `#0069b8`, shadow `0 1px 2px rgba(0,0,0,0.10)`. Glyph toggles `\uE768`/`\uE769`, label toggles "Play"/"Pause".
   - Shuffle: white, `1px solid rgba(0,0,0,0.08)`, radius 10, padding `9px 16px`, 13.5px/600, glyph `\uE8B1`, hover `rgba(0,0,0,0.04)`.
   - Icon buttons 38×38, same border/radius/hover: download all `\uE896`, edit `\uE70F`, more `\uE712`.
   - Spacer, then find-in-playlist field: 190px, height 38, white, `1px soli rgba(0,0,0,0.08)`, radius 10, search glyph `\uE721` 13px `#8A8A8A`, borderless input 13px, placeholder "Find in playlist".
   - Sort button: height 38, radius 10, padding `0 12px`, 13px/600, glyphs `\uE8CB` leading and `\uE70D` trailing. Cycles: Custom order → Recently added → Title → Artist → Duration.
   - View switcher: white, `1px solid rgba(0,0,0,0.08)`, radius 10, padding 3px, height 38; two 38px-wide buttons, radius 7, gap 2px. List `\uEA37`, grid `\uE80A`. Selected: background `#F3F4F6`, glyph `#1A1A1A`; unselected glyph `#8A8A8A`.
4. **Selection bar** — appears only when ≥1 track is selected. Margin-top 14, background `rgba(0,151,178,0.10)`, `1px solid rgba(0,151,178,0.28)`, radius 10, padding `9px 14px`, gap 14, 13px. "{n} selected" (600, `#0097B2`), 1px divider, actions "Add to queue", "Move to…", "Remove" (`#C0392B`), spacer, close `\uE711` 12px `#8A8A8A` clears selection.
5. **Track list (list view)** — card: white, `1px solid rgba(0,0,0,0.06)`, radius 12, shadow `0 1px 2px rgba(0,0,0,0.04)`, margin-top 18.
   - Header row: grid `40px 1fr 150px 84px 56px 40px`, gap 12, padding `11px 18px`, background `#FBFBFD`, bottom border `1px solid rgba(0,0,0,0.06)`; labels 11px/700/0.14em `#8A8A8A`: `#`, TITLE, ALBUM, ADDED, TIME (right), blank.
   - Rows: same grid, height 58px (46px in Compact density), padding `0 18px`, bottom border `1px solid rgba(0,0,0,0.04)`, hover `rgba(0,0,0,0.025)`.
   - Cells: index tabular-nums `#8A8A8A`; artwork 38px (30px compact) radius 6; title 14px/600 + artist 12px `#8A8A8A` with offline check `\uE73E` 10px `#0097B2`; album 13px `#5C5C5C`; added 13px `#8A8A8A`; duration 13px right, tabular-nums; like glyph `\uEB52` filled `#0097B2` / `\uEB51` outline `#C7CDD4`.
   - Now-playing row: background `rgba(0,151,178,0.10)`, index replaced by play glyph `\uE768` `#0097B2`, title `#0097B2`.
   - Selected row: background `rgba(0,151,178,0.08)`.
   - Empty search result: 44px vertical padding, centered 13.5px `#8A8A8A`, `No tracks match “{query}”`.
6. **Track grid (grid view)** — `repeat(auto-fill, minmax(150px, 1fr))`, gap `20px 18px`. Card art aspect 1:1, radius 14, shadow `0 1px 2px rgba(0,0,0,0.06), 0 10px 20px -16px rgba(0,0,0,0.35)`; hover lifts card `translateY(-3px)` over 0.18s ease. Now playing: white pill badge "PLAYING" (10px/700/0.08em, `#0097B2`) top-left. Duration bottom-right 11px `rgba(255,255,255,0.9)`. Selected: `outline: 2px solid #0097B2; outline-offset: 2px`. Below art: title 14px/600 (turns `#0097B2` when playing), artist 12px `#8A8A8A` + offline check.
7. **"Fits this playlist"** — heading 18px/700 display font + subhead "From your library, matched on tempo and time of day" 12.5px `#8A8A8A`, margin `26px 0 12px`. Rows: radius 10, padding `9px 12px`, gap 12, hover `rgba(0,0,0,0.03)`; 38px art, title 14px/600, artist 12px `#8A8A8A`, duration 12px, and an "Add" button (`1px solid rgba(0,0,0,0.08)`, radius 8, padding `5px 12px`, 12.5px/600, glyph `\uE710`).

**Right rail** uses the shared player rail.

---

## Screen 2 — Downloads (`Downloads - Redesign.dc.html`)

**Purpose:** see and control what is being kept offline, and how much space it costs.

1. **Header** — flex, `align-items: flex-end`, gap 20. Left: "OFFLINE" eyebrow, "Downloads" 32px/700 display font (line-height 1.1), meta row 13px `#5C5C5C` with dot separators — status line ("3 downloading · 4 queued", or "All downloads paused"), "4,182 tracks offline", "FLAC, up to 24-bit" (`#8A8A8A`), and a "Change quality" link (600, `#0097B2`).
   Right: Pause all / Resume all primary button (same style as playlist's primary, padding `10px 18px`, glyph `\uE769`/`\uE768`), then the **gear button** 38×38 radius 10 glyph `\uE713`. Gear default: white, `1px solid rgba(0,0,0,0.08)`, glyph `#5C5C5C`. Gear active (settings open): background `rgba(0,151,178,0.12)`, border `1px solid rgba(0,151,178,0.30)`, glyph `#0097B2`.
2. **Storage card** — margin-top 24, white, `1px solid rgba(0,0,0,0.06)`, radius 12, shadow `0 1px 2px rgba(0,0,0,0.04)`, padding `18px 20px`, flex row gap 32.
   - Left: "STORAGE" label + "62.4 GB of 128 GB used on this PC" 12.5px `#8A8A8A` (both `white-space: nowrap`, `flex: none`). Stacked bar height 10, radius 6, track `rgba(0,0,0,0.07)`, segments 34% `#0097B2` (music), 9% `oklch(0.72 0.11 288)` (podcasts), 6% `rgba(0,0,0,0.16)` (cache). Legend row gap 20, 12.5px `#5C5C5C`, 8px radius-2 swatches, each item nowrap.
   - Right: "Clear cache" button (`1px solid rgba(0,0,0,0.08)`, radius 9, padding `8px 14px`, 13px/600) and the download path `D:\Music\Offline` 11.5px `#8A8A8A`.
3. **Tab row** — margin `26px 0 16px`, bottom border `1px solid rgba(0,0,0,0.07)`, 12px bottom padding, gap 6. Chips: radius 14, padding `6px 11px`, 13px/600, count badge inside. Active: `#0097B2` background, white text, badge `rgba(255,255,255,0.22)` / white. Inactive: transparent, `#5C5C5C`, `1px solid rgba(0,0,0,0.06)`, badge `rgba(0,0,0,0.05)` / `#8A8A8A`. Tabs and counts: Active 3, Queued 4, Completed 6, Failed 2. Right-aligned "Clear completed" button (white, `1px solid rgba(0,0,0,0.08)`, radius 9, padding `7px 13px`, 13px/600).
4. **Active tab** — card (white, radius 12, hairline border, subtle shadow). Rows padding `14px 18px`, gap 14, bottom border `1px solid rgba(0,0,0,0.04)`, hover `rgba(0,0,0,0.02)`. 44px radius-7 artwork; title 14px/600 with a 12px `#8A8A8A` subtitle on the same baseline row ("Album · Tanu Sen · 11 tracks"); below it a progress row: bar height 5 radius 3, track `rgba(0,0,0,0.08)`, fill `#0097B2` (paused: `rgba(0,0,0,0.22)`), and a right-aligned 210px status string 12px `#8A8A8A` ("7 of 11 · 4.8 MB/s · 2 min left", paused: "Paused · 12%"). Trailing 34px hover buttons: pause/resume (`\uE769`/`\uE768`) and cancel (`\uE711`).
5. **Queued tab** — rows padding `12px 18px`: 26px index (13px `#8A8A8A`, tabular), 38px art, title/subtitle, size 12.5px `#8A8A8A`, overflow `\uE712` `#C7CDD4`.
6. **Completed tab** — table, header grid `1fr 160px 110px 90px 40px` with labels ITEM / ALBUM / FINISHED / SIZE (right) / blank; rows height 58px, artwork 38px, offline check `\uE73E` after the subtitle, overflow `\uE712`.
7. **Failed tab** — rows padding `14px 18px`: 38px radius-6 `#F3F4F6` tile with error glyph `\uE783` 15px `#C0392B`; title 14px/600; reason 12px `#C0392B`; "Retry" button (`1px solid rgba(0,0,0,0.08)`, radius 8, padding `6px 14px`, 12.5px/600); dismiss `\uE711`.
8. **Right rail** — the shared player rail is the **default**. Clicking the gear replaces it with the **Download settings** panel; the panel's close button (`\uE711`, 30px, hover `rgba(0,0,0,0.05)`) returns to the player rail. Only one of the two is mounted at a time; rail width stays 300px so the content column never reflows.
   Settings panel: title "Download settings" 17px/700 display font, 22px bottom margin. "DOWNLOAD RULES" label, then four toggle rows (radius 9, padding `11px 10px`, gap 12, hover `rgba(0,0,0,0.03)`): label 13.5px/600, hint 11.5px `#8A8A8A` `text-wrap: pretty`, and a switch 38×20 radius 10 padding 2 — on `#0097B2` with knob right, off `rgba(0,0,0,0.18)` with knob left, knob 16px white with shadow `0 1px 2px rgba(0,0,0,0.25)`, transition 0.16s.
   Rules: "Only on Wi-Fi" (on) / "Auto-download liked songs" (on) / "Download on battery" (off) / "Keep recently played offline" (on). Then a 1px divider (`rgba(0,0,0,0.06)`, margin `22px 0`) and "THIS SESSION" stats rows (13px, label `#5C5C5C`, value 600): Downloaded 18 tracks, Transferred 742 MB, Average speed 4.8 MB/s.

---

## Screen 3 — Browse / search (`Browse - Redesign.dc.html`)

**Purpose:** one search field that serves both library and catalog, with a useful zero-query state.

1. **Search header** — `flex: none`, padding `60px 20px 0`, row gap 12.
   - Field: flex:1, max-width 620px, height 46, white, `1px solid rgba(0,0,0,0.10)`, radius 12, padding `0 14px`, shadow `0 1px 2px rgba(0,0,0,0.04)`, gap 11. Search glyph `\uE721` 15px `#8A8A8A`; input 15px, placeholder "Search your library and the catalog"; clear `\uE711` 12px appears once there is a query; trailing "Ctrl K" hint 11px `#8A8A8A` in a `1px solid rgba(0,0,0,0.10)` radius-5 chip.
   - Scope switcher (Library / Catalog): white, `1px solid rgba(0,0,0,0.08)`, radius 12, padding 3, height 46; options radius 9, padding `0 14px`, height 38, 13px/600. Selected `#F3F4F6` / `#1A1A1A`, unselected transparent / `#8A8A8A`. Catalog widens results beyond in-library tracks.
2. **Zero-query state** (scrolling body, padding `22px 20px 0`)
   - "RECENT SEARCHES" label in a row with a 1px rule and a "Clear" link (12px/600 `#0097B2`), 12px bottom margin. Chips: white, `1px solid rgba(0,0,0,0.08)`, radius 999, padding `7px 14px`, 13px, history glyph `\uE823` 11px, gap 9, hover `rgba(0,0,0,0.04)`. Clicking a chip runs that search. 32px bottom margin.
   - "Pick up where you left off" 20px/700 display font, then a 3-column grid (gap 12): white cards, `1px solid rgba(0,0,0,0.06)`, radius 12, padding 12, gap 13, 52px radius-8 art, title 14px/600, subtitle 12px `#8A8A8A`, trailing play `\uE768` 13px `#0097B2`, hover `rgba(0,0,0,0.03)`. 34px bottom margin.
   - "Browse all" 20px/700 + subhead "Genres, moods and decades across your library" 12.5px `#8A8A8A`. Tile grid `repeat(auto-fill, minmax(170px, 1fr))`, gap 16; tiles aspect 1.5, radius 14, gradient artwork with stripe overlay, hover `translateY(-3px)`; label 16px/700 white top-left (14px/13px inset), sub-label 11.5px `rgba(255,255,255,0.86)` bottom-left. 12 tiles: 6 genres (Indie 412 tracks, Classical 188, Electronic 506, Ghazal 94, Jazz 132, Lo-fi 221), 3 moods (Late night, Focus, Monsoon), 2 decades (1990s, 2000s), Hi-res (318 tracks).
3. **Results state** (any non-empty query)
   - Type filter chips (All / Songs / Albums / Artists / Playlists): same chip styling as the Downloads tabs without counts; bottom border `1px solid rgba(0,0,0,0.07)`, 12px padding, 20px bottom margin.
   - **Top result + songs**, grid `340px 1fr`, gap 20, 30px bottom margin (shown for All and Songs).
     - Top result card: white, `1px solid rgba(0,0,0,0.06)`, radius 14, padding 18, hover `rgba(0,0,0,0.03)`. 96px radius-10 art, title 22px/700 display font, kind badge ("SONG"/"ALBUM") 10px/700 `#0097B2` on `rgba(0,151,178,0.12)`, subtitle 12.5px `#5C5C5C`, then "Play" primary (`#0078D4`, radius 9, padding `8px 16px`, glyph `\uE768`) and "Add to playlist" secondary.
     - Songs list: white card radius 12; rows height 56, padding `0 16px`, gap 13, hover `rgba(0,0,0,0.025)`; 38px art, title 14px/600, artist 12px `#8A8A8A` with in-library check `\uE73E` `#0097B2`, album 12.5px `#8A8A8A`, duration 12.5px right in a 42px column, overflow `\uE712`. Max 5 rows.
   - **Card grid** — `repeat(auto-fill, minmax(160px, 1fr))`, gap `20px 18px`, heading label above it (ALBUMS / ARTISTS / PLAYLISTS). Cards: art aspect 1:1, radius **12px** for albums and playlists, **50%** for artists; hover `translateY(-3px)`; title 14px/600, subtitle 12px `#8A8A8A`. The "All" filter shows albums.
   - **Empty state** — 70px vertical padding, centered: `Nothing for “{query}”` 19px/700 display font, then "Check the spelling, or search the full catalog instead of your library." 13.5px `#8A8A8A`.

---

## Interactions & Behavior

**Playlist**
- Play/Pause toggles both the hero button and the player-bar button from one state value.
- Sort button cycles the five sort modes on click (a real implementation should use a dropdown menu).
- Find-in-playlist filters on title, artist and album, case-insensitive, live per keystroke.
- Clicking a row (list) or card (grid) toggles selection; the selection bar appears at ≥1 and its close button clears all.
- Search, sort and selection persist across list ⇄ grid switches.
- Density (Comfortable / Compact) changes row height 58→46 and artwork 38→30.

**Downloads**
- Pause all overrides every item's state and rewrites the header status line.
- Per-item pause/resume swaps the bar fill to grey and the status to "Paused · {pct}%".
- Tabs swap the body region only; the header and storage card stay put.
- Gear toggles the right rail between player and Download settings; close returns to player.
- Each rule row toggles its switch (whole row is the hit target).

**Browse**
- Empty field → browse state; any non-empty field → results state.
- Recent-search chips set the query and reset the filter to All.
- Scope Library filters results to in-library items; Catalog includes everything.
- Type filter changes which sections render and the card shape (artists become circles).

**Transitions** — backgrounds and colors `0.12–0.16s ease`; card lifts `transform 0.18s ease`; toggle knob `0.16s ease`. Nothing longer than 0.2s.

**Not yet designed** (call out before building): drag-to-reorder affordance in the playlist list, sort and overflow menus as real popovers, keyboard focus rings, responsive behavior below 1440px, and the Lyrics tab of the right rail.

## State Management
Per screen, the state the prototypes hold:

- **Playlist** — `query: string`, `sortIndex: 0–4`, `playing: boolean`, `selected: number[]`, `view: 'list' | 'grid'`; props `density: 'Comfortable' | 'Compact'`, `showSuggestions: boolean`.
- **Downloads** — `tab: 'Active' | 'Queued' | 'Completed' | 'Failed'`, `allPaused: boolean`, `paused: Record<index, boolean>`, `rules: { wifi, autoLiked, battery, keepRecent }`, `settingsOpen: boolean` (player rail shows when false).
- **Browse** — `query: string`, `scope: 'Library' | 'Catalog'`, `filter: 'All' | 'Songs' | 'Albums' | 'Artists' | 'Playlists'`.

Data needs: playlist metadata + tracks (title, artist, album, dateAdded, duration, offline, liked, order); download jobs (kind, title, subtitle, progress, bytesTotal, speed, eta, state, error); storage totals by category; search across local index and remote catalog with an `inLibrary` flag per result; recently played for the resume row.

## Design Tokens

Colors
- Accent / brand: `#0097B2`; hover/lighter `#00B8D4`; accent wash `rgba(0,151,178,0.12)`, selection wash `rgba(0,151,178,0.08)`, now-playing wash `rgba(0,151,178,0.10)`, accent border `rgba(0,151,178,0.28–0.30)`
- Primary button: `#0078D4`, hover `#0069b8`
- Text: primary `#1A1A1A`, secondary `#5C5C5C`, tertiary `#8A8A8A`, faint `#C7CDD4`
- Surfaces: window `#F3F3F3`, content `#F7F7F8`, card `#fff`, table header `#FBFBFD`, inset `#F3F4F6`
- Lines: hairline `rgba(0,0,0,0.06)`, row divider `rgba(0,0,0,0.04)`, section rule `rgba(0,0,0,0.07)`, control border `rgba(0,0,0,0.08)`, field border `rgba(0,0,0,0.10)`
- Hover fills: `rgba(0,0,0,0.02)`, `rgba(0,0,0,0.025)`, `rgba(0,0,0,0.03)`, `rgba(0,0,0,0.04)`, `rgba(0,0,0,0.05)`
- Error: `#C0392B`; transport knob `#1A202C`; Liked Songs swatch `#6B8EFE`
- Placeholder artwork: `linear-gradient(150deg, oklch(0.62 0.13 H), oklch(0.72 0.11 H+26))` with hues cycling `196, 262, 22, 152, 318, 232, 46, 178, 288`; stripe overlay `repeating-linear-gradient(135deg, rgba(255,255,255,0.16–0.18) 0 1px, transparent 1px 8–10px)`

Typography
- Page title 32/700 · hero title 40/700 (-0.01em) · section title 20/700 · sub-section 18/700 · panel title 17/700 — all display font
- Body 14 · body strong 14/600 · control 13.5/600 · secondary 13 · small 12.5 · caption 12 · micro 11.5 · label 11/700/0.16em · table label 11/700/0.14em · badge 9–10/700/0.06–0.08em
- Tabular numerals for indexes, durations and sizes

Spacing — 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 32, 34px
Radius — 2 (dots), 5, 6, 7, 8, 9, 10, 12, 14, 16 (hero art), 999 (pills)
Shadows — control `0 1px 2px rgba(0,0,0,0.10)`; card `0 1px 2px rgba(0,0,0,0.04)`; art `0 1px 2px rgba(0,0,0,0.06), 0 10px 20px -16px rgba(0,0,0,0.35)`; hero art `0 1px 2px rgba(0,0,0,0.06), 0 18px 34px -20px rgba(0,0,0,0.45)`; toggle knob `0 1px 2px rgba(0,0,0,0.25)`
Fixed sizes — nav 240 · rail 300 · title bar 48 · player bar 90 · control height 38 · search height 46 · row 58 (46 compact)

## Assets
No bitmap or vector assets are bundled. All iconography is the Windows **Segoe Fluent Icons** font referenced by code point (listed inline above); substitute the target platform's icon set if that font is not available. All artwork is a placeholder gradient — replace with real album/playlist art.

## Files
- `Playlist - Redesign.dc.html` — playlist detail
- `Downloads - Redesign.dc.html` — downloads manager
- `Browse - Redesign.dc.html` — browse / search
- `support.js` — prototype runtime only; not part of the design and not to be ported

Related screens from the same redesign, not included here: `Library - Redesign.dc.html`, `Settings - Redesign.dc.html` (ask for them if the shell needs cross-checking).
