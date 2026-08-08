# README redesign specification

## Goal

Make the public README understandable to affected Minecraft Bedrock players
without removing the evidence and safety information developers need to assess
the patch.

## Audience and reading order

The primary reader is a Windows player who wants to know whether the patch
matches their camera problem and how to install it. The secondary reader is a
developer or cautious user who wants the measurements, implementation details,
build steps, and rollback guarantees.

The README will therefore use this order:

1. Project name, one-sentence purpose, and concise GitHub badges.
2. A prominent one-command installation section.
3. GitHub callouts covering Minecraft closure, UAC, and current compatibility.
4. A small feature table describing launcher independence, automatic repair,
   checksum verification, and exact uninstall.
5. A plain-English explanation of the camera batching problem and correction.
6. Visible manual commands, verification, update behavior, and uninstall steps.
7. Collapsible technical evidence, safety design, build instructions, and prior
   art.

## Markdown presentation

Use standard GitHub-flavored Markdown: headings, badges, fenced PowerShell,
tables, task-oriented lists, `[!NOTE]` and `[!WARNING]` callouts, and
`<details>` sections. Avoid decorative clutter, excessive emoji, nested lists,
and long uninterrupted paragraphs.

## Content constraints

- Preserve the exact GitHub release URL and checksum-verifying installer.
- Preserve the tested Minecraft package/version and measured cadence facts.
- Do not promise compatibility with arbitrary future versions.
- State clearly that administrator access modifies the protected package.
- Keep uninstall and verification easy to find without expanding a section.
- Keep build details and attribution available but out of the normal install
  path.

## Acceptance criteria

- A nontechnical reader can find and understand installation in the first
  screenful.
- The README answers what it fixes, how it works, whether updates are handled,
  and how to uninstall.
- Technical readers can expand the evidence and safety sections.
- Every command remains syntactically intact and all repository-relative links
  resolve.
- The README contains no local paths, credentials, or unsupported claims.
