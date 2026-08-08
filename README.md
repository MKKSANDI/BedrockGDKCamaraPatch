# Bedrock GDK Camera Patch

[![Latest release](https://img.shields.io/github/v/release/MKKSANDI/BedrockGDKCamaraPatch?label=release)](https://github.com/MKKSANDI/BedrockGDKCamaraPatch/releases/latest)
[![Windows 11](https://img.shields.io/badge/platform-Windows%2011-0078D4?logo=windows11)](#compatibility)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

**MCFIX removes the stepped mouse-camera movement found in Minecraft for
Windows (Bedrock, GDK).** It patches the installed game, so it works with the
Minecraft Launcher, the Start menu, and other launch methods.

## Install

> [!IMPORTANT]
> Close Minecraft before installing. The patcher will request administrator
> access through the normal Windows UAC prompt.

Open **PowerShell**, paste this command, and press Enter:

```powershell
$ErrorActionPreference='Stop';$b='https://github.com/MKKSANDI/BedrockGDKCamaraPatch/releases/latest/download';$d=Join-Path $env:TEMP ('MCFIX-'+[guid]::NewGuid());New-Item -ItemType Directory -Path $d|Out-Null;$z=Join-Path $d 'MCFIX-win-x64.zip';$s="$z.sha256";Invoke-WebRequest "$b/MCFIX-win-x64.zip" -OutFile $z;Invoke-WebRequest "$b/MCFIX-win-x64.zip.sha256" -OutFile $s;$e=((Get-Content $s -Raw).Trim() -split '\s+')[0].ToUpperInvariant();$a=(Get-FileHash $z -Algorithm SHA256).Hash;if($a-ne$e){throw "MCFIX release hash mismatch"};Expand-Archive $z -DestinationPath $d -Force;$p=Join-Path $d 'MCFIX\Patcher.exe';if(([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){& $p install;if($LASTEXITCODE){throw "Patcher failed: $LASTEXITCODE"}}else{$q=Start-Process $p -Verb RunAs -ArgumentList install -Wait -PassThru;if($q.ExitCode){throw "Patcher failed: $($q.ExitCode)"}}
```

The command downloads the latest [GitHub release](https://github.com/MKKSANDI/BedrockGDKCamaraPatch/releases/latest),
checks its SHA-256 checksum, extracts it, and runs the patcher. GitHub hosts
everything over normal outbound HTTPS.

> [!WARNING]
> MCFIX modifies files in Minecraft's protected package directory. It keeps a
> verified original and provides a full uninstall, but you should uninstall it
> before asking Minecraft or Microsoft support to troubleshoot the game.

## What it does

| Feature | Behavior |
| --- | --- |
| Smooth camera movement | Distributes the game's coarse turn chunks across its faster camera updates. |
| Works with any launcher | Minecraft loads the patch through its normal dependency path. |
| Survives compatible updates | A hidden, event-driven watchdog detects when an update removes the bootstrap and repairs it after Minecraft closes. |
| Fails safely | Missing or ambiguous code signatures are rejected; MCFIX never guesses an address. |
| Fully reversible | Uninstall restores the verified original runtime and removes MCFIX-managed files and its watchdog. |

## What was fixed

On the affected GDK client, the camera path ran at about **328 Hz**, but actual
mouse-turn values arrived only **47-51 times per second** in repeated chunks
about every **20 ms**. That made the camera advance in visible steps even when
the FPS counter was high.

MCFIX detects that exact cadence and spreads each coarse turn across the camera
updates between it and the next chunk. It preserves the total X/Y rotation and
leaves already-smooth input unchanged. The corrected behavior was measured and
confirmed in-game.

## Manual commands

Download and extract `MCFIX-win-x64.zip`, open PowerShell inside its `MCFIX`
folder, then use:

```powershell
.\Patcher.exe install    # Install the patch
.\Patcher.exe status     # Show the current package state
.\Patcher.exe verify     # Confirm installed files match this release
.\Patcher.exe repair     # Reapply the patch after an update
.\Patcher.exe uninstall  # Restore the original game files
```

Double-clicking `Patcher.exe` runs `install`. Commands that change files request
UAC automatically; `status` and `verify` are read-only.

## Updates

The watchdog uses Windows process-start events, so it does not repeatedly scan
the PC, poll the disk, or use the network while idle.

If a Minecraft update removes the patch, the first updated launch may be
unpatched. The watchdog waits for that game process to close and then repairs
the new package. The next normal launch is patched again.

> [!NOTE]
> Compatible updates must still contain one exact match for each required game
> function. If an update changes those functions, MCFIX leaves the game alone
> until a compatible patch release is available.

## Uninstall

Close Minecraft, open an elevated PowerShell, and run:

```powershell
& "$env:ProgramData\MCFIX\bin\Patcher.exe" uninstall
```

This restores the verified original runtime and removes only MCFIX files and
its scheduled watchdog. Worlds, accounts, options, and launcher data are not
touched.

## Compatibility

The current release was measured and validated on:

- Minecraft for Windows package
  `Microsoft.MinecraftUWP_1.26.4201.0_x64__8wekyb3d8bbwe`
- Game version `1.26.42`
- Windows 11, x64

Other compatible GDK builds may work when every required signature remains
unique. Arbitrary future versions are not guaranteed.

<details>
<summary><strong>Technical evidence</strong></summary>

Local telemetry showed:

- Input/camera path: approximately **328 Hz**.
- Non-zero turn-delta cadence: approximately **47-51 Hz**.
- Source interval: approximately **20 ms**.
- Repeated chunk size: approximately **0.129925653 degrees**.
- Corrected non-zero median interval: approximately **3.08 ms**.
- Sub-quantum corrected events produced: **1,596**.
- Completed segments preserved rotation within approximately **0.000005
  degrees**.

The cadence classifier activates only after five consecutive non-zero source
intervals between 12 and 35 ms while camera calls remain at or below 9 ms and
at least 2.5 times faster. Low-frame-rate input and already-smooth per-camera
input remain pass-through.

</details>

<details>
<summary><strong>How the permanent patch works</strong></summary>

`Patcher.exe` performs one elevated, transactional installation:

1. Requires Minecraft to be closed and verifies the registered
   `Microsoft.MinecraftUWP` x64 package.
2. Validates the original VC runtime and its direct `__CxxFrameHandler4`
   export.
3. Preserves it as `vcruntime140_1_mcfix_original.dll` and verifies the copy's
   SHA-256.
4. Atomically installs the forwarding `vcruntime140_1.dll` and
   `MCFIXCameraPatch.dll` beside Minecraft.
5. Restores the package directory owner, access rules, and original file
   security.
6. Installs durable repair files under `%ProgramData%\MCFIX` and registers one
   hidden watchdog task.

Minecraft loads the forwarding runtime normally. It forwards the original
runtime export and loads `MCFIXCameraPatch.dll` only inside
`Minecraft.Windows.exe`. No remote-thread injection or launcher integration is
used.

The patch resolves `InputHandler::tick`, `LocalPlayer::applyTurnDelta`, and
`MinecraftCamera::updateCamera` inside the executable `.text` section. Every
signature must resolve exactly once before any hook is enabled.

</details>

<details>
<summary><strong>Safety design</strong></summary>

- Minecraft must be closed during installation, repair, and uninstall.
- Unknown, non-x64, forwarded, missing, or ambiguous runtime layouts are
  rejected.
- Payloads are staged, SHA-256 verified, and atomically replaced.
- Package-directory ownership and access rules are restored after each
  transaction.
- Scanning is limited to executable `.text`; MCFIX does not scan or rewrite
  arbitrary game memory, camera objects, matrices, or vtables.
- Hot-path camera state is fixed-capacity and thread-local, with no allocation
  or locking.
- The watchdog has no console window, network client, or polling loop.

</details>

<details>
<summary><strong>Build and test</strong></summary>

Requirements:

- Windows 11
- Visual Studio 2022 with Desktop development with C++
- CMake 3.24 or newer
- Windows SDK 10.0.26100 or newer

```powershell
cmake -S camera_patch -B camera_patch/out -A x64
cmake --build camera_patch/out --config Release -- /m:1
ctest --test-dir camera_patch/out -C Release --output-on-failure
```

Release binaries are written to `camera_patch/out/Release`. MinHook is pinned
and vendored under `third_party/minhook`; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

</details>

<details>
<summary><strong>Credits and prior art</strong></summary>

[Igneous](https://github.com/Aetopia/Igneous) and
[Pyroclastic](https://github.com/Aetopia/Pyroclastic) helped narrow the viable
GDK bootstrap direction. MCFIX's camera correction comes from its own local
telemetry and controlled before/after validation.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for complete attribution.

</details>
