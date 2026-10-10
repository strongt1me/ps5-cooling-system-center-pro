# Changelog

## v1.59.0

### Fan Tile
- New background artwork with an airflow that glows in the colour of the measured fan speed
- Speed colours now fade smoothly from blue (up to 25 %) to green (55 %), yellow (75 %, new) and red (from 95 %); needle, number and tile border follow the same colours
- The gauge is now cut out cleanly along its shape and has a soft halo in the same colour, so it blends into the artwork
- "Fan", "%" and "measured speed" are brighter and easier to read on the background
- The number now counts up and down together with the needle instead of jumping straight to the new value

### Running Game Tile
- New background artwork
- Text sits in the top left corner, the controller in the bottom right corner directly on the artwork, both with the same distance to the edge

### File Manager
- Renamed the sidebar button from "Files" to "File manager"
- Larger page title and path line
- Moved "Refresh" into the toolbar next to "Upload"

### Bug Fixes
- Fixed the needle and number stopping at an old value when the fan tile left the screen during an animation

---

## v1.58.0

- Removed "Saved avatars" together with its function (save, load and delete avatar packs and the API endpoints `/api/v1/profile/avatar/library*`); packs already saved in `/data/PS5-Cooling-Center/Avatars` stay on the console
- Larger "Display name" card on the profile page
- Gauge and text are centred as a pair in the fan tile

## v1.57.0

- Own round file picker ("Choose file", "No file chosen.") in the app's language instead of the browser's English, square control
- Round checkboxes and range slider in the console's browser
- Removed the fan animation selector; the gauge always runs in full motion
- Fan tile text is centred and larger; more space between controller and text in the running game tile

### Bug Fixes
- Quick choice (Cool / Balanced / Quiet) now applies immediately even if the slider was touched before
- Loading or deleting a saved avatar now uses the pack selected in the list, not a leftover name in the name field

## v1.56.0

- Replaced the spinning fan with a speedometer gauge (artwork by the user); the needle moves smoothly to every new value and nothing runs in between, which fixes stutter while zooming and scrolling in the console's browser
- Installed games (`app.pkg`) are now labelled "Installed" instead of "Image"; "Open path", "Move" and "Copy" are disabled for them

## v1.55.0

- Install queue for packages: queue several packages and install them one after another, with pause and cancel; every package is checked when its turn comes
- Games page: new header with artwork, "Refresh" next to the search bar, and "Info & metadata" opens as a floating window
- New "Open path" button on every game card opens its location in the file manager
- Banners now fill the full tile width

---

Older versions: see the [GitHub releases](https://github.com/strongt1me/ps5-cooling-system-center-pro/releases).
