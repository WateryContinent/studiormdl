// rui_parse.h
// Data structures and parser for the .rui text format (Respawn UI mesh descriptions).
//
// The .rui format stores the per-model RUI (Respawn UI) mesh geometry that gets
// embedded in RMDL v54 files.  Each ruimesh defines a flat quad/triangle panel
// that is parented to one or more bones and carries UV / scale data for the
// engine to map a 2-D UI element onto the 3-D world.
//
// .rui TEXT FORMAT REFERENCE
// ---------------------------
// version 1
//
// // Full mesh name (used to compute the engine name-hash).
// ruimesh "models/weapons/r97/r97_rui_upper"
// {
//     // One 'bone' line per parent bone.  Most meshes have exactly one.
//     bone "BSTNK_barrel"
//
//     // Vertices: bone-name (resolves to per-vertex parent index) + local XYZ.
//     vertex "BSTNK_barrel"   1.0   2.0   3.0
//     vertex "BSTNK_barrel"  -1.0   2.0   3.0
//     vertex "BSTNK_barrel"  -1.0   0.0   3.0
//     vertex "BSTNK_barrel"   1.0   0.0   3.0
//
//     // Faces: v0 v1 v2  extra0 extra1
//     //        uvmin_x uvmin_y  uvmax_x uvmax_y
//     //        scalemin_x scalemin_y  scalemax_x scalemax_y
//     //
//     // v0..v2   : zero-based vertex indices (triangle winding).
//     // extra0/1 : raw bytes from mstudioruifourthvertv54_t (-1 = unused).
//     // uvmin/max: face UV corners (vertex 1 and vertex 4 of the quad).
//     // scale*   : face scale corners.
//     face  0 2 1  -1 -1   0.0 0.0   1.0 1.0   0.0 0.0   1.0 1.0
//     face  0 3 2  -1 -1   0.0 0.0   1.0 1.0   0.0 0.0   1.0 1.0
// }

#pragma once

#include <string>
#include <vector>
#include <stdint.h>

// ---------------------------------------------------------------------------
// In-memory representation after parsing a .rui file
// ---------------------------------------------------------------------------

struct RuiVertex
{
    std::string boneName;   // parent bone for this vertex (by name)
    float       x, y, z;   // local position relative to that bone
};

struct RuiFace
{
    int16_t vertid[3];      // vertex indices (triangle)

    // mstudioruifourthvertv54_t — -1 when unused
    int8_t  vertextra;
    int8_t  vertextra1;

    // mstudioruimeshface_t — UVs and scale for the two non-shared quad corners
    float uvminx,    uvminy;    // faceuvmin  (vertex 1)
    float uvmaxx,    uvmaxy;    // faceuvmax  (vertex 4)
    float scaleminx, scaleminy; // facescalemin
    float scalemaxx, scalemaxy; // facescalemax
};

struct RuiMesh
{
    std::string              name;       // full mesh name stored in the RMDL
    std::vector<std::string> boneNames;  // parent bone names (numparents)
    std::vector<RuiVertex>   vertices;
    std::vector<RuiFace>     faces;

    // Optional hash override.  -1 means "compute from name using FNV-1a 32-bit".
    // Decompiled .rui files set this to the exact value read from the binary so
    // that re-compiling reproduces the original file bit-for-bit.
    int32_t namehash = -1;

    // unk field from mstudioruimesh_t_v54 offset +6.  Meaning is undiscovered but
    // non-zero in all Respawn-compiled meshes.  Defaults to numFaces if not set
    // by the decompiler (field added for round-trip fidelity).
    // -1 means "default to numFaces at compile time".
    int16_t unk = -1;
};

struct RuiFile
{
    int                    version;  // format version (currently 1)
    std::vector<RuiMesh>   meshes;
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

// Parse a .rui text file at the given path.
// Returns true on success; on failure prints an error to stderr and returns false.
bool ParseRuiFile(const char* path, RuiFile& out);
