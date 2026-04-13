// rmdl_bvh.cpp
// BVH4 collision mesh builder for RMDL files.
//
// Generates a self-contained collision blob that is appended to the RMDL file
// at the bvhOffset field. The blob contains BVH4 nodes with packed int16
// triangle leaves, matching the format used by the Apex Legends engine.
//
// Based on MRVN-Radiant (r5valkyrie fork) BVH4 collision implementation
// and reverse engineered binary format from official RMDL files.

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>
#include <limits>

#include "studio.h"
#include "optimize.h"
#include "mathlib/mathlib.h"

// These structs are defined in rmdl_write.cpp — redeclare the ones we need.
// They must match exactly.

#define MAX_NUM_LODS_R5 8

struct r5_mstudioboneweight_t
{
    float   weight[3];
    char    bone[3];
    char    numbones;
};

struct r5_mstudiovertex_t
{
    r5_mstudioboneweight_t m_BoneWeights;
    Vector                 m_vecPosition;
    Vector                 m_vecNormal;
    Vector2D               m_vecTexCoord;
};

struct r5_vertexFileFixup_t
{
    int lod;
    int sourceVertexID;
    int numVertexes;
};

struct r5_vertexFileHeader_t
{
    int id;
    int version;
    int checksum;
    int numLODs;
    int numLODVertexes[MAX_NUM_LODS_R5];
    int numFixups;
    int fixupTableStart;
    int vertexDataStart;
    int tangentDataStart;

    const r5_vertexFileFixup_t* GetFixupData(int i) const
    {
        return reinterpret_cast<const r5_vertexFileFixup_t*>((const char*)this + fixupTableStart) + i;
    }
    const r5_mstudiovertex_t* GetVertexData(int i) const
    {
        return reinterpret_cast<const r5_mstudiovertex_t*>((const char*)this + vertexDataStart) + i;
    }
};

struct r5_mstudiobodyparts_t
{
    int sznameindex;
    int nummodels;
    int base;
    int modelindex;
};

struct r5_mstudio_meshvertexdata_t
{
    int modelvertexdata;
    int numLODVertexes[MAX_NUM_LODS_R5];
};

struct r5_mstudiomesh_t
{
    int       material;
    int       modelindex;
    int       numvertices;
    int       vertexoffset;
    int       deprecated_numflexes;
    int       deprecated_flexindex;
    int       deprecated_materialtype;
    int       deprecated_materialparam;
    int       meshid;
    Vector    center;
    r5_mstudio_meshvertexdata_t vertexloddata;
    char      pUnknown[8];
};

struct r5_mstudiomodel_t
{
    char    name[64];
    int     unkStringOffset;
    int     type;
    float   boundingradius;
    int     nummeshes;
    int     meshindex;
    int     numvertices;
    int     vertexindex;
    int     tangentsindex;
    int     numattachments;
    int     attachmentindex;
    int     deprecated_numeyeballs;
    int     deprecated_eyeballindex;
    int     pad[4];
    int     colorindex;
    int     uv2index;
};

// r5_studiohdr_t — only the fields we access
struct r5_studiohdr_t;

//=============================================================================
// BVH4 Constants and Types
//=============================================================================

namespace {

using std::min;
using std::max;

constexpr int BVH4_TYPE_NODE     = 0;
constexpr int BVH4_TYPE_NONE     = 1;
constexpr int BVH4_TYPE_TRISTRIP = 4;  // float vertices
constexpr int BVH4_TYPE_POLY3    = 5;  // packed int16 vertices

constexpr int    MAX_TRIS_PER_LEAF = 16;
constexpr int    MAX_BVH_DEPTH     = 32;
constexpr float  MIN_TRIANGLE_EDGE = 0.1f;
constexpr float  MIN_TRIANGLE_AREA = 0.01f;

//=============================================================================
// BVH4 Node — 64 bytes, matches engine CollBvh4Node_s
//=============================================================================
#pragma pack(push, 1)
struct BVHNode_t
{
    int16_t  bounds[24];  // SOA: [Xmin×4][Xmax×4][Ymin×4][Ymax×4][Zmin×4][Zmax×4]

    int32_t  cmIndex  : 8;
    int32_t  index0   : 24;

    int32_t  padding  : 8;
    int32_t  index1   : 24;

