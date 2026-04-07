# Auto-Updater Variant Compatibility Fix

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the auto-updater so it finds releases with the `main-rN` tag format and downloads the correct variant binary (`-standard` or `-salt_spray`) for each device.

**Architecture:** Two minimal changes: (1) `release.yml` uses `main-rN` tags instead of bare `rN`, restoring compatibility with the existing `parse_release_number()` logic that expects `{branch}-rN`; (2) `auto_updater.c` gains a compile-time `FIRMWARE_VARIANT` macro that appends the correct suffix to the firmware download URL.

**Tech Stack:** ESP-IDF v5.5.3, C preprocessor macros, GitHub Actions, `gh` CLI.

---

## Background

After the Kconfig migration, two incompatibilities were introduced:

| | Before migration | After migration (broken) | After this fix |
|---|---|---|---|
| Release tag | `main-r5` | `r5` | `main-r5` |
| Firmware binary | `lorawan-enddevice-main-r5.bin` | `lorawan-enddevice-r5-standard.bin` | `lorawan-enddevice-main-r5-standard.bin` |
| www binary | `www-main-r5.bin` | `www-r5.bin` | `www-main-r5.bin` |

**Existing devices with `auto_update_branch = "salt_spray"`:** After this fix, no `salt_spray-rN` releases exist. The updater will log a warning and skip until the operator changes `auto_update_branch` to `"main"` via the web UI. This is expected and acceptable — document it in CLAUDE.md.

---

## Files

- Modify: `.github/workflows/release.yml` — tag format only (2 lines)
- Modify: `main/update/auto_updater.c` — FIRMWARE_VARIANT macro + firmware URL pattern

---

### Task 1: Fix release tag format in release.yml

**Files:**
- Modify: `.github/workflows/release.yml`

The `Calculate release number` step currently generates bare `rN` tags. Change it to generate `main-rN` tags. The binary rename and release upload steps reference `${{ steps.release_info.outputs.tag }}` and don't need changes.

- [ ] **Step 1: Fix the tag regex filter and tag format**

In `.github/workflows/release.yml`, find the `Calculate release number` step:

```yaml
      - name: Calculate release number
        id: release_info
        run: |
          COUNT=$(gh api repos/${{ github.repository }}/releases \
            --paginate \
            --jq '.[] | select(.tag_name | test("^r[0-9]+$")) | .tag_name' \
            2>/dev/null | wc -l || echo 0)
          NEXT=$((COUNT + 1))
          echo "tag=r${NEXT}" >> $GITHUB_OUTPUT
          echo "name=Release #${NEXT}" >> $GITHUB_OUTPUT
        env:
          GH_TOKEN: ${{ github.token }}
```

Replace with:

```yaml
      - name: Calculate release number
        id: release_info
        run: |
          COUNT=$(gh api repos/${{ github.repository }}/releases \
            --paginate \
            --jq '.[] | select(.tag_name | test("^main-r[0-9]+$")) | .tag_name' \
            2>/dev/null | wc -l || echo 0)
          NEXT=$((COUNT + 1))
          echo "tag=main-r${NEXT}" >> $GITHUB_OUTPUT
          echo "name=Release #${NEXT}" >> $GITHUB_OUTPUT
        env:
          GH_TOKEN: ${{ github.token }}
```

- [ ] **Step 2: Verify no other `rN`-format references remain**

```bash
grep -n '"tag=r\${' .github/workflows/release.yml
grep -n "test(\"\\^r\[" .github/workflows/release.yml
```

Expected: no output (both patterns gone).

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "fix(release): restore main-rN tag format for auto-updater compatibility"
```

---

### Task 2: Fix firmware URL in auto_updater.c to include variant suffix

**Files:**
- Modify: `main/update/auto_updater.c`

Add a `FIRMWARE_VARIANT` compile-time string macro after the existing `GITHUB_ASSET_BASE` define (line 36). Then update the `snprintf` that builds the firmware download URL (line 488-490) to append the variant suffix.

The www URL (`www-%s.bin`) does not change — the www partition is identical for both variants.

- [ ] **Step 1: Add FIRMWARE_VARIANT macro**

In `main/update/auto_updater.c`, find:

```c
#define GITHUB_ASSET_BASE   "https://github.com/" GITHUB_OWNER "/" GITHUB_REPO "/releases/download"
```

Replace with:

```c
#define GITHUB_ASSET_BASE   "https://github.com/" GITHUB_OWNER "/" GITHUB_REPO "/releases/download"

