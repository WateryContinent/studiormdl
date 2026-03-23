#!/usr/bin/env python3
"""
rmdl_dump_rui.py
================
Extracts RUI (Respawn UI) mesh data from an RMDL v54 file and writes it
as a .rui text file compatible with studiormdl's $ruimeshfile command.

Usage
-----
    python rmdl_dump_rui.py <file.rmdl> [output.rui]

    If the output path is omitted, <file>.rui is written alongside the input.
    Pass --info to print a summary without writing any file.

Format notes
------------
The script produces the exact format consumed by the studiormdl compiler.
A '// namehash:' comment is written above each ruimesh block showing the
32-bit hash stored in the binary.  The compiler recomputes this hash from
the mesh name using FNV-1a 32-bit; if the values differ for a reference
model, the hash algorithm may need adjustment.
"""

import struct
import sys
import os

# ---------------------------------------------------------------------------
# r5_studiohdr_t byte offsets (all fields are int32 unless stated)
# Verified against the C struct in rmdl_write.cpp — total size = 548 bytes
# ---------------------------------------------------------------------------
_HDR_ID            =   0   # int32  'IDST' magic
_HDR_VERSION       =   4   # int32  should be 54
_HDR_NAME          =  16   # char[64] — model name string
_HDR_NUMBONES      = 160   # int32
_HDR_BONEINDEX     = 164   # int32  absolute offset to bone array
_HDR_UIPANELCOUNT  = 300   # int32  number of RUI meshes
_HDR_UIPANELOFFSET = 304   # int32  absolute offset to ruiheader[] array

# mstudiobone_t_v54: stride computed from reference file (20424 / 111 = 184 bytes)
_BONE_STRIDE = 184
_BONE_SZNAMEOFFSET = 0     # int32 at byte 0 of each bone struct: offset to name

# mstudiorruiheader_t  (8 bytes)
#   int32 namehash
#   int32 ruimeshindex  ← offset FROM start of this 8-byte entry to mesh data

# mstudioruimesh_t_v54 (28 bytes)
#   int16 numparents    @ 0
#   int16 numvertices   @ 2
#   int16 numfaces      @ 4
#   int16 unk           @ 6
#   int32 parentindex   @ 8   ← offset from mesh start to parent int16[] array
#   int32 vertexindex   @ 12  ← offset from mesh start to mstudioruivert_t[]
#   int32 unkindex      @ 16  ← offset from mesh start to fourthvert[] array
#   int32 vertmapindex  @ 20  ← offset from mesh start to mstudioruivertmap_t[]
#   int32 facedataindex @ 24  ← offset from mesh start to mstudioruimeshface_t[]

# mstudioruivert_t     (16 bytes)  int32 parent + float x + float y + float z
# mstudioruivertmap_t  ( 6 bytes)  int16 vertid[3]
# mstudioruifourthvertv54_t (2 bytes) int8 vertextra + int8 vertextra1
# mstudioruimeshface_t (32 bytes)  8x float: uvmin xy, uvmax xy, scalemin xy, scalemax xy

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _i32(data: bytes, off: int) -> int:
    return struct.unpack_from('<i', data, off)[0]

def _u32(data: bytes, off: int) -> int:
    return struct.unpack_from('<I', data, off)[0]

def _i16(data: bytes, off: int) -> int:
    return struct.unpack_from('<h', data, off)[0]

def _i8(data: bytes, off: int) -> int:
    return struct.unpack_from('<b', data, off)[0]

def _f32(data: bytes, off: int) -> float:
    return struct.unpack_from('<f', data, off)[0]

def _cstr(data: bytes, off: int) -> str:
    """Read a null-terminated UTF-8 string."""
    end = data.index(b'\x00', off)
    return data[off:end].decode('utf-8', errors='replace')

# ---------------------------------------------------------------------------
# RMDL parser
# ---------------------------------------------------------------------------

