# Publishing to store.kde.org (Dolphin's "Download New Services…")

Dolphin's dialog reads category **102 – Dolphin Service Menus** from
store.kde.org through the OCS API (`/usr/share/knsrcfiles/servicemenu.knsrc`
→ `api.kde-look.org`). Getting listed means having a product in that
category with a payload archive Dolphin's `servicemenuinstaller` can run —
which is exactly what `scripts/build-ghns-package.sh` produces.

## One-time setup (manual, ~10 minutes)

1. Create an account at <https://www.opendesktop.org> (same login works on
   store.kde.org).
2. On <https://store.kde.org>, click **Add Product**.
3. Fill in:
   - **Category:** Dolphin Service Menus
   - **Name:** Dolphin Context Actions
   - **Description:** paste `packaging/pling/description.txt`
   - **License:** MIT
   - **Link to source:** the GitHub repo URL
   - **Preview images:** upload `promo/preview-1-hero.png`,
     `preview-2-menu.png`, `preview-3-formats.png` (the first one is the
     thumbnail users see inside Dolphin — keep it first)
4. Under **Files**, upload the current
   `dist/dolphin-context-actions-servicemenu-v*.tar.gz`
   (build it with `scripts/build-ghns-package.sh`).
5. Save. Note the product number in the URL: `https://store.kde.org/p/<ID>`.

Within a store cache refresh the product appears in Dolphin:
Settings → Configure Dolphin → Context Menu → **Download New Services…**

## Every release after that (automated)

Add three repository secrets on GitHub
(Settings → Secrets and variables → Actions):

| Secret             | Value                                  |
|--------------------|----------------------------------------|
| `PLING_USERNAME`   | your OpenDesktop login                 |
| `PLING_PASSWORD`   | its password                           |
| `PLING_CONTENT_ID` | the product number from the URL        |

When release-please cuts a release, the `publish-store` job builds the
payload and pushes it to the product via the OCS v1 API
(`scripts/publish-to-pling.sh`: `content/edit` for the version string,
`content/uploaddownload` for the file). Without the secrets the job skips
with a notice instead of failing.

The OCS write API is legacy. If it ever refuses (statuscode ≠ 100), fall
back to uploading the tarball from the release page by hand — same file,
same place.

## What users see, and what makes them click

The Dolphin dialog shows: first preview image, product name, author, the
first line of the description, star rating, download count. That first
line — "Convert files from the right-click menu." — is the whole pitch;
keep it under ~60 characters and benefit-first. Ratings come from users on
the store page, so the GitHub README links to the store page and asks
happy users to rate it there.
