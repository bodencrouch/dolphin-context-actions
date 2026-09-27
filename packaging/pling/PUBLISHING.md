# Publishing to store.kde.org (Dolphin's "Download New Services…")

Dolphin's dialog reads category **102 – Dolphin Service Menus** from
store.kde.org through the OCS API (`/usr/share/knsrcfiles/servicemenu.knsrc`
→ `api.kde-look.org`). Getting listed means having a product in that
category with a payload archive Dolphin's `servicemenuinstaller` can run.

This repo ships three payloads. Dolphin lists one row per product, so
convert, archive, and link are three Add Product clicks. CI can upload
files after those pages exist. It cannot create the pages.

| Store name | First description line | Payload | Metadata |
|---|---|---|---|
| Dolphin Context Actions | Convert a file from the right-click menu. | `dist/dolphin-context-actions-servicemenu-v*.tar.gz` | `packaging/pling/context-actions/` |
| Dolphin Archive | Unpack or compress from the right-click menu. | `dist/dolphin-archive-servicemenu-v*.tar.gz` | `packaging/pling/archive/` |
| Dolphin Link | Hardlink or symlink from the right-click menu. | `dist/dolphin-link-servicemenu-v*.tar.gz` | `packaging/pling/link/` |

Build a payload with `scripts/build-ghns-package.sh <product>` where
product is `context-actions`, `archive`, or `link`.

## One-time setup (manual, ~15 minutes)

1. Create an account at <https://www.opendesktop.org> (same login works on
   store.kde.org). Username: `th3w1zard1`. Display name: **Boden Crouch**.
2. On <https://store.kde.org>, click **Add Product** three times, once per
   row in the table above. Category is **Dolphin Service Menus** every time.
3. Fill in each product:
   - **Name:** the store name from the table
   - **Description:** paste that product's `description.txt` (keep the first
     line as the pitch Dolphin shows in the dialog)
   - **License:** MIT
   - **Link to source:** the GitHub repo URL
   - **Preview images:** upload `promo/preview-1-hero.png`,
     `preview-2-menu.png`, `preview-3-formats.png`. The first image is the
     thumbnail users see inside Dolphin — keep it first.
4. Under **Files**, upload that product's tarball from the table.
5. Save. Note the number in the URL: `https://store.kde.org/p/<ID>`. Put
   those three numbers into GitHub secrets
   `PLING_CONTENT_ID_CONTEXT_ACTIONS`, `PLING_CONTENT_ID_ARCHIVE`, and
   `PLING_CONTENT_ID_LINK`. (`PLING_CONTENT_ID` still works as an alias for
   the convert product.)

Dolphin shows the three rows after the store cache refreshes:
Settings → Configure Dolphin → Context Menu → **Download New Services…**

## Every release after that (automated)

Add these repository secrets on GitHub
(Settings → Secrets and variables → Actions):

| Secret | Value |
|---|---|
| `PLING_USERNAME` | OpenDesktop login (`th3w1zard1`) |
| `PLING_PASSWORD` | its password |
| `PLING_CONTENT_ID_CONTEXT_ACTIONS` | convert product number from `/p/<ID>` |
| `PLING_CONTENT_ID_ARCHIVE` | archive product number |
| `PLING_CONTENT_ID_LINK` | link product number |
| `PLING_CONTENT_ID` | optional alias for the convert product |

When release-please cuts a release, the `publish-store` job builds all
three payloads and calls `scripts/publish-to-pling.sh` once per tarball
(`content/edit` for the version string, `content/uploaddownload` for the
file). Missing username/password skips the whole job with a notice.
Missing a content-ID secret skips that product only.

The OCS write API is legacy. If it ever refuses (statuscode ≠ 100), fall
back to uploading the tarball from the release page by hand — same file,
same place.

## What users see, and what makes them click

The Dolphin dialog shows: first preview image, product name, author, the
first line of the description, star rating, download count. That first
line is the whole pitch; keep it under ~60 characters and benefit-first.
Ratings come from users on the store page, so the GitHub README links to
the store and asks happy users to rate it there.


## Published content IDs (th3w1zard1)

| Product | URL |
|---|---|
| Dolphin Context Actions | https://store.kde.org/p/2372345 |
| Dolphin Archive | https://store.kde.org/p/2372346 |
| Dolphin Link | https://store.kde.org/p/2372347 |
