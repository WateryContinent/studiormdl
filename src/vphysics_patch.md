# prebuilt/vphysics.dll — Patched Portal 2 Binary

## What it is

`prebuilt/vphysics.dll` is Portal 2's `vphysics.dll` with a single-byte binary
patch applied. It does **not** require Portal 2 to be installed.

`collect_output.bat` copies it to `bin/` automatically. No Valve auth tools or
Steam ownership of Portal 2 are needed for a fresh source build.

## Why it is patched

Our tier0 ships the CSGO-era `CPUInformation` struct which added two fields
(`m_nLogicalProcessors` and `m_nPhysicalProcessors`) at offsets 4–5, pushing the
SSE-capability bitfield from offset 4 to offset 6.

Portal 2's `vphysics.dll` was compiled against the older struct and reads the
bitfield at offset 4. On a machine with ≥16 logical processors `m_nLogicalProcessors`
is `0x10`, whose SSE bit (bit 3) is 0, causing MathLib_Init to abort with:

```
"SSE and SSE2 are required."   (error 1114 / ERROR_DLL_INIT_FAILED)
```

## The patch

| File offset | Before | After | Instruction |
|-------------|--------|-------|-------------|
| 0xA74C4     | `0x04` | `0x06` | `mov al, [eax+N]` — N changed 4→6 |

Full original instruction: `8A 40 04` → patched: `8A 40 06`

This makes Portal 2's MathLib_Init read the SSE bitfield from the correct offset
in our CSGO-era struct.

## Reproducing the patch on a fresh vphysics.dll

If you ever need to re-patch from a different copy of Portal 2's vphysics.dll:

```powershell
# Locate the 3-byte sequence 8A 40 04 A8 08 near the "SSE and SSE2" string
# (cross-ref from the error string to MathLib_Init).
# Change byte at offset +2 from 0x04 to 0x06.

$dll  = 'path\to\vphysics.dll'
$copy = 'prebuilt\vphysics.dll'
Copy-Item $dll $copy

$bytes = [System.IO.File]::ReadAllBytes($copy)
# Patch offset 0xA74C4 (adjust if DLL differs — verify via hex search for 8A 40 04 A8 08)
$bytes[0xA74C4] = 0x06
[System.IO.File]::WriteAllBytes($copy, $bytes)
```

Verify with: the 12 bytes at 0xA74C2 should read `8A 40 06 A8 08 74 04 A8 10 75 0E 68`.