    int32_t  childType0 : 4;
    int32_t  childType1 : 4;
    int32_t  index2     : 24;

    int32_t  childType2 : 4;
    int32_t  childType3 : 4;
    int32_t  index3     : 24;
};
#pragma pack(pop)
static_assert(sizeof(BVHNode_t) == 64, "BVHNode_t must be 64 bytes");

//=============================================================================
// Surface Properties — 8 bytes
//=============================================================================
#pragma pack(push, 1)
struct CollSurfProps_t
{
    uint16_t surfFlags;
    uint8_t  surfTypeID;
    uint8_t  contentsIdx;
    uint32_t nameOffset;
};
#pragma pack(pop)
static_assert(sizeof(CollSurfProps_t) == 8, "CollSurfProps_t must be 8 bytes");

//=============================================================================
// Packed Vertex — 6 bytes (int16 x, y, z)
//=============================================================================
#pragma pack(push, 1)
struct PackedVertex_t
{
    int16_t x, y, z;
};
#pragma pack(pop)
static_assert(sizeof(PackedVertex_t) == 6, "PackedVertex_t must be 6 bytes");

//=============================================================================
// Internal types for BVH building
//=============================================================================
struct CollisionTri_t
{
    Vector v0, v1, v2;
    Vector normal;
};

struct BVHBuildNode_t
{
    Vector mins, maxs;
    int    childIndices[4];
    int    childTypes[4];
    std::vector<int> triangleIndices;
    bool   isLeaf;

    BVHBuildNode_t()
        : mins(FLT_MAX, FLT_MAX, FLT_MAX)
        , maxs(-FLT_MAX, -FLT_MAX, -FLT_MAX)
        , isLeaf(false)
    {
        for (int i = 0; i < 4; i++) {
            childIndices[i] = -1;
            childTypes[i] = BVH4_TYPE_NONE;
        }
    }
};

//=============================================================================
// Builder state
//=============================================================================
static std::vector<CollisionTri_t>  g_tris;
static std::vector<BVHBuildNode_t>  g_buildNodes;
static std::vector<PackedVertex_t>  g_packedVerts;
static std::vector<int32_t>         g_leafData;
static std::vector<BVHNode_t>       g_bvhNodes;

static Vector g_bvhOrigin;
static float  g_bvhScale;

//=============================================================================
// Helper: component-wise min/max
//=============================================================================
static Vector VecMin(const Vector& a, const Vector& b)
{
    return Vector(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z));
}

static Vector VecMax(const Vector& a, const Vector& b)
{
    return Vector(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z));
}

//=============================================================================
// Triangle helpers
//=============================================================================
static float TriangleArea(const CollisionTri_t& tri)
{
    Vector e1 = tri.v1 - tri.v0;
    Vector e2 = tri.v2 - tri.v0;
    Vector cross;
    CrossProduct(e1, e2, cross);
    return VectorLength(cross) * 0.5f;
}

static float MinEdgeLength(const CollisionTri_t& tri)
{
    Vector d0 = tri.v1 - tri.v0;
    Vector d1 = tri.v2 - tri.v1;
    Vector d2 = tri.v0 - tri.v2;
    return min(VectorLength(d0), min(VectorLength(d1), VectorLength(d2)));
}

static bool IsDegenerate(const CollisionTri_t& tri)
{
    return MinEdgeLength(tri) < MIN_TRIANGLE_EDGE || TriangleArea(tri) < MIN_TRIANGLE_AREA;
}