// Compile-time variant string — selects the correct firmware binary from the release
#ifdef CONFIG_THERMOCOUPLE_ENABLED
#define FIRMWARE_VARIANT    "salt_spray"
#else
#define FIRMWARE_VARIANT    "standard"
#endif
```

- [ ] **Step 2: Update firmware download URL to include variant suffix**

Find (lines 487-490):

```c
        char fw_url[256];
        snprintf(fw_url, sizeof(fw_url),
                 GITHUB_ASSET_BASE "/%s/lorawan-enddevice-%s.bin",
                 latest_tag, latest_tag);
```

Replace with:

```c
        char fw_url[256];
        snprintf(fw_url, sizeof(fw_url),
                 GITHUB_ASSET_BASE "/%s/lorawan-enddevice-%s-" FIRMWARE_VARIANT ".bin",
                 latest_tag, latest_tag);
```

- [ ] **Step 3: Verify www URL is unchanged**

The www URL (lines 513-516) must remain as-is — do not modify it:

```c
        snprintf(www_url, sizeof(www_url),
                 GITHUB_ASSET_BASE "/%s/www-%s.bin",
                 latest_tag, latest_tag);
```

Confirm this line is unchanged by reading the file around line 513.

- [ ] **Step 4: Build standard variant and verify URL string is embedded**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh
idf.py build 2>&1 | tail -5
```

Expected: `Project build complete.`

Verify the `standard` string is embedded in the binary (confirms FIRMWARE_VARIANT resolved correctly):

```bash
strings build/lorawan-enddevice.bin | grep "lorawan-enddevice.*standard"
```

Expected output (example): `lorawan-enddevice-%s-standard.bin` or a full URL like `https://github.com/felipeosmar/isi-latecme-quality-enddevice-2/releases/download`

- [ ] **Step 5: Build salt_spray variant and verify URL string**

```bash
idf.py fullclean
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.salt_spray" build 2>&1 | tail -5
```

Expected: `Project build complete.`

Verify `salt_spray` string is embedded:

```bash
strings build/lorawan-enddevice.bin | grep "lorawan-enddevice.*salt_spray"
```

Expected output contains `salt_spray` (confirming CONFIG_THERMOCOUPLE_ENABLED correctly switched the variant).

- [ ] **Step 6: Restore sdkconfig to standard variant**

```bash
idf.py fullclean
idf.py build 2>&1 | tail -3
git checkout -- sdkconfig sdkconfig.old
```

- [ ] **Step 7: Commit**

```bash
git add main/update/auto_updater.c
git commit -m "fix(auto_updater): download variant-specific firmware binary (standard/salt_spray)"
```

---

### Task 3: Document migration note for existing salt_spray devices

**Files:**
- Modify: `CLAUDE.md`

Existing devices in the field that were configured for `auto_update_branch = "salt_spray"` will no longer find updates after this change (no `salt_spray-rN` releases exist). The operator must update those devices via the web UI.

- [ ] **Step 1: Add migration note to CLAUDE.md**

Find the section in `CLAUDE.md` that documents the two firmware variants (the table added in the previous migration). After that section, add:

```markdown
**Existing salt_spray devices in the field:** Devices with `auto_update_branch = "salt_spray"` will stop receiving updates after the Kconfig migration. Change `auto_update_branch` to `"main"` via the web UI (Config → Auto-Update → Branch). The auto-updater will then download `lorawan-enddevice-main-rN-salt_spray.bin` automatically based on the compiled variant.
```

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs(claude): document auto-update branch migration for existing salt_spray devices"
```