def parse_rmdl_rui(data: bytes):
    """
    Parse RUI mesh data out of an RMDL v54 binary blob.

    Returns a list of mesh dicts (see below) and the bone name table.
    Raises ValueError on unrecoverable format errors.
    """
    if len(data) < 548:
        raise ValueError("file too small to be an RMDL")

    magic   = _u32(data, _HDR_ID)
    version = _i32(data, _HDR_VERSION)
    if magic != 0x54534449:   # little-endian 'IDST'
        raise ValueError(f"bad magic 0x{magic:08X} — not an RMDL/MDL file")
    if version != 54:
        print(f"  WARNING: expected version 54, got {version} — attempting anyway",
              file=sys.stderr)

    # ---- bone names --------------------------------------------------------
    numbones  = _i32(data, _HDR_NUMBONES)
    boneindex = _i32(data, _HDR_BONEINDEX)
    bone_names: list[str] = []
    for i in range(numbones):
        bone_off   = boneindex + i * _BONE_STRIDE
        name_off   = _i32(data, bone_off + _BONE_SZNAMEOFFSET)
        bone_names.append(_cstr(data, bone_off + name_off))

    # ---- RUI header --------------------------------------------------------
    num_rui  = _i32(data, _HDR_UIPANELCOUNT)
    rui_base = _i32(data, _HDR_UIPANELOFFSET)

    if num_rui <= 0 or rui_base <= 0:
        return [], bone_names   # no RUI meshes

    meshes = []
    for i in range(num_rui):
        hdr_off      = rui_base + i * 8
        namehash     = _i32(data, hdr_off)
        ruimeshindex = _i32(data, hdr_off + 4)
        mesh_off     = hdr_off + ruimeshindex

        # mstudioruimesh_t_v54
        numparents   = _i16(data, mesh_off +  0)
        numvertices  = _i16(data, mesh_off +  2)
        numfaces     = _i16(data, mesh_off +  4)
        unk          = _i16(data, mesh_off +  6)
        parentindex  = _i32(data, mesh_off +  8)
        vertexindex  = _i32(data, mesh_off + 12)
        unkindex     = _i32(data, mesh_off + 16)   # fourthvert array
        vertmapindex = _i32(data, mesh_off + 20)
        facedataindex= _i32(data, mesh_off + 24)

        name = _cstr(data, mesh_off + 28)

        # parent bone indices → names
        parent_bone_indices = [
            _i16(data, mesh_off + parentindex + j * 2)
            for j in range(numparents)
        ]
        parent_bone_names = [
            bone_names[p] if 0 <= p < len(bone_names) else f"bone_{p}"
            for p in parent_bone_indices
        ]

        # vertex map (3x int16 per face)
        vertmaps = []
        vm_base = mesh_off + vertmapindex
        for j in range(numfaces):
            v0 = _i16(data, vm_base + j * 6 + 0)
            v1 = _i16(data, vm_base + j * 6 + 2)
            v2 = _i16(data, vm_base + j * 6 + 4)
            vertmaps.append((v0, v1, v2))

        # fourthvert (2 signed bytes per face)
        fourthverts = []
        fv_base = mesh_off + unkindex
        for j in range(numfaces):
            e0 = _i8(data, fv_base + j * 2 + 0)
            e1 = _i8(data, fv_base + j * 2 + 1)
            fourthverts.append((e0, e1))

        # vertices (int32 bone-parent + 3x float = 16 bytes each)
        vertices = []
        v_base = mesh_off + vertexindex
        for j in range(numvertices):
            vp = _i32(data, v_base + j * 16 +  0)
            vx = _f32(data, v_base + j * 16 +  4)
            vy = _f32(data, v_base + j * 16 +  8)
            vz = _f32(data, v_base + j * 16 + 12)
            vbone = bone_names[vp] if 0 <= vp < len(bone_names) else f"bone_{vp}"
            vertices.append((vbone, vx, vy, vz))

        # face data (8x float = 32 bytes each)
        facedata = []
        fd_base = mesh_off + facedataindex
        for j in range(numfaces):
            uvminx    = _f32(data, fd_base + j * 32 +  0)
            uvminy    = _f32(data, fd_base + j * 32 +  4)
            uvmaxx    = _f32(data, fd_base + j * 32 +  8)
            uvmaxy    = _f32(data, fd_base + j * 32 + 12)
            scaleminx = _f32(data, fd_base + j * 32 + 16)
            scaleminy = _f32(data, fd_base + j * 32 + 20)
            scalemaxx = _f32(data, fd_base + j * 32 + 24)
            scalemaxy = _f32(data, fd_base + j * 32 + 28)
            facedata.append((uvminx, uvminy, uvmaxx, uvmaxy,
                             scaleminx, scaleminy, scalemaxx, scalemaxy))

        meshes.append({
            'name':              name,
            'namehash':          namehash & 0xFFFFFFFF,
            'unk':               unk,
            'parent_bone_names': parent_bone_names,
            'vertices':          vertices,
            'vertmaps':          vertmaps,
            'fourthverts':       fourthverts,
            'facedata':          facedata,
        })

    return meshes, bone_names