//=============================================================================
// Bounds computation
//=============================================================================
static void ComputeTriBounds(const std::vector<int>& indices, Vector& outMins, Vector& outMaxs)
{
    outMins.Init(FLT_MAX, FLT_MAX, FLT_MAX);
    outMaxs.Init(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (int idx : indices) {
        const CollisionTri_t& t = g_tris[idx];
        outMins = VecMin(outMins, VecMin(VecMin(t.v0, t.v1), t.v2));
        outMaxs = VecMax(outMaxs, VecMax(VecMax(t.v0, t.v1), t.v2));
    }
}

//=============================================================================
// Emit packed vertex — returns index into g_packedVerts
//=============================================================================
static uint32_t EmitPackedVertex(const Vector& worldPos)
{
    float invScale = 1.0f / (g_bvhScale * 65536.0f);

    PackedVertex_t v;
    float px = (worldPos.x - g_bvhOrigin.x) * invScale;
    float py = (worldPos.y - g_bvhOrigin.y) * invScale;
    float pz = (worldPos.z - g_bvhOrigin.z) * invScale;

    v.x = (int16_t)clamp(px, -32768.0f, 32767.0f);
    v.y = (int16_t)clamp(py, -32768.0f, 32767.0f);
    v.z = (int16_t)clamp(pz, -32768.0f, 32767.0f);

    uint32_t idx = (uint32_t)g_packedVerts.size();
    g_packedVerts.push_back(v);
    return idx;
}

//=============================================================================
// Emit Poly3 leaf (Type 5) — packed int16 triangle leaves
//
// Header word: [0-11] surfPropIdx=0, [12-15] numPolys-1, [16-31] baseVertex
// Per-tri word: [0-10] v0 offset, [11-19] v1 delta, [20-28] v2 delta, [29-31] edge flags=7
//
// Vertex winding is swapped (v0, v2, v1) because the engine computes
// normals as (v1-v0) × (v0-v2) which requires reversed winding.
//=============================================================================
static int EmitPoly3Leaf(const std::vector<int>& triIndices)
{
    if (triIndices.empty()) {
        int idx = (int)g_leafData.size();
        g_leafData.push_back(0);
        return idx;
    }

    int numTris = min((int)triIndices.size(), MAX_TRIS_PER_LEAF);
    int leafIndex = (int)g_leafData.size();

    uint32_t baseVertexGlobal = (uint32_t)g_packedVerts.size();

    // Emit vertices — swap v1/v2 for reversed winding
    for (int i = 0; i < numTris; i++) {
        const CollisionTri_t& tri = g_tris[triIndices[i]];
        EmitPackedVertex(tri.v0);
        EmitPackedVertex(tri.v2);  // swapped
        EmitPackedVertex(tri.v1);  // swapped
    }

    uint32_t baseVertexEncoded = baseVertexGlobal >> 10;
    uint32_t surfPropIdx = 0;

    uint32_t header = (surfPropIdx & 0xFFF) | (((numTris - 1) & 0xF) << 12);
    uint32_t headerWord = header | (baseVertexEncoded << 16);
    g_leafData.push_back((int32_t)headerWord);

    uint32_t running_base = baseVertexEncoded << 10;

    for (int i = 0; i < numTris; i++) {
        uint32_t v0_global = baseVertexGlobal + i * 3;
        uint32_t v1_global = v0_global + 1;
        uint32_t v2_global = v0_global + 2;

        uint32_t v0_offset = v0_global - running_base;
        uint32_t v1_delta  = v1_global - (v0_global + 1);
        uint32_t v2_delta  = v2_global - (v0_global + 1);

        constexpr uint32_t EDGE_FLAGS_ALL = 7;
        uint32_t triData = (v0_offset & 0x7FF)
            | ((v1_delta & 0x1FF) << 11)
            | ((v2_delta & 0x1FF) << 20)
            | (EDGE_FLAGS_ALL << 29);
        g_leafData.push_back((int32_t)triData);

        running_base = v0_global;
    }

    return leafIndex;
}

//=============================================================================
// Pack bounds into int16 SOA format
// Layout: [Xmin×4][Xmax×4][Ymin×4][Ymax×4][Zmin×4][Zmax×4]
//=============================================================================
struct ChildBounds { Vector mins, maxs; };

static void PackBoundsToInt16(const ChildBounds bounds[4], int16_t outBounds[24])
{
    float invScale = 1.0f / (g_bvhScale * 65536.0f);

    for (int c = 0; c < 4; c++) {
        float minX = (bounds[c].mins.x - g_bvhOrigin.x) * invScale;
        float minY = (bounds[c].mins.y - g_bvhOrigin.y) * invScale;
        float minZ = (bounds[c].mins.z - g_bvhOrigin.z) * invScale;
        float maxX = (bounds[c].maxs.x - g_bvhOrigin.x) * invScale;
        float maxY = (bounds[c].maxs.y - g_bvhOrigin.y) * invScale;
        float maxZ = (bounds[c].maxs.z - g_bvhOrigin.z) * invScale;

        outBounds[0  + c] = (int16_t)floor(clamp(minX, -32768.0f, 32767.0f));
        outBounds[4  + c] = (int16_t)ceil (clamp(maxX, -32768.0f, 32767.0f));
        outBounds[8  + c] = (int16_t)floor(clamp(minY, -32768.0f, 32767.0f));
        outBounds[12 + c] = (int16_t)ceil (clamp(maxY, -32768.0f, 32767.0f));
        outBounds[16 + c] = (int16_t)floor(clamp(minZ, -32768.0f, 32767.0f));
        outBounds[20 + c] = (int16_t)ceil (clamp(maxZ, -32768.0f, 32767.0f));
    }
}

//=============================================================================
// Partition triangles along longest axis into 2-4 groups
//=============================================================================
static int PartitionTriangles(const std::vector<int>& triIndices,
                              const Vector& mins, const Vector& maxs,
                              std::vector<int> outParts[4])
{
    if ((int)triIndices.size() <= MAX_TRIS_PER_LEAF) {
        outParts[0] = triIndices;
        return 1;
    }

    Vector size = maxs - mins;
    int axis = 0;
    if (size.y > size.x) axis = 1;
    if (size.z > size[axis]) axis = 2;

    std::vector<int> sorted = triIndices;
    std::sort(sorted.begin(), sorted.end(), [axis](int a, int b) {
        const CollisionTri_t& ta = g_tris[a];
        const CollisionTri_t& tb = g_tris[b];
        float ca = (ta.v0[axis] + ta.v1[axis] + ta.v2[axis]) / 3.0f;
        float cb = (tb.v0[axis] + tb.v1[axis] + tb.v2[axis]) / 3.0f;
        return ca < cb;
    });

    size_t count = sorted.size();
    int numPartitions = (count >= 8) ? 4 : 2;

    for (int i = 0; i < 4; i++) outParts[i].clear();

    size_t perPart = (count + numPartitions - 1) / numPartitions;
    for (size_t i = 0; i < count; i++) {
        int p = min((int)(i / perPart), numPartitions - 1);
        outParts[p].push_back(sorted[i]);
    }

    // Compact: remove empty partitions
    int actual = 0;
    for (int i = 0; i < numPartitions; i++) {
        if (!outParts[i].empty()) {
            if (i != actual) {
                outParts[actual] = std::move(outParts[i]);
                outParts[i].clear();
            }
            actual++;
        }
    }

    return actual;
}

//=============================================================================
// Build BVH4 tree recursively
//=============================================================================
static int BuildBVH4Node(const std::vector<int>& triIndices, int depth)
{
    if (triIndices.empty()) return -1;

    int nodeIndex = (int)g_buildNodes.size();
    g_buildNodes.emplace_back();

    ComputeTriBounds(triIndices, g_buildNodes[nodeIndex].mins, g_buildNodes[nodeIndex].maxs);

    // Leaf condition
    if ((int)triIndices.size() <= MAX_TRIS_PER_LEAF || depth >= MAX_BVH_DEPTH) {
        g_buildNodes[nodeIndex].isLeaf = true;
        g_buildNodes[nodeIndex].triangleIndices = triIndices;
        return nodeIndex;
    }

    std::vector<int> partitions[4];
    int numParts = PartitionTriangles(triIndices,
        g_buildNodes[nodeIndex].mins, g_buildNodes[nodeIndex].maxs, partitions);

    if (numParts <= 1) {
        g_buildNodes[nodeIndex].isLeaf = true;
        g_buildNodes[nodeIndex].triangleIndices = triIndices;
        return nodeIndex;
    }

    g_buildNodes[nodeIndex].isLeaf = false;

    for (int i = 0; i < 4; i++) {
        if (i < numParts && !partitions[i].empty()) {
            if ((int)partitions[i].size() <= MAX_TRIS_PER_LEAF) {
                // Make a leaf child
                int leafIdx = (int)g_buildNodes.size();
                g_buildNodes.emplace_back();
                ComputeTriBounds(partitions[i],
                    g_buildNodes[leafIdx].mins, g_buildNodes[leafIdx].maxs);
                g_buildNodes[leafIdx].isLeaf = true;
                g_buildNodes[leafIdx].triangleIndices = partitions[i];
                g_buildNodes[nodeIndex].childIndices[i] = leafIdx;
                g_buildNodes[nodeIndex].childTypes[i] = BVH4_TYPE_POLY3;
            } else {
                int childIdx = BuildBVH4Node(partitions[i], depth + 1);
                g_buildNodes[nodeIndex].childIndices[i] = childIdx;
                if (childIdx >= 0) {
                    if (g_buildNodes[childIdx].isLeaf)
                        g_buildNodes[nodeIndex].childTypes[i] = BVH4_TYPE_POLY3;
                    else
                        g_buildNodes[nodeIndex].childTypes[i] = BVH4_TYPE_NODE;
                }
            }
        } else {
            g_buildNodes[nodeIndex].childIndices[i] = -1;
            g_buildNodes[nodeIndex].childTypes[i] = BVH4_TYPE_NONE;
        }
    }

    return nodeIndex;
}

//=============================================================================
// Emit BVH4 nodes — serializes build tree into final BVHNode_t array
//=============================================================================
static int EmitBVH4Nodes(int buildIdx)
{
    if (buildIdx < 0 || buildIdx >= (int)g_buildNodes.size())
        return -1;

    const BVHBuildNode_t& build = g_buildNodes[buildIdx];

    int nodeIdx = (int)g_bvhNodes.size();
    g_bvhNodes.emplace_back();
    memset(&g_bvhNodes[nodeIdx], 0, sizeof(BVHNode_t));

    // Contents mask index — 0 for CONTENTS_SOLID (first/only entry)
    g_bvhNodes[nodeIdx].cmIndex = 0;

    if (build.isLeaf) {
        // Leaf node: single child with triangle data
        g_bvhNodes[nodeIdx].childType0 = BVH4_TYPE_POLY3;
        g_bvhNodes[nodeIdx].childType1 = BVH4_TYPE_NONE;
        g_bvhNodes[nodeIdx].childType2 = BVH4_TYPE_NONE;
        g_bvhNodes[nodeIdx].childType3 = BVH4_TYPE_NONE;

        g_bvhNodes[nodeIdx].index0 = EmitPoly3Leaf(build.triangleIndices);
        g_bvhNodes[nodeIdx].index1 = 0;
        g_bvhNodes[nodeIdx].index2 = 0;
        g_bvhNodes[nodeIdx].index3 = 0;

        // Bounds: child 0 = leaf bounds, children 1-3 = inverted (degenerate)
        ChildBounds cb[4];
        cb[0].mins = build.mins;
        cb[0].maxs = build.maxs;
        // Invert bounds for unused children to prevent BVH traversal
        for (int i = 1; i < 4; i++) {
            cb[i].mins = build.maxs;
            cb[i].maxs = build.mins;
        }
        PackBoundsToInt16(cb, g_bvhNodes[nodeIdx].bounds);
    } else {
        // Internal node with up to 4 children
        ChildBounds cb[4];

        for (int i = 0; i < 4; i++) {
            if (build.childIndices[i] >= 0) {
                cb[i].mins = g_buildNodes[build.childIndices[i]].mins;
                cb[i].maxs = g_buildNodes[build.childIndices[i]].maxs;
            } else {
                // Invert bounds for empty children
                cb[i].mins = build.maxs;
                cb[i].maxs = build.mins;
            }
        }
        PackBoundsToInt16(cb, g_bvhNodes[nodeIdx].bounds);

        g_bvhNodes[nodeIdx].childType0 = build.childTypes[0];
        g_bvhNodes[nodeIdx].childType1 = build.childTypes[1];
        g_bvhNodes[nodeIdx].childType2 = build.childTypes[2];
        g_bvhNodes[nodeIdx].childType3 = build.childTypes[3];

        for (int i = 0; i < 4; i++) {
            int childIndex = 0;
            int childType = build.childTypes[i];

            if (build.childIndices[i] >= 0) {
                if (childType == BVH4_TYPE_NODE) {
                    childIndex = EmitBVH4Nodes(build.childIndices[i]);
                } else if (childType != BVH4_TYPE_NONE) {
                    // Leaf type — emit triangle data
                    const BVHBuildNode_t& childBuild = g_buildNodes[build.childIndices[i]];
                    childIndex = EmitPoly3Leaf(childBuild.triangleIndices);
                }
            }

            switch (i) {
            case 0: g_bvhNodes[nodeIdx].index0 = childIndex; break;
            case 1: g_bvhNodes[nodeIdx].index1 = childIndex; break;
            case 2: g_bvhNodes[nodeIdx].index2 = childIndex; break;
            case 3: g_bvhNodes[nodeIdx].index3 = childIndex; break;
            }
        }
    }

    return nodeIdx;
}

//=============================================================================
// Collect triangles from VVD/VTX for LOD 0
//=============================================================================
static void CollectTriangles(
    const studiohdr_t* pOldHdr,
    const OptimizedModel::FileHeader_t* pVTX,
    const r5_vertexFileHeader_t* pVVD)
{
    g_tris.clear();
    int skipped = 0, total = 0;

    // Build LOD 0 vertex array (apply fixups)
    std::vector<const r5_mstudiovertex_t*> lodVerts;

    if (pVVD->numFixups == 0) {
        for (int i = 0; i < pVVD->numLODVertexes[0]; i++)
            lodVerts.push_back(pVVD->GetVertexData(i));
    } else {
        for (int f = 0; f < pVVD->numFixups; f++) {
            const r5_vertexFileFixup_t* fix = pVVD->GetFixupData(f);
            if (fix->lod <= 0) {
                for (int v = 0; v < fix->numVertexes; v++)
                    lodVerts.push_back(pVVD->GetVertexData(fix->sourceVertexID + v));
            }
        }
    }

    for (int bpIdx = 0; bpIdx < pVTX->numBodyParts; bpIdx++) {
        const OptimizedModel::BodyPartHeader_t* pVtxBP = pVTX->pBodyPart(bpIdx);
        const mstudiobodyparts_t* pMdlBP = pOldHdr->pBodypart(bpIdx);

        for (int mdlIdx = 0; mdlIdx < pVtxBP->numModels; mdlIdx++) {
            const OptimizedModel::ModelHeader_t* pVtxMdl = pVtxBP->pModel(mdlIdx);
            const mstudiomodel_t* pMdlMdl = pMdlBP->pModel(mdlIdx);

            if (pVTX->numLODs == 0) continue;
            const OptimizedModel::ModelLODHeader_t* pVtxLOD = pVtxMdl->pLOD(0);

            int localVertBase = 0;

            for (int meshIdx = 0; meshIdx < pVtxLOD->numMeshes; meshIdx++) {
                const OptimizedModel::MeshHeader_t* pVtxMesh = pVtxLOD->pMesh(meshIdx);
                const mstudiomesh_t* pMdlMesh = pMdlMdl->pMesh(meshIdx);

                for (int sgIdx = 0; sgIdx < pVtxMesh->numStripGroups; sgIdx++) {
                    const OptimizedModel::StripGroupHeader_t* pSG = pVtxMesh->pStripGroup(sgIdx);

                    // Build local vertex map for this strip group
                    int numSGVerts = pSG->numVerts;
                    std::vector<int> vertMap(numSGVerts, -1);
                    for (int vi = 0; vi < numSGVerts; vi++) {
                        const OptimizedModel::Vertex_t* pVert = pSG->pVertex(vi);
                        int vvdIdx = localVertBase + pVert->origMeshVertID;
                        if (vvdIdx >= 0 && vvdIdx < (int)lodVerts.size())
                            vertMap[vi] = vvdIdx;
                    }

                    // Extract triangles from indices
                    int numIdx = pSG->numIndices;
                    for (int ii = 0; ii + 2 < numIdx; ii += 3) {
                        total++;
                        unsigned short i0 = *pSG->pIndex(ii);
                        unsigned short i1 = *pSG->pIndex(ii + 1);
                        unsigned short i2 = *pSG->pIndex(ii + 2);

                        if (i0 >= numSGVerts || i1 >= numSGVerts || i2 >= numSGVerts) continue;
                        int vi0 = vertMap[i0], vi1 = vertMap[i1], vi2 = vertMap[i2];
                        if (vi0 < 0 || vi1 < 0 || vi2 < 0) continue;

                        CollisionTri_t tri;
                        tri.v0 = lodVerts[vi0]->m_vecPosition;
                        tri.v1 = lodVerts[vi1]->m_vecPosition;
                        tri.v2 = lodVerts[vi2]->m_vecPosition;

                        Vector e1 = tri.v1 - tri.v0;
                        Vector e2 = tri.v2 - tri.v0;
                        CrossProduct(e1, e2, tri.normal);
                        float len = VectorLength(tri.normal);
                        if (len > 0.0001f)
                            tri.normal = tri.normal * (1.0f / len);

                        if (IsDegenerate(tri)) { skipped++; continue; }

                        g_tris.push_back(tri);
                    }
                }

                localVertBase += pMdlMesh->numvertices;
            }
        }
    }

    printf("  [BVH] Collected %d collision triangles (%d degenerate skipped from %d total)\n",
        (int)g_tris.size(), skipped, total);
}

} // anonymous namespace

