// rmdl_bvh.h
// BVH4 collision mesh builder for RMDL files
// Generates collision data embedded at bvhOffset in the RMDL header.
//
// Based on reverse engineering of Apex Legends BVH4 collision format
// and MRVN-Radiant (r5valkyrie fork) reference implementation.

#pragma once

#include <cstdint>
#include <vector>

// Forward declarations
struct r5_vertexFileHeader_t;
namespace OptimizedModel { struct FileHeader_t; }

// Builds a self-contained BVH4 collision blob from the model's mesh data.
// pOldHdr is the original MDL v49 studiohdr_t (passed as void* to avoid
// including studio.h in the header).
// Returns the blob bytes, or empty vector if no valid collision triangles.
std::vector<uint8_t> R5_BuildBVHCollision(
    const void* pOldHdr,
    const OptimizedModel::FileHeader_t* pVTX,
    const r5_vertexFileHeader_t* pVVD);