def print_summary(meshes, bone_names, rmdl_path: str):
    """Print a human-readable summary to stdout."""
    print(f"\n  RMDL: {os.path.basename(rmdl_path)}")
    print(f"  Bones: {len(bone_names)}")
    print(f"  RUI meshes: {len(meshes)}\n")
    for i, m in enumerate(meshes):
        print(f"  [{i:2d}] \"{m['name']}\"")
        print(f"        hash=0x{m['namehash']:08X}  "
              f"parents={len(m['parent_bone_names'])}  "
              f"verts={len(m['vertices'])}  faces={len(m['facedata'])}")
        for bn in m['parent_bone_names']:
            print(f"          bone \"{bn}\"")


def write_rui(meshes, out_path: str):
    """Write meshes to a .rui text file."""
    lines = [
        'version 1',
        '',
        f'// Generated by rmdl_dump_rui.py from {len(meshes)} mesh(es)',
        '// namehash values are preserved exactly from the binary for round-trip fidelity.',
        '',
    ]

    for m in meshes:
        lines.append(f'ruimesh "{m["name"]}"')
        lines.append('{')
        # Write the exact hash from the binary.  Respawn uses an undiscovered hash
        # algorithm (not FNV-1a, CRC32, Murmur, etc. applied to the stored name string),
        # so we always preserve the raw value.  The studiormdl compiler honours the
        # 'namehash' keyword and skips recomputing the hash.
        lines.append(f'    namehash 0x{m["namehash"]:08X}')
        lines.append(f'    unk {m["unk"]}')
        lines.append('')

        for bn in m['parent_bone_names']:
            lines.append(f'    bone "{bn}"')

        if m['vertices']:
            lines.append('')
            for (vbone, vx, vy, vz) in m['vertices']:
                lines.append(f'    vertex "{vbone}"  {vx:.6f}  {vy:.6f}  {vz:.6f}')

        if m['facedata']:
            lines.append('')
            for j, (v0, v1, v2) in enumerate(m['vertmaps']):
                e0, e1 = m['fourthverts'][j]
                um, um2, uM, uM2, sm, sm2, sM, sM2 = m['facedata'][j]
                lines.append(
                    f'    face  {v0} {v1} {v2}  {e0} {e1}'
                    f'  {um:.6f} {um2:.6f}  {uM:.6f} {uM2:.6f}'
                    f'  {sm:.6f} {sm2:.6f}  {sM:.6f} {sM2:.6f}'
                )

        lines.append('}')
        lines.append('')

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print(f"  Written: {out_path}  ({len(meshes)} mesh(es))")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    args = sys.argv[1:]
    info_only = '--info' in args
    args = [a for a in args if not a.startswith('--')]

    if not args:
        print(__doc__)
        sys.exit(1)

    rmdl_path = args[0]
    out_path  = args[1] if len(args) > 1 else None

    if not os.path.isfile(rmdl_path):
        print(f"ERROR: file not found: {rmdl_path}", file=sys.stderr)
        sys.exit(1)

    with open(rmdl_path, 'rb') as f:
        data = f.read()

    try:
        meshes, bone_names = parse_rmdl_rui(data)
    except (ValueError, struct.error, IndexError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)

    if not meshes:
        print("No RUI meshes found in this RMDL.")
        sys.exit(0)

    print_summary(meshes, bone_names, rmdl_path)

    if not info_only:
        if out_path is None:
            out_path = os.path.splitext(rmdl_path)[0] + '.rui'
        write_rui(meshes, out_path)


if __name__ == '__main__':
    main()