//=============================================================================
// R5_BuildBVHCollision — public entry point
//
// Builds a self-contained BVH4 collision blob.
// Returns empty vector if there are no valid triangles.
//=============================================================================
std::vector<uint8_t> R5_BuildBVHCollision(
    const void* pHdrVoid,
    const OptimizedModel::FileHeader_t* pVTX,
    const r5_vertexFileHeader_t* pVVD)
{
    const studiohdr_t* pOldHdr = reinterpret_cast<const studiohdr_t*>(pHdrVoid);

    // Clear state
    g_tris.clear();
    g_buildNodes.clear();
    g_packedVerts.clear();
    g_leafData.clear();
    g_bvhNodes.clear();

    // Step 1: Collect triangles from LOD 0
    CollectTriangles(pOldHdr, pVTX, pVVD);
    if (g_tris.empty()) {
        printf("  [BVH] No collision triangles, skipping BVH generation\n");
        return {};
    }

    // Step 2: Compute overall bounds and BVH scale
    Vector overallMins(FLT_MAX, FLT_MAX, FLT_MAX);
    Vector overallMaxs(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const CollisionTri_t& t : g_tris) {
        overallMins = VecMin(overallMins, VecMin(VecMin(t.v0, t.v1), t.v2));
        overallMaxs = VecMax(overallMaxs, VecMax(VecMax(t.v0, t.v1), t.v2));
    }

    Vector center = (overallMins + overallMaxs) * 0.5f;
    Vector extents = (overallMaxs - overallMins) * 0.5f;
    float maxExtent = max(extents.x, max(extents.y, max(extents.z, 1.0f)));

    if (maxExtent <= 32000.0f)
        g_bvhScale = 1.0f / 65536.0f;
    else
        g_bvhScale = maxExtent / (32000.0f * 65536.0f);

    g_bvhOrigin = center;

    // Step 3: Build BVH4 tree
    std::vector<int> allIndices(g_tris.size());
    std::iota(allIndices.begin(), allIndices.end(), 0);

    g_buildNodes.clear();
    int rootBuildIdx = BuildBVH4Node(allIndices, 0);

    if (rootBuildIdx < 0) {
        printf("  [BVH] BVH tree build failed\n");
        return {};
    }

    // Step 4: Emit packed vertices, leaf data, and BVH nodes
    g_packedVerts.clear();
    g_leafData.clear();
    g_bvhNodes.clear();

    EmitBVH4Nodes(rootBuildIdx);

    printf("  [BVH] Emitted %d BVH nodes, %d leaf data words, %d packed vertices\n",
        (int)g_bvhNodes.size(), (int)g_leafData.size(), (int)g_packedVerts.size());

    // Step 5: Serialize into a self-contained blob
    //
    // Blob layout:
    //   +0x00: Global header (0x10 bytes)
    //     uint32 contentsMaskOfs
    //     uint32 surfPropsOfs
    //     uint32 surfNameBufOfs
    //     uint32 numParts (1)
    //
    //   +0x10: Per-part data (0x28 bytes per part)
    //     uint32 bvhFlags          (1 = packed int16 verts)
    //     uint32 nodesOfs          (byte offset to BVH nodes)
    //     uint32 packedVertsOfs    (byte offset to packed int16 vertices)
    //     uint32 leafDataOfs       (byte offset to leaf data int32 array)
    //     float  origin[3]
    //     float  scale
    //     uint32 collisionVertsOfs (0 when using packed verts)
    //     uint32 reserved          (0)
    //
    //   Then: surfProps (8 bytes), contentsMask (4 bytes), surfNameBuf (4+ bytes)
    //   Then: packed vertices (6 bytes each)
    //   Then: leaf data (4 bytes each int32)
    //   Then: BVH nodes (64 bytes each, 64-byte aligned)

    const uint32_t headerSize = 0x10 + 0x28; // 0x38

    // Surface property: default (all zeros = default surface)
    CollSurfProps_t surfProp = {};
    surfProp.surfFlags   = 0;
    surfProp.surfTypeID  = 0;
    surfProp.contentsIdx = 0;
    surfProp.nameOffset  = 0;

    uint32_t contentsMask = 0x00000001; // CONTENTS_SOLID

    // Empty surface name (null-terminated, 4-byte padded)
    uint8_t surfNameBuf[4] = { 0, 0, 0, 0 };
    uint32_t surfNameBufSize = 4;

    // Calculate offsets
    uint32_t surfPropsOfs    = headerSize;                                      // 0x38
    uint32_t contentsMaskOfs = surfPropsOfs + sizeof(CollSurfProps_t);          // 0x40
    uint32_t surfNameBufOfs  = contentsMaskOfs + sizeof(uint32_t);              // 0x44

    uint32_t packedVertsOfs  = surfNameBufOfs + surfNameBufSize;                // 0x48
    uint32_t packedVertsSize = (uint32_t)g_packedVerts.size() * sizeof(PackedVertex_t);

    // Align leaf data to 4 bytes
    uint32_t leafDataOfs = packedVertsOfs + packedVertsSize;
    leafDataOfs = (leafDataOfs + 3) & ~3u;
    uint32_t leafDataSize = (uint32_t)g_leafData.size() * sizeof(int32_t);

    // Align BVH nodes to 64 bytes
    uint32_t nodesOfs = leafDataOfs + leafDataSize;
    nodesOfs = (nodesOfs + 63) & ~63u;
    uint32_t nodesSize = (uint32_t)g_bvhNodes.size() * sizeof(BVHNode_t);

    uint32_t totalBlobSize = nodesOfs + nodesSize;
    totalBlobSize = (totalBlobSize + 3) & ~3u;

    // Assemble blob
    std::vector<uint8_t> blob(totalBlobSize, 0);
    auto wr32 = [&blob](uint32_t ofs, uint32_t v) { memcpy(blob.data() + ofs, &v, 4); };
    auto wrf  = [&blob](uint32_t ofs, float v)    { memcpy(blob.data() + ofs, &v, 4); };

    // Global header
    wr32(0x00, contentsMaskOfs);
    wr32(0x04, surfPropsOfs);
    wr32(0x08, surfNameBufOfs);
    wr32(0x0C, 1);  // numParts

    // Per-part data
    wr32(0x10, 1);              // bvhFlags = 1 (packed int16 verts)
    wr32(0x14, nodesOfs);       // nodesOfs
    wr32(0x18, packedVertsOfs); // packedVertsOfs
    wr32(0x1C, leafDataOfs);    // leafDataOfs
    wrf (0x20, center.x);      // origin[0]
    wrf (0x24, center.y);      // origin[1]
    wrf (0x28, center.z);      // origin[2]
    wrf (0x2C, g_bvhScale);    // scale
    wr32(0x30, 0);              // collisionVertsOfs (0 = not used with packed)
    wr32(0x34, 0);              // reserved

    // Payload sections
    memcpy(blob.data() + surfPropsOfs, &surfProp, sizeof(surfProp));
    wr32(contentsMaskOfs, contentsMask);
    memcpy(blob.data() + surfNameBufOfs, surfNameBuf, surfNameBufSize);

    if (!g_packedVerts.empty())
        memcpy(blob.data() + packedVertsOfs, g_packedVerts.data(), packedVertsSize);

    if (!g_leafData.empty())
        memcpy(blob.data() + leafDataOfs, g_leafData.data(), leafDataSize);

    if (!g_bvhNodes.empty())
        memcpy(blob.data() + nodesOfs, g_bvhNodes.data(), nodesSize);

    printf("  [BVH] Blob: %u bytes (verts@0x%X, leaves@0x%X, nodes@0x%X)\n",
        totalBlobSize, packedVertsOfs, leafDataOfs, nodesOfs);

    // Cleanup
    g_tris.clear();
    g_buildNodes.clear();
    g_packedVerts.clear();
    g_leafData.clear();
    g_bvhNodes.clear();

    return blob;
}
