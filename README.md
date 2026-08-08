# MCFIX

MCFIX is a permanent, launcher-independent camera-smoothing patch for the
stepped mouse-look bug observed in Minecraft for Windows (Bedrock, GDK). It does
not change the Minecraft version, require Flarial, require a custom launcher,
or inject a DLL into an already-running game.

The current release was measured and validated on Minecraft package
`Microsoft.MinecraftUWP_1.26.4201.0_x64__8wekyb3d8bbwe` (game 1.26.42).

## What was wrong

On the affected GDK client, local telemetry showed the input/camera path running
at about 328 Hz while non-zero turn deltas arrived only about 47–51 times per
second. The non-zero values arrived roughly every 20 ms in repeated
approximately 0.129925653-degree chunks. The renderer could therefore be smooth
while the camera angle itself still advanced in visible steps.

This is why generic FPS, GPU, Windows mouse-acceleration, polling-rate, and
display tweaks did not address the actual fault on the tested machine: the
coarse cadence existed inside the game's turn-delta path.

MCFIX resolves the exact `InputHandler::tick`,
`LocalPlayer::applyTurnDelta`, and `MinecraftCamera::updateCamera` seams inside
the executable `.text` section. Every signature must resolve exactly once.
It distributes each coarse turn chunk across the intervening camera calls,
applies the first portion immediately, caps the queue at eight slots, flushes
on stale timing/object changes, and preserves the original total X/Y rotation.

Before distribution activates, a fixed-capacity cadence classifier requires
five consecutive non-zero source intervals in the 12–35 ms range while camera
calls remain at or below 9 ms and at least 2.5 times faster. Already-smooth
per-camera input and genuinely low frame-rate input remain pass-through.

In the validated capture, the corrected non-zero median interval was about
3.08 ms, 1,596 sub-quantum events were produced, and completed segments
preserved total rotation within about 0.000005 degrees. The visible result was
then confirmed in-game.

## How the permanent patch works

`Patcher.exe` performs one explicitly elevated, transactional installation:

1. It requires Minecraft to be closed and verifies the exact registered
   `Microsoft.MinecraftUWP` x64 package.
2. It validates that the preserved VC runtime is a real x64 DLL with one direct
   `__CxxFrameHandler4` export—not another forwarding proxy.
3. It preserves that runtime as
   `vcruntime140_1_mcfix_original.dll` and verifies the copied SHA-256.
4. It atomically installs the MCFIX `vcruntime140_1.dll` forwarding proxy and
   `MCFIXCameraPatch.dll` beside the game.
5. It restores the package directory owner/DACL and applies the original
   runtime's file security to all installed files.
6. It installs durable repair files under `%ProgramData%\MCFIX` and creates one
   hidden, highest-privilege logon task.

The proxy is loaded by Minecraft's normal dependency loading path. It forwards
the original runtime export and loads only `MCFIXCameraPatch.dll`, only inside
`Minecraft.Windows.exe`. There is no launcher dependency and no remote-thread
injector.

The no-window `MCFIXWatchdog.exe` waits on the Windows process-start event
provider rather than repeatedly scanning the machine. It performs no idle disk
or network polling. On each Minecraft start it checks a PID-scoped bootstrap
heartbeat. If an update removed the bootstrap, the watcher leaves the running
game untouched, waits for it to close, then repairs the new package directory.
The next normal launch is patched again.

Future builds are allowed to reach the resolver only when the package name,
x64 architecture, and Microsoft publisher family still match. If any hook
signature becomes missing or ambiguous, MCFIX fails closed and the game runs
without camera hooks; it never guesses an address.

## Universal PowerShell install

No server, open port, or port forwarding is required. GitHub hosts the release
assets over normal outbound HTTPS. Open PowerShell and run this single command:

```powershell
$ErrorActionPreference='Stop';$b='https://github.com/MKKSANDI/MCFIX/releases/latest/download';$d=Join-Path $env:TEMP ('MCFIX-'+[guid]::NewGuid());New-Item -ItemType Directory -Path $d|Out-Null;$z=Join-Path $d 'MCFIX-win-x64.zip';$s="$z.sha256";Invoke-WebRequest "$b/MCFIX-win-x64.zip" -OutFile $z;Invoke-WebRequest "$b/MCFIX-win-x64.zip.sha256" -OutFile $s;$e=((Get-Content $s -Raw).Trim() -split '\s+')[0].ToUpperInvariant();$a=(Get-FileHash $z -Algorithm SHA256).Hash;if($a-ne$e){throw "MCFIX release hash mismatch"};Expand-Archive $z -DestinationPath $d -Force;$p=Join-Path $d 'MCFIX\Patcher.exe';if(([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){& $p install;if($LASTEXITCODE){throw "Patcher failed: $LASTEXITCODE"}}else{$q=Start-Process $p -Verb RunAs -ArgumentList install -Wait -PassThru;if($q.ExitCode){throw "Patcher failed: $($q.ExitCode)"}}
```

The command downloads the latest release ZIP and its SHA-256 file, verifies the
archive before extraction, and either runs the patcher directly from an
administrator PowerShell or requests UAC normally.

## Manual use

Extract `MCFIX-win-x64.zip`, close Minecraft, then run:

```powershell
.\Patcher.exe install
.\Patcher.exe status
.\Patcher.exe verify
.\Patcher.exe repair
.\Patcher.exe uninstall
```

Double-clicking `Patcher.exe` selects `install`. Mutating commands self-elevate
through the standard UAC prompt. `status` is read-only; `verify` exits non-zero
unless the installed proxy and camera payload exactly match the release.

## Update behavior and limits

- Ordinary launchers, the Start menu, protocol activation, and the Minecraft
  Launcher all use the same installed patch.
- After a Minecraft update, the first launch can be unpatched if the update
  replaced the bootstrap. The watcher repairs only after that process exits so
  it never rewrites a running game.
- Compatible updates whose three signatures remain unique activate normally.
- An incompatible build is left untouched at runtime and needs a newer MCFIX
  release. No universal native patch can safely promise guessed offsets for an
  arbitrary future executable.
- Store Verify/Repair or reinstall can intentionally remove the patch. The
  watcher repairs a compatible active package afterward.
- This project modifies files in the protected package directory with explicit
  administrator approval. Every managed change has a verified original and an
  uninstall path, but Minecraft/Microsoft support may ask you to uninstall it
  before troubleshooting.

## Safety properties

- Minecraft must be closed for install, repair, and uninstall.
- The installer refuses missing, non-x64, forwarded, ambiguous, or otherwise
  unknown original-runtime layouts.
- All payload copies are staged, SHA-256 verified, and atomically replaced.
- Package-directory security is captured and restored after each transaction.
- The patch scans only executable `.text`; it does not scan/write arbitrary
  game memory, camera objects, matrices, or vtables.
- The camera hook uses fixed-capacity thread-local state with no allocation or
  lock on the hot path.
- Flarial and historical MCFIX hook DLLs are treated as conflicts to avoid
  overlapping camera hooks.
- The watchdog has no console window, no network client, and no polling loop.

## Build and test

Requirements: Windows 11, Visual Studio 2022 with the Desktop C++ workload,
CMake 3.24 or newer, and Windows SDK 10.0.26100 or newer.

```powershell
cmake -S camera_patch -B camera_patch/out -A x64
cmake --build camera_patch/out --config Release -- /m:1
ctest --test-dir camera_patch/out -C Release --output-on-failure
```

Release binaries are produced under `camera_patch/out/Release/`. MinHook is
pinned and vendored under `third_party/minhook`; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Uninstall

Close Minecraft, then run the installed patcher from an elevated PowerShell:

```powershell
& "$env:ProgramData\MCFIX\bin\Patcher.exe" uninstall
```

Uninstall restores the verified original runtime, removes only MCFIX-managed
game siblings, ends/deletes the watchdog task, and removes durable payloads.
Worlds, options, accounts, and launcher data are not touched.

## Prior art

[Igneous](https://github.com/Aetopia/Igneous) and
[Pyroclastic](https://github.com/Aetopia/Pyroclastic) helped narrow the viable
GDK bootstrap direction. MCFIX's correction is based on its own local telemetry
and controlled before/after validation. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution details.
