# README Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the dense public README with a quick-start-first GitHub Markdown document that remains technically credible.

**Architecture:** Keep installation, compatibility, features, manual commands, verification, update behavior, and uninstall visible. Move measurements, implementation internals, safety guarantees, build instructions, and attribution into focused `<details>` sections so advanced readers retain the full evidence without blocking new users.

**Tech Stack:** GitHub-flavored Markdown, PowerShell command examples, GitHub release assets.

## Global Constraints

- Preserve the repository URL `https://github.com/MKKSANDI/BedrockGDKCamaraPatch`.
- Preserve the checksum-verifying universal PowerShell installer exactly.
- Preserve the tested package `Microsoft.MinecraftUWP_1.26.4201.0_x64__8wekyb3d8bbwe` and measured cadence claims.
- Do not promise arbitrary future-version compatibility.
- Keep verification and uninstall visible without expanding a section.
- Do not add local paths, credentials, or unsupported claims.

---

### Task 1: Rewrite and publish the public README

**Files:**
- Modify: `README.md`
- Reference: `docs/superpowers/specs/2026-08-08-readme-redesign-design.md`

**Interfaces:**
- Consumes: GitHub release assets `MCFIX-win-x64.zip` and `MCFIX-win-x64.zip.sha256`.
- Produces: A public README whose primary path is install, verify, update behavior, and uninstall.

- [ ] **Step 1: Replace the README structure**

Create these visible sections in order: title and badges, plain-English purpose,
Install, What it does, What was fixed, Manual commands, Updates, Uninstall, and
Compatibility. Use `[!IMPORTANT]`, `[!NOTE]`, and `[!WARNING]` callouts only for
information that can change the outcome.

- [ ] **Step 2: Preserve advanced documentation without clutter**

Add collapsed sections for Technical evidence, How the permanent patch works,
Safety design, Build and test, and Credits. Retain exact measurements and
failure-closed behavior.

- [ ] **Step 3: Verify document integrity**

Run:

```powershell
git diff --check -- README.md
rg -n "BedrockGDKCamaraPatch/releases/latest/download|Patcher.exe.*uninstall|Microsoft.MinecraftUWP_1.26.4201.0_x64__8wekyb3d8bbwe|<details>|\[!IMPORTANT\]" README.md
rg -n "MKKSANDI/MCFIX|C:\\Users\\|github_pat_|ghp_" README.md
```

Expected: the first command reports no whitespace errors; the required-content
scan finds every named element; the stale URL/local-data scan finds nothing.

- [ ] **Step 4: Verify the hosted release path**

Run:

```powershell
curl.exe -fL --silent --show-error https://github.com/MKKSANDI/BedrockGDKCamaraPatch/releases/latest/download/MCFIX-win-x64.zip.sha256
```

Expected: exit code 0 and the published SHA-256 line.

- [ ] **Step 5: Commit and push**

```powershell
git add README.md docs/superpowers/plans/2026-08-08-readme-redesign.md
git commit -m "Simplify README"
git push origin main
```

Expected: local `HEAD` and `origin/main` resolve to the same commit.
