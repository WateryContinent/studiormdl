// rmdl_write.cpp
// Converts MDL v49 + VVD + VTX output from studiomdl into RMDL v54 (increment 10) + VG
// Based on rmdlconv by rexx (GPL v3) - https://github.com/r-ex/rmdlconv
//
// Apex Legends Season 3-4 format (v10, studiohdr version 54)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <algorithm>

// Use studiomdl's own headers for the MDL v49 and VTX structures
#include "studio.h"
#include "optimize.h"
#include "mathlib/mathlib.h"
#include "tier0/icommandline.h"
#include "vphysics_interface.h"
#include "mathlib/polyhedron.h"

// R5-AnimConv integration — produces .rrig/.rseq alongside the .rmdl
#include "animconv_wrapper.h"

// RUI mesh support
#include "rui_parse.h"

// BVH4 collision mesh
#include "rmdl_bvh.h"

#pragma warning(disable: 4996) // strcpy/sprintf safety
#pragma warning(disable: 4244) // float/int conversions

// studiomdl globals — cdtexture search paths set by $cdtexture in the QC.
// Read directly from the global array instead of parsing the MDL binary to
// avoid issues with the studiomdl string-table patching order.
extern int    numcdtextures;
extern char*  cdtextures[16];
extern char   g_outname[MAX_PATH]; // e.g. "weapons\smr\w_smr.mdl"

// Game content root — declared in utils_common/filesystem_tools.cpp.
// rrig/rseq output is placed relative to this directory.
extern char gamedir[1024];

//=============================================================================
// RMDL v54 (Apex Legends) Struct Definitions
// These use r5_ prefix to avoid collisions with studiomdl's existing types.
//=============================================================================

#define R5_IDSTUDIOHEADER  (('T'<<24)+('S'<<16)+('D'<<8)+'I') // 'IDST' little-endian
#define R5_STUDIO_VERSION  54

// VG file magic: '0tVG'
#define VG_FILE_MAGIC   0x47567430
#define VG_FILE_VERSION 1

// Vertex flags for VG meshes
#define VG_VERTEX_HAS_POSITION        0x1   // full float pos (12 bytes)
#define VG_VERTEX_HAS_POSITION_PACKED 0x2   // Vector64 pos (8 bytes)
#define VG_VERTEX_HAS_COLOR           0x10  // 4 bytes
#define VG_VERTEX_HAS_UNK             0x40  // flag only, no size contribution
#define VG_VERTEX_HAS_NORMAL_PACKED   0x200 // packed normal/tangent (4 bytes, uint32)
#define VG_VERTEX_HAS_UNK2            0x800 // flag only
#define VG_VERTEX_HAS_WEIGHT_BONES    0x1000  // 4 bytes (3 bone indices + count)
#define VG_VERTEX_HAS_WEIGHT_VALUES_2 0x4000  // 4 bytes (2 packed weights, uint16x2)
#define VG_VERTEX_HAS_UV1             0x2000000LL // 8 bytes (Vector2D)
#define VG_VERTEX_HAS_UV2             0x200000000LL // 8 bytes (Vector2D)

// Material shader types
#define MATERIAL_TYPE_RGDP 0x1  // rigid/static prop
#define MATERIAL_TYPE_SKNP 0x4  // skinned

// Studiohdr flags we need
#define R5_STUDIOHDR_FLAGS_STATIC_PROP       0x10
#define R5_STUDIOHDR_FLAGS_HAS_PHYSICS_DATA  0x40000
#define R5_STUDIOHDR_FLAGS_USES_VERTEX_COLOR 0x1000000
#define R5_STUDIOHDR_FLAGS_USES_UV2          0x2000000

#define MAX_NUM_LODS_R5 8
#define RMDL_FILEBUF_SIZE (32 * 1024 * 1024)

//-----------------------------------------------------------------------------
// VVD structures (matches Valve VVD format exactly)
//-----------------------------------------------------------------------------
#define VVD_MAX_NUM_BONES_PER_VERT 3

struct r5_mstudioboneweight_t
{
    float   weight[VVD_MAX_NUM_BONES_PER_VERT];
    char    bone[VVD_MAX_NUM_BONES_PER_VERT];
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
    const Vector4D* GetTangentData(int i) const
    {
        return reinterpret_cast<const Vector4D*>((const char*)this + tangentDataStart) + i;
    }
};

//-----------------------------------------------------------------------------
// VG (VertexGroup) structures - rev1, used by RMDL v8-v12
//-----------------------------------------------------------------------------

#pragma pack(push,1)
struct r5_vg_PackedWeights_t
{
    uint16_t weight[2]; // packed weights (divide by 32767 for float)
};

struct r5_vg_PackedBones_t
{
    uint8_t  bones[3];   // bone indices
    uint8_t  numbones;   // number of bones - 1
};
#pragma pack(pop)

// VG LOD header
struct r5_vg_LODHeader_t
{
    uint16_t meshOffset; // index into mesh array
    uint16_t meshCount;
    float    switchPoint;
};

// VG Mesh header (48 bytes)
struct r5_vg_MeshHeader_t
{
    int64_t flags;             // vertex format flags

    uint32_t vertOffset;       // byte offset to start of verts for this mesh
    uint32_t vertCacheSize;    // size of one vertex in bytes
    uint32_t vertCount;        // number of vertices

    int32_t  unk1;             // unused

    int32_t  extraBoneWeightOffset; // offset into extra bone weight buffer
    int32_t  extraBoneWeightSize;   // size in bytes

    int32_t  indexOffset;     // offset into index buffer (in uint16 units)
    int32_t  indexCount;      // number of indices

    int32_t  legacyWeightOffset;   // offset into legacy weight buffer
    int32_t  legacyWeightCount;    // count

    int32_t  stripOffset;     // index into strip array
    int32_t  stripCount;      // number of strips

    int32_t  unk[4];          // unused
};

// VG file header (main)
struct r5_vg_FileHeader_t
{
    int32_t  id;              // 0x47567430 ('0tVG')
    int32_t  version;         // 1
    int32_t  unk;             // 0
    int32_t  dataSize;        // total size

    int64_t  boneStateChangeOffset;
    int64_t  boneStateChangeCount;

    int64_t  meshOffset;
    int64_t  meshCount;

    int64_t  indexOffset;
    int64_t  indexCount;

    int64_t  vertOffset;
    int64_t  vertBufferSize;

    int64_t  extraBoneWeightOffset;
    int64_t  extraBoneWeightSize;

    int64_t  unknownOffset;
    int64_t  unknownCount;

    int64_t  lodOffset;
    int64_t  lodCount;

    int64_t  legacyWeightOffset;
    int64_t  legacyWeightCount;

    int64_t  stripOffset;
    int64_t  stripCount;

    int64_t  unused[8];
};

// VTX Strip header (matches OptimizedModel::StripHeader_t — must be #pragma pack(1) = 35 bytes)
// Without packing, int32_t numBoneStateChanges would be padded to offset 20 instead of 19,
// giving 36 bytes and corrupting every strip after index 0 in the VG file.
#pragma pack(push, 1)
struct r5_vg_StripHeader_t
{
    int32_t  numIndices;
    int32_t  indexOffset;
    int32_t  numVerts;
    int32_t  vertOffset;
    int16_t  numBones;
    uint8_t  flags;
    int32_t  numBoneStateChanges;
    int32_t  boneStateChangeOffset;
    int32_t  numTopologyIndices;
    int32_t  topologyOffset;
};
#pragma pack(pop)
static_assert(sizeof(r5_vg_StripHeader_t) == 35, "r5_vg_StripHeader_t must be 35 bytes (packed)");

//-----------------------------------------------------------------------------
// RMDL v54 structs
//-----------------------------------------------------------------------------

// Bone
struct r5_mstudiobone_t
{
    int       sznameindex;
    int       parent;
    int       bonecontroller[6];
    Vector    pos;
    Quaternion quat;
    RadianEuler rot;
    Vector    scale;   // new in r5 (replaces posscale)
    matrix3x4_t poseToBone;
    Quaternion qAlignment;
    int       flags;
    int       proctype;
    int       procindex;
    int       physicsbone;
    int       surfacepropidx;
    int       contents;
    int       surfacepropLookup;
    int       unk_B0;
    int       collisionIndex;
};

// Jiggle bone (RMDL version)
struct r5_mstudiojigglebone_t
{
    uint8_t flags;
    uint8_t bone;
    uint8_t pad[2];
    float   length, tipMass, tipFriction;
    float   yawStiffness, yawDamping;
    float   pitchStiffness, pitchDamping;
    float   alongStiffness, alongDamping;
    float   angleLimit;
    float   minYaw, maxYaw, yawFriction, yawBounce;
    float   minPitch, maxPitch, pitchFriction, pitchBounce;
    float   baseMass, baseStiffness, baseDamping;
    float   baseMinLeft, baseMaxLeft, baseLeftFriction;
    float   baseMinUp, baseMaxUp, baseUpFriction;
    float   baseMinForward, baseMaxForward, baseForwardFriction;
};

// Attachment (RMDL v54 — no unused[8], 60 bytes total)
struct r5_mstudioattachment_t
{
    int         sznameindex;
    int         flags;
    int         localbone;
    matrix3x4_t localmatrix;
};

// Hitbox set (same as MDL v49)
struct r5_mstudiohitboxset_t
{
    int sznameindex;
    int numhitboxes;
    int hitboxindex;
};

// Hitbox (RMDL v54/v8 — 44 bytes total; MDL v49 had unused[8] here = 32 extra bytes)
struct r5_mstudiobbox_t
{
    int   bone;
    int   group;
    Vector bbmin;
    Vector bbmax;
    int   szhitboxnameindex;
    int   critShotOverride;   // 0 = normal, 1 = acts as head group regardless of group
    int   hitdataGroupOffset; // hit_data group keyvalue offset (was keyvalueindex in MDL)
};

// IK link
struct r5_mstudioiklink_t
{
    int    bone;
    Vector kneeDir;
};

// IK chain
struct r5_mstudioikchain_t
{
    int   sznameindex;
    int   linktype;
    int   numlinks;
    int   linkindex;
    float unk; // default 0.707f
};

// Pose parameter
struct r5_mstudioposeparamdesc_t
{
    int   sznameindex;
    int   flags;
    float start;
    float end;
    float loop;
};

// Texture (r5: has GUID instead of flags)
#pragma pack(push, 4)
struct r5_mstudiotexture_t
{
    int      sznameindex;
    uint64_t textureGuid; // rpak material GUID
};
#pragma pack(pop)

// Source bone transform (same as MDL v49)
struct r5_mstudiosrcbonetransform_t
{
    int         sznameindex;
    matrix3x4_t pretransform;
    matrix3x4_t posttransform;
};

// Sequence descriptor
struct r5_mstudioseqdesc_t
{
    int       baseptr;
    int       szlabelindex;
    int       szactivitynameindex;
    int       flags;
    int       activity;
    int       actweight;
    int       numevents;
    int       eventindex;
    Vector    bbmin;
    Vector    bbmax;
    int       numblends;
    int       animindexindex;
    int       movementindex;
    int       groupsize[2];
    int       paramindex[2];
    float     paramstart[2];
    float     paramend[2];
    int       paramparent;
    float     fadeintime;
    float     fadeouttime;
    int       localentrynode;
    int       localexitnode;
    int       nodeflags;
    float     entryphase;
    float     exitphase;
    float     lastframe;
    int       nextseq;
    int       pose;
    int       numikrules;
    int       numautolayers;
    int       autolayerindex;
    int       weightlistindex;
    int       posekeyindex;
    int       numiklocks;
    int       iklockindex;
    int       keyvalueindex;
    int       keyvaluesize;
    int       cycleposeindex;
    int       activitymodifierindex;
    int       numactivitymodifiers;
    int       unkCount;
    int       unkOffset;
    int       unused[2];  // RMDL v54: 2 unused ints (208 bytes total); MDL v49 had unused[5] here = 12 bytes extra
};

// Anim descriptor (RMDL v54 — 52 bytes total, matches binary template mstudioanimdesc_t_v54)
struct r5_mstudioanimdesc_t
{
    int   baseptr;
    int   sznameindex;
    float fps;
    int   flags;
    int   numframes;
    int   nummovements;
    int   movementindex;
    int   framemovementindex;
    int   animindex;
    int   numikrules;
    int   ikruleindex;
    int   sectionindex;
    int   sectionframes;
};

// Linear bone header (r5 version - 7 fields, 28 bytes header)
struct r5_mstudiolinearbone_t
{
    int numbones;
    int flagsindex;
    int parentindex;
    int posindex;
    int quatindex;
    int rotindex;
    int posetoboneindex;
};

// Main RMDL v54 studiohdr
struct r5_studiohdr_t
{
    int     id;
    int     version;
    int     checksum;
    int     sznameindex;
    char    name[64];
    int     length;

    Vector  eyeposition;
    Vector  illumposition;
    Vector  hull_min;
    Vector  hull_max;
    Vector  view_bbmin;
    Vector  view_bbmax;

    int     flags;

    int     numbones;
    int     boneindex;

    int     numbonecontrollers;
    int     bonecontrollerindex;

    int     numhitboxsets;
    int     hitboxsetindex;

    int     numlocalanim;
    int     localanimindex;

    int     numlocalseq;
    int     localseqindex;

    int     activitylistversion;

    int     materialtypesindex;
    int     numtextures;
    int     textureindex;

    int     numcdtextures;
    int     cdtextureindex;

    int     numskinref;
    int     numskinfamilies;
    int     skinindex;

    int     numbodyparts;
    int     bodypartindex;

    int     numlocalattachments;
    int     localattachmentindex;

    int     numlocalnodes;
    int     localnodeindex;
    int     localnodenameindex;

    int     unkNodeCount;
    int     nodeDataOffsetsOffset;

    int     meshOffset;

    int     deprecated_numflexcontrollers;
    int     deprecated_flexcontrollerindex;

    int     deprecated_numflexrules;
    int     deprecated_flexruleindex;

    int     numikchains;
    int     ikchainindex;

    int     uiPanelCount;
    int     uiPanelOffset;

    int     numlocalposeparameters;
    int     localposeparamindex;

    int     surfacepropindex;

    int     keyvalueindex;
    int     keyvaluesize;

    int     numlocalikautoplaylocks;
    int     localikautoplaylockindex;

    float   mass;
    int     contents;

    int     numincludemodels;
    int     includemodelindex;

    int     virtualModel;

    int     bonetablebynameindex;

    char    constdirectionallightdot;
    char    rootLOD;
    char    numAllowedRootLODs;
    char    unused;

    float   defaultFadeDist;
    float   gatherSize;

    int     deprecated_numflexcontrollerui;
    int     deprecated_flexcontrolleruiindex;

    float   flVertAnimFixedPointScale;
    int     surfacepropLookup;

    int     sourceFilenameOffset;

    int     numsrcbonetransform;
    int     srcbonetransformindex;

    int     illumpositionattachmentindex;
    int     linearboneindex;

    int     procBoneCount;
    int     procBoneTableOffset;
    int     linearProcBoneOffset;

    int     deprecated_m_nBoneFlexDriverCount;
    int     deprecated_m_nBoneFlexDriverIndex;

    int     deprecated_m_nPerTriAABBIndex;
    int     deprecated_m_nPerTriAABBNodeCount;
    int     deprecated_m_nPerTriAABBLeafCount;
    int     deprecated_m_nPerTriAABBVertCount;

    int     unkStringOffset;

    int     vtxOffset;
    int     vvdOffset;
    int     vvcOffset;
    int     phyOffset;

    int     vtxSize;
    int     vvdSize;
    int     vvcSize;
    int     phySize;

    int     deprecated_unkOffset;
    int     deprecated_unkCount;

    int     boneFollowerCount;
    int     boneFollowerOffset;

    Vector  mins;
    Vector  maxs;

    int     unk3[3];
    int     bvhOffset;
    short   unk4[2];

    int     vvwOffset;
    int     vvwSize;
};

// r5 bodyparts / models / meshes (same layout as MDL v49 base parts)
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

// mstudiomesh_t on-disk size is exactly 92 bytes (matches engine r5::v8 layout with pack(4)).
// The reference (rmdlconv studio.h) uses void* pUnknown with #pragma pack(push,4) to get 92
// bytes on 64-bit. On Win32, void* is only 4 bytes, giving 88 bytes — wrong for the engine.
// Using char pUnknown[8] is always 8 bytes on all platforms and requires no pack pragma.
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
    char      pUnknown[8]; // always 8 bytes on any platform (vs void* which is 4 on Win32)
};
static_assert(sizeof(r5_mstudiomesh_t) == 92, "r5_mstudiomesh_t must be 92 bytes");

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

//=============================================================================
// String Table
//=============================================================================

struct r5_stringentry_t
{
    char*       base;
    char*       addr;
    int*        ptr;
    std::string string; // owning copy — prevents dangling when caller's std::string dies
    int         dupindex; // -1 if unique
};

static std::vector<r5_stringentry_t> s_stringTable;
static char* s_pBase  = nullptr;
static char* s_pData  = nullptr;
static r5_studiohdr_t* s_pHdr = nullptr;
static bool s_bZeroGuids    = false; // set by -vmtext flag
static bool s_bConvertAnims = false; // set by -convertanims flag
static bool s_bCdPick       = false; // set by -cdpick flag — interactive per-texture cdmaterials picker
static std::string s_overrideRrigPath;  // set by -rp
static std::string s_overrideRseqPath;  // set by -sp
static std::string s_ruiMeshFilePath;   // set by $ruimeshfile QC command

void SetRuiMeshFile(const char* path)
{
    s_ruiMeshFilePath = path ? path : "";
}

static void R5_BeginStringTable()
{
    s_stringTable.clear();
    s_stringTable.push_back({ nullptr, nullptr, nullptr, "", -1 });
}

static void R5_AddToStringTable(char* base, int* ptr, const char* string)
{
    if (!string) string = "";

    r5_stringentry_t entry{};
    int i = 0;
    for (auto& it : s_stringTable)
    {
        if (it.string == string)
        {
            entry.base     = base;
            entry.ptr      = ptr;
            entry.string   = string;
            entry.dupindex = i;
            s_stringTable.push_back(entry);
            return;
        }
        i++;
    }

    entry.base     = base;
    entry.ptr      = ptr;
    entry.string   = string;
    entry.dupindex = -1;
    s_stringTable.push_back(entry);
}

static char* R5_WriteStringTable(char* pData)
{
    for (auto& it : s_stringTable)
    {
        if (it.dupindex == -1)
        {
            it.addr = pData;
            if (it.ptr)
            {
                *it.ptr = (int)(pData - it.base);
                int len = (int)it.string.size();
                memcpy(pData, it.string.c_str(), len);
                pData += len;
            }
            *pData = '\0';
            pData++;
        }
        else
        {
            *it.ptr = (int)(s_stringTable[it.dupindex].addr - it.base);
        }
    }
    return pData;
}

//=============================================================================
// Alignment helpers
//=============================================================================
static inline void R5_AlignData(char*& p, int align = 4)
{
    intptr_t addr = (intptr_t)p;
    intptr_t pad  = (align - (addr % align)) % align;
    memset(p, 0, pad);
    p += pad;
}

//=============================================================================
// Hashing for material GUIDs
//=============================================================================
static uint64_t R5_HashString(const char* pData)
{
    uint32_t* v1 = (uint32_t*)pData;
    uint64_t  v2 = 0;
    int       v3 = 0;
    uint32_t  v4 = (*v1 - 45 * ((~(*v1 ^ 0x5C5C5C5Cu) >> 7) & (((*v1 ^ 0x5C5C5C5Cu) - 0x1010101) >> 7) & 0x1010101)) & 0xDFDFDFDF;

    uint32_t  i;
    for (i = ~*v1 & (*v1 - 0x1010101u) & 0x80808080u; !i; i = ~*v1 & (*v1 - 0x1010101u) & 0x80808080u)
    {
        uint64_t v6 = v4;
        uint32_t v7 = v1[1];
        ++v1;
        v3 += 4;
        v2 = ((((uint64_t)(0xFB8C4D96501ull * v6) >> 24) + 0x633D5F1ull * v2) >> 61)
           ^ (((uint64_t)(0xFB8C4D96501ull * v6) >> 24) + 0x633D5F1ull * v2);
        v4 = (v7 - 45 * ((~(v7 ^ 0x5C5C5C5Cu) >> 7) & (((v7 ^ 0x5C5C5C5Cu) - 0x1010101u) >> 7) & 0x1010101u)) & 0xDFDFDFDFu;
    }

    int v9 = -1;
    uint32_t v10 = (i & (uint32_t)(-(int)i)) - 1;
    unsigned long v12;
    if (_BitScanReverse(&v12, v10))
        v9 = (int)v12;

    return 0x633D5F1ull * v2
        + ((0xFB8C4D96501ull * (uint64_t)(v4 & v10)) >> 24)
        - 0xAE502812AA7333ull * (uint32_t)(v3 + v9 / 8);
}

//=============================================================================
// Normal/Tangent Packing (from rmdlconv)
//=============================================================================
static uint32_t R5_PackNormalTangent(const Vector& normal, const Vector4D& tangent)
{
    Vector absNml = normal;
    absNml.x = fabsf(absNml.x);
    absNml.y = fabsf(absNml.y);
    absNml.z = fabsf(absNml.z);

    uint8_t idx1 = 0;
    if (absNml.x >= absNml.y && absNml.x >= absNml.z)
        idx1 = 0;
    else if (absNml.y >= absNml.x && absNml.y >= absNml.z)
        idx1 = 1;
    else
        idx1 = 2;

    int idx2 = (0x124u >> (2 * idx1 + 2)) & 3;
    int idx3 = (0x124u >> (2 * idx1 + 4)) & 3;

    float s    = 255.0f / absNml[idx1];
    float val2 = (normal[idx2] * s) + 256.0f;
    float val3 = (normal[idx3] * s) + 256.0f;

    // Build orthonormal tangent frame
    Vector a, b;
    if (normal.z < -0.999899983f)
    {
        a = Vector(0, -1, 0);
        b = Vector(-1, 0, 0);
    }
    else
    {
        float v1 = -1.0f / (1.0f + normal.z);
        float v2 = v1 * (normal.x * normal.y);
        float v3 = v1 * (normal.x * normal.x) + 1.0f;
        float v4 = v1 * (normal.y * normal.y) + 1.0f;
        a = Vector(v3, v2, -normal.x);
        b = Vector(v2, v4, -normal.y);
    }

    // tangent as Vector3
    Vector tanVec(tangent.x, tangent.y, tangent.z);

    float angle = atan2f(DotProduct(tanVec, b), DotProduct(tanVec, a));
    if (angle < 0.0f)
        angle += 2.0f * M_PI * 2.0f; // += 2*pi (360 degrees)
    angle /= 0.00614192151f;

    bool sign      = (normal[idx1] < 0.0f);
    uint8_t binormSign = (tangent.w < 1.0f) ? 1u : 0u;

    return ((uint32_t)binormSign << 31)
         | ((uint32_t)idx1 << 29)
         | ((uint32_t)(sign ? 1 : 0) << 28)
         | ((uint32_t)(unsigned short)(int)val2 << 19)
         | ((uint32_t)(unsigned short)(int)val3 << 10)
         | ((uint32_t)(unsigned short)(int)angle & 0x3FFu);
}

//=============================================================================
// Vector64 Packed Position
//=============================================================================
struct r5_Vector64
{
    uint64_t x : 21;
    uint64_t y : 21;
    uint64_t z : 22;

    void Set(const Vector& v)
    {
        x = (uint64_t)((v.x + 1024.0f) / 0.0009765625f);
        y = (uint64_t)((v.y + 1024.0f) / 0.0009765625f);
        z = (uint64_t)((v.z + 2048.0f) / 0.0009765625f);
    }
};
static_assert(sizeof(r5_Vector64) == 8, "Vector64 must be 8 bytes");

//=============================================================================
// VG Data Builder
//=============================================================================

// Intermediate vertex (all possible fields, only relevant ones get written)
struct r5_VGVertex_t
{
    Vector   pos;              // full float position
    r5_Vector64 posPacked;    // packed position
    r5_vg_PackedWeights_t weights;
    r5_vg_PackedBones_t   bones;
    uint32_t normalTangent;    // packed normal+tangent
    Vector2D texcoord;
    int64_t  meshIndex;        // which mesh owns this vertex (used for write, not in file)
};

struct r5_VGBuilder
{
    r5_vg_FileHeader_t              hdr;
    std::vector<uint8_t>            boneStates;
    std::map<uint8_t, uint8_t>      boneMap;
    std::vector<r5_vg_LODHeader_t>  lods;
    std::vector<r5_vg_MeshHeader_t> meshes;
    std::vector<r5_VGVertex_t>      vertices;
    std::vector<uint16_t>           indices;
    std::vector<r5_vg_StripHeader_t> strips;
    std::vector<r5_mstudioboneweight_t> legacyWeights;
    size_t  currentVertBufferSize;
    int64_t defaultFlags;
};

//-----------------------------------------------------------------------------
// Build bone state remapping from VVD
//-----------------------------------------------------------------------------
static void R5_SetupBoneStates(r5_VGBuilder& b, const r5_vertexFileHeader_t* pVVD)
{
    b.boneMap.clear();
    b.boneStates.clear();

    for (int vi = 0; vi < pVVD->numLODVertexes[0]; vi++)
    {
        const r5_mstudiovertex_t* vert = pVVD->GetVertexData(vi);
        const r5_mstudioboneweight_t& bw = vert->m_BoneWeights;

        for (int bi = 0; bi < bw.numbones && bi < 3; bi++)
        {
            uint8_t bone = (uint8_t)bw.bone[bi];
            if (!b.boneMap.count(bone))
            {
                b.boneMap.insert({ bone, (uint8_t)b.boneStates.size() });
                b.boneStates.push_back(bone);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Calculate vertex size from mesh flags
//-----------------------------------------------------------------------------
static uint32_t R5_CalcVertexSize(int64_t flags)
{
    // Mask out UNK flag (0x40) as it doesn't contribute to size
    flags &= ~VG_VERTEX_HAS_UNK;

    uint32_t size = 0;

    if (flags & VG_VERTEX_HAS_POSITION)        size += 12; // Vector
    if (flags & VG_VERTEX_HAS_POSITION_PACKED) size += 8;  // Vector64
    if (flags & VG_VERTEX_HAS_COLOR)           size += 4;
    if (flags & VG_VERTEX_HAS_NORMAL_PACKED)   size += 4;  // uint32
    // 0x800 (UNK2) doesn't contribute to vertex cache size
    if (flags & VG_VERTEX_HAS_WEIGHT_VALUES_2) size += 4;  // 2x uint16
    if (flags & VG_VERTEX_HAS_WEIGHT_BONES)    size += 4;  // 3x uint8 + count
    if (flags & VG_VERTEX_HAS_UV1)             size += 8;  // Vector2D
    if (flags & VG_VERTEX_HAS_UV2)             size += 8;  // Vector2D

    return size;
}

//-----------------------------------------------------------------------------
// Get vertices from VVD, handling LOD fixups
//-----------------------------------------------------------------------------
static void R5_GetVerticesForLOD(const r5_vertexFileHeader_t* pVVD, int lodLevel,
    std::vector<const r5_mstudiovertex_t*>& outVerts,
    std::vector<const Vector4D*>& outTangents)
{
    outVerts.clear();
    outTangents.clear();

    if (pVVD->numFixups > 0)
    {
        for (int j = 0; j < pVVD->numFixups; j++)
        {
            const r5_vertexFileFixup_t* fix = pVVD->GetFixupData(j);
            if (fix->lod >= lodLevel)
            {
                for (int k = 0; k < fix->numVertexes; k++)
                {
                    outVerts.push_back(pVVD->GetVertexData(fix->sourceVertexID + k));
                    outTangents.push_back(pVVD->GetTangentData(fix->sourceVertexID + k));
                }
            }
        }
    }
    else
    {
        // No fixup tables: all LOD levels share the same underlying vertex pool.
        // VTX origMeshVertID values always index into the full LOD-0 array, so
        // we must always populate lodVerts with the complete LOD-0 set regardless
        // of which LOD we are currently processing.  Using numLODVertexes[lodLevel]
        // here (the reduced per-LOD count) is what causes the "vvdIdx out of range"
        // warnings when building LOD 1+.
        int count = pVVD->numLODVertexes[0];
        for (int j = 0; j < count; j++)
        {
            outVerts.push_back(pVVD->GetVertexData(j));
            outTangents.push_back(pVVD->GetTangentData(j));
        }
    }
}

//-----------------------------------------------------------------------------
// Fill VG builder from VVD+VTX+RMDL header
//-----------------------------------------------------------------------------
static bool R5_BuildVGData(r5_VGBuilder& b,
    const r5_studiohdr_t* pHdr,
    const OptimizedModel::FileHeader_t* pVTX,
    const r5_vertexFileHeader_t* pVVD)
{
    // Check model bounds to decide packed vs full position
    bool isLargeModel = false;
    if (pHdr->hull_min.x < -1023.f || pHdr->hull_max.x > 1023.f ||
        pHdr->hull_min.y < -1023.f || pHdr->hull_max.y > 1023.f ||
        pHdr->hull_min.z < -2047.f || pHdr->hull_max.z > 2047.f)
    {
        isLargeModel = true;
    }

    printf("  [VG] isLargeModel=%s, hull min(%.1f,%.1f,%.1f) max(%.1f,%.1f,%.1f)\n",
        isLargeModel ? "YES" : "NO",
        pHdr->hull_min.x, pHdr->hull_min.y, pHdr->hull_min.z,
        pHdr->hull_max.x, pHdr->hull_max.y, pHdr->hull_max.z);

    // Set default mesh flags
    b.defaultFlags = 0;
    b.defaultFlags |= VG_VERTEX_HAS_UNK;
    b.defaultFlags |= VG_VERTEX_HAS_NORMAL_PACKED;
    b.defaultFlags |= VG_VERTEX_HAS_UNK2;
    b.defaultFlags |= VG_VERTEX_HAS_UV1;

    // Bone states
    if (pHdr->numbones > 2)
        R5_SetupBoneStates(b, pVVD);

    b.hdr.boneStateChangeCount = (int64_t)b.boneStates.size();
    b.hdr.lodCount = pVTX->numLODs;

    b.hdr.id      = VG_FILE_MAGIC;
    b.hdr.version = VG_FILE_VERSION;

    b.currentVertBufferSize = 0;

    std::vector<const r5_mstudiovertex_t*> lodVerts;
    std::vector<const Vector4D*>           lodTangents;

    for (int lodIdx = 0; lodIdx < pVTX->numLODs; lodIdx++)
    {
        R5_GetVerticesForLOD(pVVD, lodIdx, lodVerts, lodTangents);

        r5_vg_LODHeader_t newLOD{};
        newLOD.meshOffset = (uint16_t)b.meshes.size();

        int localVertOffset = 0;

        for (int bpIdx = 0; bpIdx < pVTX->numBodyParts; bpIdx++)
        {
            const OptimizedModel::BodyPartHeader_t* pVtxBP = pVTX->pBodyPart(bpIdx);
            const r5_mstudiobodyparts_t* pMdlBP =
                reinterpret_cast<const r5_mstudiobodyparts_t*>((const char*)pHdr + pHdr->bodypartindex) + bpIdx;

            for (int mdlIdx = 0; mdlIdx < pVtxBP->numModels; mdlIdx++)
            {
                const OptimizedModel::ModelHeader_t* pVtxMdl = pVtxBP->pModel(mdlIdx);
                const r5_mstudiomodel_t* pMdlMdl =
                    reinterpret_cast<const r5_mstudiomodel_t*>((const char*)pMdlBP + pMdlBP->modelindex) + mdlIdx;

                const OptimizedModel::ModelLODHeader_t* pVtxLOD = pVtxMdl->pLOD(lodIdx);
                newLOD.switchPoint = pVtxLOD->switchPoint;

                for (int meshIdx = 0; meshIdx < pVtxLOD->numMeshes; meshIdx++)
                {
                    const OptimizedModel::MeshHeader_t* pVtxMesh = pVtxLOD->pMesh(meshIdx);
                    const r5_mstudiomesh_t* pMdlMesh =
                        reinterpret_cast<const r5_mstudiomesh_t*>((const char*)pMdlMdl + pMdlMdl->meshindex) + meshIdx;

                    int64_t globalMeshIndex = (int64_t)b.meshes.size();

                    r5_vg_MeshHeader_t newMesh{};
                    newMesh.stripOffset = (int32_t)b.strips.size();
                    newMesh.indexOffset = (int32_t)b.indices.size();
                    newMesh.vertOffset  = (uint32_t)b.currentVertBufferSize;
                    newMesh.flags       = b.defaultFlags;

                    // Bone weight flags
                    if (pHdr->numbones > 1)
                    {
                        newMesh.extraBoneWeightOffset = 0; // no extra weights for v49
                        newMesh.legacyWeightOffset    = (int32_t)b.legacyWeights.size();
                        newMesh.flags |= VG_VERTEX_HAS_WEIGHT_BONES;
                        newMesh.flags |= VG_VERTEX_HAS_WEIGHT_VALUES_2;
                    }

                    newMesh.flags |= isLargeModel ? VG_VERTEX_HAS_POSITION : VG_VERTEX_HAS_POSITION_PACKED;

                    // One strip per mesh
                    r5_vg_StripHeader_t hwStrip{};
                    hwStrip.flags = 0x1; // TRILIST

                    newMesh.stripCount = 1;

                    int meshVertBase = 0; // tracks vertex offset within this mesh for multi-stripgroup

                    for (int sgIdx = 0; sgIdx < pVtxMesh->numStripGroups; sgIdx++)
                    {
                        const OptimizedModel::StripGroupHeader_t* pVtxSG = pVtxMesh->pStripGroup(sgIdx);

                        newMesh.vertCount  += pVtxSG->numVerts;
                        newMesh.indexCount += pVtxSG->numIndices;

                        hwStrip.numIndices += pVtxSG->numIndices;
                        hwStrip.numVerts   += pVtxSG->numVerts;
                        if (pVtxSG->numStrips > 0)
                            hwStrip.numBones += pVtxSG->pStrip(0)->numBones;

                        // Process vertices
                        for (int vi = 0; vi < pVtxSG->numVerts; vi++)
                        {
                            const OptimizedModel::Vertex_t* pVtxVert = pVtxSG->pVertex(vi);
                            int vvdIdx = localVertOffset + pVtxVert->origMeshVertID;

                            if (vvdIdx >= (int)lodVerts.size())
                            {
                                printf("  [VG] WARNING: vvdIdx %d out of range (max %d), clamping\n",
                                    vvdIdx, (int)lodVerts.size() - 1);
                                vvdIdx = (int)lodVerts.size() - 1;
                            }

                            const r5_mstudiovertex_t* pVVDVert = lodVerts[vvdIdx];
                            const Vector4D* pTangent = lodTangents[vvdIdx];

                            r5_VGVertex_t hwVert{};
                            hwVert.meshIndex   = globalMeshIndex;
                            hwVert.pos         = pVVDVert->m_vecPosition;
                            hwVert.posPacked.Set(pVVDVert->m_vecPosition);
                            hwVert.normalTangent = R5_PackNormalTangent(pVVDVert->m_vecNormal, *pTangent);
                            hwVert.texcoord    = pVVDVert->m_vecTexCoord;

                            if (newMesh.flags & VG_VERTEX_HAS_WEIGHT_BONES)
                            {
                                const r5_mstudioboneweight_t& bw = pVVDVert->m_BoneWeights;

                                for (int bi = 0; bi < bw.numbones && bi < 3; bi++)
                                {
                                    uint8_t bone = (uint8_t)bw.bone[bi];
                                    uint8_t remapped = bone;
                                    if (b.boneMap.count(bone))
                                        remapped = b.boneMap[bone];

                                    hwVert.bones.bones[bi] = remapped;

                                    if (bi < 2)
                                        hwVert.weights.weight[bi] = (uint16_t)(bw.weight[bi] * 32767.0f);
                                }

                                hwVert.bones.numbones = (uint8_t)(bw.numbones > 0 ? bw.numbones - 1 : 0);

                                b.legacyWeights.push_back(bw);
                                newMesh.legacyWeightCount++;
                            }

                            b.vertices.push_back(hwVert);
                        }

                        // Process indices with per-stripgroup base offset
                        int indicesStart = (int)b.indices.size();
                        b.indices.resize(b.indices.size() + pVtxSG->numIndices);
                        const uint16_t* srcIdx = reinterpret_cast<const uint16_t*>(
                            (const char*)pVtxSG + pVtxSG->indexOffset);
                        for (int ii = 0; ii < pVtxSG->numIndices; ii++)
                            b.indices[indicesStart + ii] = srcIdx[ii] + (uint16_t)meshVertBase;

                        meshVertBase += pVtxSG->numVerts;
                    }

                    b.strips.push_back(hwStrip);

                    if (newMesh.vertCount == 0)
                    {
                        newMesh.flags = 0;
                        newMesh.stripCount = 0;
                        b.strips.pop_back();  // undo the strip pushed above; empty meshes have no strips
                    }

                    newMesh.vertCacheSize = R5_CalcVertexSize(newMesh.flags & ~VG_VERTEX_HAS_UNK);
                    b.currentVertBufferSize += (size_t)newMesh.vertCacheSize * newMesh.vertCount;

                    b.hdr.indexCount += newMesh.indexCount;
                    b.hdr.stripCount += newMesh.stripCount;
                    b.hdr.legacyWeightCount += newMesh.legacyWeightCount;
                    b.hdr.meshCount++;
                    newLOD.meshCount++;

                    // Advance the vertex-pool offset for the next mesh.
                    //
                    // Fixup path: numLODVertexes[lodIdx] is the authoritative count of
                    // vertices this mesh contributes to lodVerts at the current LOD.
                    // A value of 0 is legitimate — it means the mesh is fully stripped at
                    // this LOD and adds nothing to lodVerts, so the stride is 0.
                    // DO NOT fall back to pMdlMesh->numvertices here: that is the LOD-0
                    // count and using it for a stripped mesh inflates localVertOffset,
                    // pushing every subsequent mesh's vvdIdx out of range.
                    //
                    // No-fixup path: all LOD levels reference the same LOD-0 vertex pool,
                    // so always stride by the LOD-0 count regardless of lodIdx.
                    int meshVertCount;
                    if (pVVD->numFixups > 0)
                    {
                        // Trust numLODVertexes directly — 0 means 0 contribution.
                        meshVertCount = pMdlMesh->vertexloddata.numLODVertexes[lodIdx];
                    }
                    else
                    {
                        // No fixups — always stride by the LOD-0 count.
                        meshVertCount = pMdlMesh->numvertices;
                        if (meshVertCount == 0)
                            meshVertCount = pMdlMesh->vertexloddata.numLODVertexes[0];
                    }
                    localVertOffset += meshVertCount;

                    b.meshes.push_back(newMesh);
                }
            }
        }

        b.lods.push_back(newLOD);
    }

    printf("  [VG] Built: %zu meshes, %zu verts, %zu indices, %zu strips\n",
        b.meshes.size(), b.vertices.size(), b.indices.size(), b.strips.size());

    return true;
}

//-----------------------------------------------------------------------------
// Write vertex data for one vertex based on mesh flags
//-----------------------------------------------------------------------------
static void R5_WriteVertex(FILE* f, const r5_VGVertex_t& v, int64_t flags)
{
    if (flags & VG_VERTEX_HAS_POSITION_PACKED)
        fwrite(&v.posPacked, sizeof(r5_Vector64), 1, f);
    else if (flags & VG_VERTEX_HAS_POSITION)
        fwrite(&v.pos, sizeof(Vector), 1, f);

    if (flags & VG_VERTEX_HAS_WEIGHT_VALUES_2)
        fwrite(&v.weights, sizeof(r5_vg_PackedWeights_t), 1, f);

    if (flags & VG_VERTEX_HAS_WEIGHT_BONES)
        fwrite(&v.bones, sizeof(r5_vg_PackedBones_t), 1, f);

    fwrite(&v.normalTangent, sizeof(uint32_t), 1, f);

    if (flags & VG_VERTEX_HAS_UV1)
        fwrite(&v.texcoord, sizeof(Vector2D), 1, f);
}

//-----------------------------------------------------------------------------
// Write VG file to disk
//-----------------------------------------------------------------------------
static void R5_WriteVGFile(const char* path, r5_VGBuilder& b)
{
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        printf("ERROR: failed to open VG output '%s'\n", path);
        return;
    }

    b.hdr.unknownCount = b.hdr.meshCount / b.hdr.lodCount;

    // Write header placeholder (we'll rewrite at the end)
    fwrite(&b.hdr, sizeof(r5_vg_FileHeader_t), 1, f);

    // Bone states
    b.hdr.boneStateChangeOffset = (int64_t)ftell(f);
    fwrite(b.boneStates.data(), 1, b.boneStates.size(), f);

    // Meshes
    b.hdr.meshOffset = (int64_t)ftell(f);
    fwrite(b.meshes.data(), sizeof(r5_vg_MeshHeader_t), b.meshes.size(), f);

    // Indices
    b.hdr.indexOffset = (int64_t)ftell(f);
    fwrite(b.indices.data(), sizeof(uint16_t), b.indices.size(), f);

    // Vertices
    b.hdr.vertOffset = (int64_t)ftell(f);
    for (size_t vi = 0; vi < b.vertices.size(); vi++)
    {
        const r5_VGVertex_t& vert = b.vertices[vi];
        int64_t meshFlags = 0;
        if (vert.meshIndex >= 0 && vert.meshIndex < (int64_t)b.meshes.size())
            meshFlags = b.meshes[(size_t)vert.meshIndex].flags;
        R5_WriteVertex(f, vert, meshFlags);
    }
    b.hdr.vertBufferSize = (int64_t)ftell(f) - b.hdr.vertOffset;

    // Extra bone weights (none for v49 - no VVW)
    b.hdr.extraBoneWeightOffset = (int64_t)ftell(f);
    b.hdr.extraBoneWeightSize   = 0;

    // Unknown per-mesh data (0x30 bytes each, all zeros)
    size_t unkCount = (size_t)b.hdr.unknownCount;
    b.hdr.unknownOffset = (int64_t)ftell(f);
    std::vector<uint8_t> unkBuf(unkCount * 0x30, 0);
    fwrite(unkBuf.data(), 1, unkBuf.size(), f);

    // LODs
    b.hdr.lodOffset = (int64_t)ftell(f);
    fwrite(b.lods.data(), sizeof(r5_vg_LODHeader_t), b.lods.size(), f);

    // Legacy bone weights
    b.hdr.legacyWeightOffset = (int64_t)ftell(f);
    fwrite(b.legacyWeights.data(), sizeof(r5_mstudioboneweight_t), b.legacyWeights.size(), f);

    // Strips
    b.hdr.stripOffset = (int64_t)ftell(f);
    fwrite(b.strips.data(), sizeof(r5_vg_StripHeader_t), b.strips.size(), f);

    b.hdr.dataSize = (int32_t)ftell(f);

    // Rewrite header with filled offsets
    fseek(f, 0, SEEK_SET);
    fwrite(&b.hdr, sizeof(r5_vg_FileHeader_t), 1, f);

    fclose(f);

    printf("  [VG] Written: %s (%d bytes)\n", path, b.hdr.dataSize);
}

//=============================================================================
// MDL v49 → RMDL v54 Conversion
//=============================================================================

#define RMDL_STRING_FROM_IDX(base, idx) ((const char*)(base) + (idx))

static void R5_ConvertBones(const studiohdr_t* pOld)
{
    int numBones = pOld->numbones;
    printf("  [RMDL] Converting %d bones...\n", numBones);

    std::vector<r5_mstudiojigglebone_t> jiggleBones;
    std::map<uint8_t, uint8_t> linearProcBones;

    char* boneBase = s_pData;
    s_pHdr->boneindex = (int)(s_pData - s_pBase);

    for (int i = 0; i < numBones; i++)
    {
        const mstudiobone_t* pOldBone = pOld->pBone(i);
        r5_mstudiobone_t*    pNewBone = reinterpret_cast<r5_mstudiobone_t*>(s_pData) + i;

        memset(pNewBone, 0, sizeof(r5_mstudiobone_t));

        R5_AddToStringTable((char*)pNewBone, &pNewBone->sznameindex,
            RMDL_STRING_FROM_IDX(pOldBone, pOldBone->sznameindex));
        R5_AddToStringTable((char*)pNewBone, &pNewBone->surfacepropidx,
            RMDL_STRING_FROM_IDX(pOldBone, pOldBone->surfacepropidx));

        pNewBone->parent = pOldBone->parent;
        memcpy(&pNewBone->bonecontroller, &pOldBone->bonecontroller, sizeof(pOldBone->bonecontroller));
        pNewBone->pos        = pOldBone->pos;
        pNewBone->quat       = pOldBone->quat;
        pNewBone->rot        = pOldBone->rot;
        pNewBone->scale      = Vector(1, 1, 1); // r5 adds scale field
        pNewBone->poseToBone = pOldBone->poseToBone;
        pNewBone->qAlignment = pOldBone->qAlignment;
        pNewBone->flags      = pOldBone->flags;
        pNewBone->proctype   = pOldBone->proctype;
        pNewBone->procindex  = pOldBone->procindex;
        pNewBone->physicsbone = pOldBone->physicsbone;
        pNewBone->contents   = pOldBone->contents;
        pNewBone->surfacepropLookup = pOldBone->surfacepropLookup;
    }

    s_pData += numBones * sizeof(r5_mstudiobone_t);
    R5_AlignData(s_pData);

    // Jiggle bones - check proc bones
    for (int i = 0; i < numBones; i++)
    {
        const mstudiobone_t* pOldBone = pOld->pBone(i);
        if (pOldBone->proctype != 5) continue; // proctype 5 = jiggle

        r5_mstudiobone_t* pNewBone = reinterpret_cast<r5_mstudiobone_t*>(boneBase) + i;

        // Note: for Portal 2 v49 models, jiggle bone data is at procindex offset from bone
        // r5 jiggle bones are smaller, convert key fields
        const mstudiojigglebone_t* pOldJiggle = reinterpret_cast<const mstudiojigglebone_t*>(
            (const char*)pOldBone + pOldBone->procindex);

        r5_mstudiojigglebone_t* pNewJiggle = reinterpret_cast<r5_mstudiojigglebone_t*>(s_pData);

        pNewBone->procindex = (int)((char*)pNewJiggle - (char*)pNewBone);

        // Convert jiggle flags
        uint8_t oldFlags = (uint8_t)(pOldJiggle->flags & 0xFF);
        uint8_t newFlags = oldFlags & ~(0x01u | 0x02u); // clear IS_FLEXIBLE and IS_RIGID
        if (oldFlags & (0x01 | 0x02)) // has tip flex
        {
            newFlags |= 0x02; // HAS_TIP_FLEX in r5
            if (oldFlags & 0x01) newFlags |= 0x01; // IS_FLEXIBLE
        }

        pNewJiggle->flags   = newFlags;
        pNewJiggle->bone    = (uint8_t)i;
        pNewJiggle->length  = pOldJiggle->length;
        pNewJiggle->tipMass = pOldJiggle->tipMass;
        pNewJiggle->yawStiffness  = pOldJiggle->yawStiffness;
        pNewJiggle->yawDamping    = pOldJiggle->yawDamping;
        pNewJiggle->pitchStiffness = pOldJiggle->pitchStiffness;
        pNewJiggle->pitchDamping  = pOldJiggle->pitchDamping;
        pNewJiggle->alongStiffness = pOldJiggle->alongStiffness;
        pNewJiggle->alongDamping  = pOldJiggle->alongDamping;
        pNewJiggle->angleLimit    = pOldJiggle->angleLimit;
        pNewJiggle->minYaw = pOldJiggle->minYaw; pNewJiggle->maxYaw = pOldJiggle->maxYaw;
        pNewJiggle->yawFriction = pOldJiggle->yawFriction; pNewJiggle->yawBounce = pOldJiggle->yawBounce;
        pNewJiggle->baseMass    = pOldJiggle->baseMass;    pNewJiggle->baseStiffness = pOldJiggle->baseStiffness;
        pNewJiggle->baseDamping = pOldJiggle->baseDamping;
        pNewJiggle->baseMinLeft = pOldJiggle->baseMinLeft; pNewJiggle->baseMaxLeft = pOldJiggle->baseMaxLeft;
        pNewJiggle->baseLeftFriction = pOldJiggle->baseLeftFriction;
        pNewJiggle->baseMinUp   = pOldJiggle->baseMinUp;   pNewJiggle->baseMaxUp = pOldJiggle->baseMaxUp;
        pNewJiggle->baseUpFriction = pOldJiggle->baseUpFriction;
        pNewJiggle->baseMinForward = pOldJiggle->baseMinForward; pNewJiggle->baseMaxForward = pOldJiggle->baseMaxForward;
        pNewJiggle->baseForwardFriction = pOldJiggle->baseForwardFriction;
        pNewJiggle->minPitch = pOldJiggle->minPitch; pNewJiggle->maxPitch = pOldJiggle->maxPitch;
        pNewJiggle->pitchFriction = pOldJiggle->pitchFriction; pNewJiggle->pitchBounce = pOldJiggle->pitchBounce;

        linearProcBones.emplace((uint8_t)i, (uint8_t)linearProcBones.size());
        s_pData += sizeof(r5_mstudiojigglebone_t);
    }

    R5_AlignData(s_pData);

    if (!linearProcBones.empty())
    {
        s_pHdr->procBoneCount = (int)linearProcBones.size();
        s_pHdr->procBoneTableOffset = (int)(s_pData - s_pBase);

        for (auto& p : linearProcBones)
        {
            *s_pData = p.first;
            s_pData++;
        }

        s_pHdr->linearProcBoneOffset = (int)(s_pData - s_pBase);

        for (int i = 0; i < numBones; i++)
        {
            *s_pData = linearProcBones.count((uint8_t)i) ? linearProcBones[(uint8_t)i] : 0xFF;
            s_pData++;
        }

        R5_AlignData(s_pData);
    }
}

static void R5_ConvertAttachments(const studiohdr_t* pOld)
{
    int num = pOld->numlocalattachments;
    if (num <= 0) return;

    printf("  [RMDL] Converting %d attachments...\n", num);
    s_pHdr->localattachmentindex = (int)(s_pData - s_pBase);

    for (int i = 0; i < num; i++)
    {
        const mstudioattachment_t* pOldAtt = pOld->pLocalAttachment(i);
        r5_mstudioattachment_t*    pNewAtt = reinterpret_cast<r5_mstudioattachment_t*>(s_pData) + i;

        memset(pNewAtt, 0, sizeof(r5_mstudioattachment_t));
        R5_AddToStringTable((char*)pNewAtt, &pNewAtt->sznameindex,
            RMDL_STRING_FROM_IDX(pOldAtt, pOldAtt->sznameindex));

        pNewAtt->flags     = pOldAtt->flags;
        pNewAtt->localbone = pOldAtt->localbone;
        memcpy(&pNewAtt->localmatrix, &pOldAtt->local, sizeof(matrix3x4_t));
    }

    s_pData += num * sizeof(r5_mstudioattachment_t);
    R5_AlignData(s_pData);
}

static void R5_ConvertHitboxes(const studiohdr_t* pOld)
{
    int numSets = pOld->numhitboxsets;
    printf("  [RMDL] Converting %d hitbox sets...\n", numSets);

    s_pHdr->hitboxsetindex = (int)(s_pData - s_pBase);

    r5_mstudiohitboxset_t* newSets = reinterpret_cast<r5_mstudiohitboxset_t*>(s_pData);

    for (int i = 0; i < numSets; i++)
    {
        const mstudiohitboxset_t* pOldSet = pOld->pHitboxSet(i);
        r5_mstudiohitboxset_t*    pNewSet = newSets + i;

        R5_AddToStringTable((char*)pNewSet, &pNewSet->sznameindex,
            RMDL_STRING_FROM_IDX(pOldSet, pOldSet->sznameindex));

        pNewSet->numhitboxes = pOldSet->numhitboxes;
        s_pData += sizeof(r5_mstudiohitboxset_t);
    }

    // Write hitboxes for each set
    for (int i = 0; i < numSets; i++)
    {
        const mstudiohitboxset_t* pOldSet = pOld->pHitboxSet(i);
        newSets[i].hitboxindex = (int)(s_pData - (char*)(newSets + i));

        for (int j = 0; j < pOldSet->numhitboxes; j++)
        {
            const mstudiobbox_t* pOldBox = pOldSet->pHitbox(j);
            r5_mstudiobbox_t*    pNewBox = reinterpret_cast<r5_mstudiobbox_t*>(s_pData);

            pNewBox->bone             = pOldBox->bone;
            pNewBox->group            = pOldBox->group;
            pNewBox->bbmin            = pOldBox->bbmin;
            pNewBox->bbmax            = pOldBox->bbmax;
            pNewBox->critShotOverride = 0; // no crit override

            R5_AddToStringTable((char*)pNewBox, &pNewBox->szhitboxnameindex,
                RMDL_STRING_FROM_IDX(pOldBox, pOldBox->szhitboxnameindex));
            R5_AddToStringTable((char*)pNewBox, &pNewBox->hitdataGroupOffset, "");

            s_pData += sizeof(r5_mstudiobbox_t);
        }
    }

    R5_AlignData(s_pData);
}

static void R5_ConvertBoneByName(const studiohdr_t* pOld)
{
    int numBones = pOld->numbones;
    s_pHdr->bonetablebynameindex = (int)(s_pData - s_pBase);

    const byte* pOldTable = (const byte*)((const char*)pOld + pOld->bonetablebynameindex);
    memcpy(s_pData, pOldTable, numBones);
    s_pData += numBones;

    R5_AlignData(s_pData);
}

static void R5_WriteDefaultSequence()
{
    r5_mstudioseqdesc_t* pSeq = reinterpret_cast<r5_mstudioseqdesc_t*>(s_pData);
    memset(pSeq, 0, sizeof(r5_mstudioseqdesc_t));

    s_pHdr->localseqindex = (int)(s_pData - s_pBase);
    s_pHdr->numlocalseq   = 1;

    pSeq->baseptr   = 0;
    R5_AddToStringTable((char*)pSeq, &pSeq->szlabelindex, "@ref");
    R5_AddToStringTable((char*)pSeq, &pSeq->szactivitynameindex, "");

    pSeq->flags          = 0x80000;
    pSeq->activity       = -1;
    pSeq->bbmin          = s_pHdr->hull_min;
    pSeq->bbmax          = s_pHdr->hull_max;
    pSeq->groupsize[0]   = 1;
    pSeq->groupsize[1]   = 1;
    pSeq->paramindex[0]  = -1;
    pSeq->paramindex[1]  = -1;
    pSeq->fadeintime     = 0.2f;
    pSeq->fadeouttime    = 0.2f;

    // eventindex / autolayer / weightlist all point to right after seq (empty)
    pSeq->eventindex       = (int)sizeof(*pSeq);
    pSeq->autolayerindex   = (int)sizeof(*pSeq);
    pSeq->weightlistindex  = (int)sizeof(*pSeq);

    s_pData += sizeof(r5_mstudioseqdesc_t);

    // Weight list (one float per bone, all 1.0)
    for (int i = 0; i < s_pHdr->numbones; i++)
    {
        *reinterpret_cast<float*>(s_pData) = 1.0f;
        s_pData += sizeof(float);
    }

    pSeq->animindexindex = (int)(s_pData - (char*)pSeq);

    // Empty arrays (iklockindex, keyvalueindex, activitymodifierindex) point here too
    pSeq->iklockindex              = pSeq->animindexindex;
    pSeq->keyvalueindex            = pSeq->animindexindex;
    pSeq->activitymodifierindex    = pSeq->animindexindex;

    // Blend index (points to animdesc)
    int blendRef = pSeq->animindexindex + (int)sizeof(int);
    memcpy(s_pData, &blendRef, sizeof(int));
    s_pData += sizeof(int);

    // AnimDesc
    r5_mstudioanimdesc_t* pAnim = reinterpret_cast<r5_mstudioanimdesc_t*>(s_pData);
    memset(pAnim, 0, sizeof(r5_mstudioanimdesc_t));

    R5_AddToStringTable((char*)pAnim, &pAnim->sznameindex, "");
    pAnim->fps       = 30.0f;
    pAnim->flags     = 0x20000; // RMDL v54 ALLZEROS flag
    pAnim->numframes = 1;

    s_pData += sizeof(r5_mstudioanimdesc_t);

    // Record total block size in unused[0] (seqdesc start to end of seq data)
    pSeq->unused[0] = (int)(s_pData - (char*)pSeq);

    R5_AlignData(s_pData);
}

static void R5_ConvertBodyParts(const studiohdr_t* pOld)
{
    int numBP = pOld->numbodyparts;
    printf("  [RMDL] Converting %d bodyparts...\n", numBP);

    s_pHdr->bodypartindex = (int)(s_pData - s_pBase);

    r5_mstudiobodyparts_t* newBPs = reinterpret_cast<r5_mstudiobodyparts_t*>(s_pData);

    for (int i = 0; i < numBP; i++)
    {
        const mstudiobodyparts_t* pOldBP = pOld->pBodypart(i);
        r5_mstudiobodyparts_t*    pNewBP = newBPs + i;

        R5_AddToStringTable((char*)pNewBP, &pNewBP->sznameindex,
            RMDL_STRING_FROM_IDX(pOldBP, pOldBP->sznameindex));

        pNewBP->nummodels = pOldBP->nummodels;
        pNewBP->base      = pOldBP->base;

        s_pData += sizeof(r5_mstudiobodyparts_t);
    }

    // Write models and meshes per bodypart
    for (int i = 0; i < numBP; i++)
    {
        const mstudiobodyparts_t* pOldBP = pOld->pBodypart(i);
        r5_mstudiobodyparts_t*    pNewBP = newBPs + i;

        pNewBP->modelindex = (int)(s_pData - (char*)pNewBP);

        r5_mstudiomodel_t* newModels = reinterpret_cast<r5_mstudiomodel_t*>(s_pData);

        for (int j = 0; j < pOldBP->nummodels; j++)
        {
            const mstudiomodel_t* pOldMdl = pOldBP->pModel(j);
            r5_mstudiomodel_t*    pNewMdl = newModels + j;
            memset(pNewMdl, 0, sizeof(r5_mstudiomodel_t));

            memcpy(pNewMdl->name, pOldMdl->name, sizeof(pNewMdl->name));
            pNewMdl->type            = pOldMdl->type;
            pNewMdl->boundingradius  = pOldMdl->boundingradius;
            pNewMdl->nummeshes       = pOldMdl->nummeshes;
            pNewMdl->numvertices     = pOldMdl->numvertices;
            pNewMdl->vertexindex     = pOldMdl->vertexindex;
            pNewMdl->tangentsindex   = pOldMdl->tangentsindex;
            pNewMdl->numattachments  = pOldMdl->numattachments;
            pNewMdl->attachmentindex = pOldMdl->attachmentindex;

            s_pData += sizeof(r5_mstudiomodel_t);
        }

        // Write meshes per model
        for (int j = 0; j < pOldBP->nummodels; j++)
        {
            const mstudiomodel_t* pOldMdl = pOldBP->pModel(j);
            r5_mstudiomodel_t*    pNewMdl = newModels + j;

            pNewMdl->meshindex = (int)(s_pData - (char*)pNewMdl);

            for (int k = 0; k < pOldMdl->nummeshes; k++)
            {
                const mstudiomesh_t* pOldMesh = pOldMdl->pMesh(k);
                r5_mstudiomesh_t*    pNewMesh  = reinterpret_cast<r5_mstudiomesh_t*>(s_pData);

                memset(pNewMesh, 0, sizeof(r5_mstudiomesh_t));

                pNewMesh->material    = pOldMesh->material;
                pNewMesh->modelindex  = (int)((char*)pNewMdl - (char*)pNewMesh);
                pNewMesh->numvertices = pOldMesh->numvertices;
                pNewMesh->vertexoffset = pOldMesh->vertexoffset;
                pNewMesh->meshid      = pOldMesh->meshid;
                pNewMesh->center      = pOldMesh->center;
                memcpy(&pNewMesh->vertexloddata, &pOldMesh->vertexdata,
                    sizeof(r5_mstudio_meshvertexdata_t));

                s_pData += sizeof(r5_mstudiomesh_t);
            }
        }
    }

    R5_AlignData(s_pData);
}

static void R5_ConvertPoseParams(const studiohdr_t* pOld)
{
    int num = pOld->numlocalposeparameters;
    if (num <= 0) return;

    s_pHdr->localposeparamindex  = (int)(s_pData - s_pBase);
    s_pHdr->numlocalposeparameters = num;

    for (int i = 0; i < num; i++)
    {
        const mstudioposeparamdesc_t* pOldPP = pOld->pLocalPoseParameter(i);
        r5_mstudioposeparamdesc_t*    pNewPP = reinterpret_cast<r5_mstudioposeparamdesc_t*>(s_pData) + i;

        R5_AddToStringTable((char*)pNewPP, &pNewPP->sznameindex,
            RMDL_STRING_FROM_IDX(pOldPP, pOldPP->sznameindex));

        pNewPP->flags = pOldPP->flags;
        pNewPP->start = pOldPP->start;
        pNewPP->end   = pOldPP->end;
        pNewPP->loop  = pOldPP->loop;
    }

    s_pData += num * sizeof(r5_mstudioposeparamdesc_t);
    R5_AlignData(s_pData);
}

static void R5_ConvertIKChains(const studiohdr_t* pOld)
{
    // For non-rig models, write zero ik chains (rmdlconv skips them)
    s_pHdr->ikchainindex = (int)(s_pData - s_pBase);
    // s_pHdr->numikchains is already 0
}

static void R5_ConvertTextures(const studiohdr_t* pOld)
{
    int numTex = pOld->numtextures;

    // Collect and normalise all non-empty $cdmaterials paths (from the live QC globals).
    std::vector<std::string> cdPaths;
    for (int ci = 0; ci < numcdtextures; ci++)
    {
        const char* cd = cdtextures[ci];
        if (!cd || cd[0] == '\0') continue;
        std::string p = cd;
        for (char& c : p) if (c == '\\') c = '/';
        if (p.back() != '/') p += '/';
        cdPaths.push_back(std::move(p));
    }

    for (const auto& p : cdPaths)
        printf("  [RMDL] cdmaterials: '%s'\n", p.c_str());

    // Pick the best $cdmaterials prefix for bare-name textures by matching the
    // model's own directory against the tail of each cdmaterials path.
    //   g_outname  = "weapons\smr\w_smr.mdl"
    //   modelDir   = "weapons/smr/"
    //   best match = "models/weapons/smr/"  (its tail equals modelDir)
    // Falls back to cdPaths[0] when no suffix match is found.
    std::string bestCdPath = cdPaths.empty() ? "" : cdPaths[0];
    {
        // Build normalised, lower-case model directory from g_outname.
        std::string modelDir = g_outname;
        for (char& c : modelDir) if (c == '\\') c = '/';
        // Strip extension if present (e.g. ".mdl").
        {
            auto dot   = modelDir.rfind('.');
            auto slash = modelDir.rfind('/');
            if (dot != std::string::npos &&
                (slash == std::string::npos || dot > slash))
                modelDir = modelDir.substr(0, dot);
        }
        // Truncate to directory portion (keep trailing '/').
        {
            auto slash = modelDir.rfind('/');
            if (slash != std::string::npos)
                modelDir = modelDir.substr(0, slash + 1);
            else
                modelDir.clear(); // model sits in root; no prefix to match
        }
        std::string modelDirLow = modelDir;
        for (char& c : modelDirLow) c = (char)tolower((unsigned char)c);

        int bestScore = -1;
        for (const auto& cdp : cdPaths)
        {
            // Score = length of modelDir if it is a suffix of cdp (case-insensitive).
            int score = 0;
            if (!modelDirLow.empty() && cdp.size() >= modelDirLow.size())
            {
                std::string cdpLow = cdp;
                for (char& c : cdpLow) c = (char)tolower((unsigned char)c);
                if (cdpLow.compare(cdpLow.size() - modelDirLow.size(),
                                   modelDirLow.size(), modelDirLow) == 0)
                    score = (int)modelDirLow.size();
            }
            if (score > bestScore)
            {
                bestScore = score;
                bestCdPath = cdp;
            }
        }
        printf("  [RMDL] model dir '%s' -> best cdmaterials '%s'\n",
               modelDir.c_str(), bestCdPath.c_str());
    }

    s_pHdr->textureindex = (int)(s_pData - s_pBase);

    // Cache for -cdpick: maps bare texture name → user-chosen cdmaterials prefix.
    // Ensures identical texture names always get the same prefix, and we only ask once.
    std::map<std::string, std::string> cdPickCache;

    printf("  [RMDL] %d textures:\n", numTex);
    for (int i = 0; i < numTex; i++)
    {
        const mstudiotexture_t* pOldTex = pOld->pTexture(i);
        r5_mstudiotexture_t*    pNewTex = reinterpret_cast<r5_mstudiotexture_t*>(s_pData);
        memset(pNewTex, 0, sizeof(r5_mstudiotexture_t));

        const char* texName = RMDL_STRING_FROM_IDX(pOldTex, pOldTex->sznameindex);

        std::string fullName;
        if (strchr(texName, '/') || strchr(texName, '\\'))
        {
            // Already has a path — normalise slashes and use verbatim.
            fullName = texName;
            for (char& c : fullName) if (c == '\\') c = '/';
        }
        else if (!bestCdPath.empty() || !cdPaths.empty())
        {
            // Determine the cdmaterials prefix for this bare texture name.
            std::string chosenPath = bestCdPath; // auto-selected default

            if (s_bCdPick && cdPaths.size() > 1)
            {
                // Interactive picker: ask once per unique bare texture name.
                auto it = cdPickCache.find(texName);
                if (it != cdPickCache.end())
                {
                    chosenPath = it->second; // reuse earlier answer for this name
                }
                else
                {
                    printf("\n  [RMDL] -cdpick: pick cdmaterials for '%s'\n", texName);
                    for (int ci = 0; ci < (int)cdPaths.size(); ci++)
                        printf("         %d: %s%s\n", ci + 1,
                               cdPaths[ci].c_str(), texName);
                    printf("         [auto=%s] Enter number or press Enter to accept: ",
                           bestCdPath.c_str());
                    fflush(stdout);
                    char buf[16] = {};
                    if (fgets(buf, sizeof(buf), stdin))
                    {
                        int pick = atoi(buf);
                        if (pick >= 1 && pick <= (int)cdPaths.size())
                            chosenPath = cdPaths[pick - 1];
                    }
                    cdPickCache[texName] = chosenPath;
                    printf("         -> '%s%s'\n", chosenPath.c_str(), texName);
                }
            }

            fullName = chosenPath + texName;
        }
        else
        {
            // No $cdmaterials at all — use the bare name exactly as it appears
            // in the SMD (e.g. "smr", "smr_plate").
            fullName = texName;
        }

        R5_AddToStringTable((char*)pNewTex, &pNewTex->sznameindex, fullName.c_str());

        if (s_bZeroGuids)
        {
            pNewTex->textureGuid = 0;
            printf("    [%d] '%s' (zeroed)\n", i, fullName.c_str());
        }
        else
        {
            std::string matPath = std::string("material/") + fullName + ".rpak";
            pNewTex->textureGuid = R5_HashString(matPath.c_str());
            printf("    [%d] '%s' -> 0x%016llX\n", i, fullName.c_str(),
                (unsigned long long)pNewTex->textureGuid);
        }

        s_pData += sizeof(r5_mstudiotexture_t);
    }

    R5_AlignData(s_pData);

    // Material type bytes (one byte per texture)
    s_pHdr->materialtypesindex = (int)(s_pData - s_pBase);

    uint8_t matType = (s_pHdr->flags & R5_STUDIOHDR_FLAGS_STATIC_PROP) ?
        MATERIAL_TYPE_RGDP : MATERIAL_TYPE_SKNP;

    memset(s_pData, matType, numTex);
    s_pData += numTex;
    R5_AlignData(s_pData);

    // CDtexture (single empty entry)
    s_pHdr->cdtextureindex  = (int)(s_pData - s_pBase);
    s_pHdr->numcdtextures   = 1;

    R5_AddToStringTable(s_pBase, reinterpret_cast<int*>(s_pData), "");
    s_pData += sizeof(int);
}

static void R5_ConvertSkins(const studiohdr_t* pOld)
{
    int numRef    = pOld->numskinref;
    int numFam    = pOld->numskinfamilies;
    if (numRef <= 0 || numFam <= 0) return;

    printf("  [RMDL] Converting %d skins (%d refs)...\n", numFam, numRef);

    s_pHdr->skinindex      = (int)(s_pData - s_pBase);
    s_pHdr->numskinref     = numRef;
    s_pHdr->numskinfamilies = numFam;

    int skinSize = sizeof(short) * numRef * numFam;
    const char* pOldSkin = (const char*)pOld + pOld->skinindex;
    memcpy(s_pData, pOldSkin, skinSize);
    s_pData += skinSize;
    R5_AlignData(s_pData);

    // Skin family names (skin 0 is unnamed, additional skins get "skin1", "skin2", ...)
    for (int i = 0; i < numFam - 1; i++)
    {
        char skinName[32];
        sprintf(skinName, "skin%d", i + 1);
        R5_AddToStringTable(s_pBase, reinterpret_cast<int*>(s_pData), skinName);
        s_pData += sizeof(int);
    }

    R5_AlignData(s_pData);
}

static void R5_WriteKeyValues()
{
    const char* kv = "mdlkeyvalue{prop_data{base \"\"}}\n";
    int kvLen = (int)strlen(kv) + 1;
    int kvPadded = (kvLen + 1) & ~1; // align to 2

    s_pHdr->keyvalueindex = (int)(s_pData - s_pBase);
    s_pHdr->keyvaluesize  = kvPadded;

    memcpy(s_pData, kv, kvLen);
    s_pData += kvPadded;
    R5_AlignData(s_pData);
}

static void R5_ConvertSrcBoneTransforms(const studiohdr_t* pOld)
{
    const studiohdr2_t* pHdr2 = pOld->pStudioHdr2();
    int num = pHdr2->numsrcbonetransform;
    if (num <= 0) return;

    printf("  [RMDL] Converting %d src bone transforms...\n", num);

    s_pHdr->numsrcbonetransform    = num;
    s_pHdr->srcbonetransformindex = (int)(s_pData - s_pBase);

    const mstudiosrcbonetransform_t* pOldXF =
        reinterpret_cast<const mstudiosrcbonetransform_t*>(
            (const char*)pHdr2 + pHdr2->srcbonetransformindex);

    for (int i = 0; i < num; i++)
    {
        r5_mstudiosrcbonetransform_t* pNewXF =
            reinterpret_cast<r5_mstudiosrcbonetransform_t*>(s_pData) + i;

        R5_AddToStringTable((char*)pNewXF, &pNewXF->sznameindex,
            RMDL_STRING_FROM_IDX(&pOldXF[i], pOldXF[i].sznameindex));

        memcpy(&pNewXF->pretransform,  &pOldXF[i].pretransform,  sizeof(matrix3x4_t));
        memcpy(&pNewXF->posttransform, &pOldXF[i].posttransform, sizeof(matrix3x4_t));
    }

    s_pData += num * sizeof(r5_mstudiosrcbonetransform_t);
    R5_AlignData(s_pData);
}

static void R5_ConvertLinearBones(const studiohdr_t* pOld)
{
    const studiohdr2_t* pHdr2 = pOld->pStudioHdr2();
    if (!pHdr2->linearboneindex || pOld->numbones <= 1)
        return;

    const mstudiolinearbone_t* pOldLB = pHdr2->pLinearBones();
    if (!pOldLB) return;

    printf("  [RMDL] Converting linear bones...\n");

    s_pHdr->linearboneindex = (int)(s_pData - s_pBase);

    r5_mstudiolinearbone_t* pNewLB = reinterpret_cast<r5_mstudiolinearbone_t*>(s_pData);

    // r5 linear bone header is 28 bytes (7 int fields)
    // Old header is 64 bytes (10 ints + 6 unused ints)
    // The data array indices are relative to start of mstudiolinearbone_t
    // so we need to subtract the difference (64 - 28 = 36) from all indices
    int indexAdjust = (int)sizeof(mstudiolinearbone_t) - (int)sizeof(r5_mstudiolinearbone_t);

    pNewLB->numbones         = pOldLB->numbones;
    pNewLB->flagsindex       = pOldLB->flagsindex - indexAdjust;
    pNewLB->parentindex      = pOldLB->parentindex - indexAdjust;
    pNewLB->posindex         = pOldLB->posindex - indexAdjust;
    pNewLB->quatindex        = pOldLB->quatindex - indexAdjust;
    pNewLB->rotindex         = pOldLB->rotindex - indexAdjust;
    pNewLB->posetoboneindex  = pOldLB->posetoboneindex - indexAdjust;

    s_pData += sizeof(r5_mstudiolinearbone_t);

    // Copy the raw data arrays (flags, parents, pos, quat, rot, posetoBone, etc.)
    // The data starts right after the old header
    const char* pOldData = (const char*)pOldLB + sizeof(mstudiolinearbone_t);

    // Calculate size of data: bones * (flags=4, parent=4, pos=12, quat=16, rot=12,
    // poseToBone=48, posScale=12, rotScale=12, qAlignment=16) = bones * 136 bytes
    // But we only need up to posetoBone (minus posscale/rotscale/qalignment from data)
    // Actually, we copy ALL the data as-is (the data arrays are shared)
    // The new indices already point correctly into the data (relative to pNewLB)
    // We need to copy everything up to the end of the data referenced by the largest index
    int poseToBoneOffset = pOldLB->posetoboneindex;
    int numBones = pOldLB->numbones;
    int dataToCopy = poseToBoneOffset + numBones * (int)sizeof(matrix3x4_t);
    // Copy from old header start + adjustment
    memcpy(s_pData, pOldData, dataToCopy - indexAdjust);
    s_pData += dataToCopy - indexAdjust;

    R5_AlignData(s_pData);
}

//=============================================================================
// Main Conversion Entry Point
//=============================================================================

static char* R5_LoadFile(const char* path, size_t& outSize)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        printf("  [RMDL] Cannot open '%s'\n", path);
        outSize = 0;
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    outSize = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = new char[outSize];
    fread(buf, 1, outSize, f);
    fclose(f);
    return buf;
}

static std::string R5_ReplaceExt(const std::string& path, const char* newExt)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos)
        return path + newExt;
    return path.substr(0, dot) + newExt;
}

// Extract just the filename stem (no directory, no extension) from a full path.
//   "C:\game\weapons\p2011\p2011.mdl" -> "p2011"
static std::string R5_FileStem(const std::string& path)
{
    size_t slash = path.find_last_of("\\/");
    std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

// Recursively create all directories in a path.
static void R5_CreateDirsRecursive(const std::string& dirPath)
{
    for (size_t i = 0; i < dirPath.size(); ++i)
    {
        if (dirPath[i] == '\\' || dirPath[i] == '/')
        {
            std::string partial = dirPath.substr(0, i);
            if (!partial.empty())
                CreateDirectoryA(partial.c_str(), NULL);
        }
    }
    if (!dirPath.empty())
        CreateDirectoryA(dirPath.c_str(), NULL);
}

// Extract the path relative to gamedir, stripping an optional leading "models\" component.
//   mdlPath    = "C:\game\weapons\p2011\p2011.mdl"
//   gamedirStr = "C:\game\"  (trailing backslash)
//   -> "weapons\p2011\p2011.mdl"
// Used to locate the phy file that collisionmodel.cpp writes under gamedir\models\.
static std::string R5_RelativeToGamedir(const std::string& mdlPath,
                                         const std::string& gamedirStr)
{
    std::string rel = mdlPath;
    if (rel.size() > gamedirStr.size() &&
        _strnicmp(rel.c_str(), gamedirStr.c_str(), gamedirStr.size()) == 0)
    {
        rel = rel.substr(gamedirStr.size());
    }
    std::replace(rel.begin(), rel.end(), '/', '\\');
    // strip optional leading "models\" (collisionmodel.cpp always writes there)
    if (_strnicmp(rel.c_str(), "models\\", 7) == 0)
        rel = rel.substr(7);
    return rel;
}

//-----------------------------------------------------------------------------
// R5_RunAnimConv — convert the compiled MDL to .rrig/.rseq in-process.
// (Previously spawned R5-AnimConv.exe as a subprocess; now calls the
//  embedded R5-AnimConv source directly via animconv_wrapper.h.)
//-----------------------------------------------------------------------------
static void R5_RunAnimConv(const char* mdlFilePath)
{
    printf("[RMDL] Running in-process anim conversion on '%s'\n", mdlFilePath);
    R5_ConvertMDLAnimations(mdlFilePath,
                             gamedir,
                             s_overrideRrigPath.c_str(),
                             s_overrideRseqPath.c_str(),
                             /*verbose=*/false);
}

//-----------------------------------------------------------------------------
// R5_WriteRuiSection
// Writes the ruiheader[] + ruimesh blob into the RMDL buffer and fills
// s_pHdr->uiPanelCount / uiPanelOffset.
//
// Binary layout written per mesh
// ┌─────────────────────────────────────────────────────┐
// │ mstudioruimesh_t_v54   (28 bytes)                   │
// │ char name[]            (null-terminated)            │
// │ char padding[]         (to 16-byte-align vertices)  │
// │ int16 parent[numparents]                            │
// │ mstudioruivertmap_t[numfaces]    (6 bytes each)     │
// │ mstudioruifourthvertv54_t[nf]    (2 bytes each)     │
// │ mstudioruivert_t[numvertices]    (16 bytes each)    │
// │ mstudioruimeshface_t[numfaces]   (32 bytes each)    │
// └─────────────────────────────────────────────────────┘
//-----------------------------------------------------------------------------
static void R5_WriteRuiSection(const RuiFile& ruiFile, const studiohdr_t* pOldHdr)
{
    if (ruiFile.meshes.empty())
        return;

    // Build bone-name → index map from the source MDL.
    std::map<std::string, int> boneMap;
    for (int i = 0; i < pOldHdr->numbones; i++)
    {
        const mstudiobone_t* pBone = pOldHdr->pBone(i);
        if (pBone && pBone->pszName())
            boneMap[std::string(pBone->pszName())] = i;
    }

    const int numMeshes = (int)ruiFile.meshes.size();

    // Record where the ruiheader[] array starts in the output buffer.
    s_pHdr->uiPanelCount  = numMeshes;
    s_pHdr->uiPanelOffset = (int)(s_pData - s_pBase);

    // -----------------------------------------------------------------------
    // On-disk header entry layout (8 bytes):
    //   int32 namehash
    //   int32 ruimeshindex   <- offset from start of THIS entry to mesh data
    // -----------------------------------------------------------------------
#pragma pack(push, 1)
    struct RuiHdrEntry
    {
        int32_t namehash;
        int32_t ruimeshindex;
    };
#pragma pack(pop)

    // Allocate header entries (filled in after mesh blobs are written).
    RuiHdrEntry* headers = reinterpret_cast<RuiHdrEntry*>(s_pData);
    memset(headers, 0, numMeshes * sizeof(RuiHdrEntry));
    s_pData += numMeshes * sizeof(RuiHdrEntry);

    // -----------------------------------------------------------------------
    // Write each mesh blob.
    // -----------------------------------------------------------------------
    for (int mi = 0; mi < numMeshes; mi++)
    {
        const RuiMesh& mesh = ruiFile.meshes[mi];

        // Use the explicit hash if specified (set by decompiler for round-trip fidelity),
        // otherwise fall back to FNV-1a 32-bit on the full mesh name.
        if (mesh.namehash != -1)
        {
            headers[mi].namehash = mesh.namehash;
        }
        else
        {
            uint32_t hash = 2166136261u;
            for (const char* p = mesh.name.c_str(); *p; p++)
            {
                hash ^= (uint8_t)*p;
                hash *= 16777619u;
            }
            headers[mi].namehash = (int32_t)hash;
        }

        // Resolve parent bone names to indices.
        std::vector<int16_t> parentIndices;
        parentIndices.reserve(mesh.boneNames.size());
        for (const std::string& bn : mesh.boneNames)
        {
            auto it = boneMap.find(bn);
            if (it == boneMap.end())
            {
                printf("  [RUI] WARNING: bone '%s' not found for mesh '%s', defaulting to 0\n",
                       bn.c_str(), mesh.name.c_str());
                parentIndices.push_back(0);
            }
            else
            {
                parentIndices.push_back((int16_t)it->second);
            }
        }

        const int numParents  = (int)parentIndices.size();
        const int numVertices = (int)mesh.vertices.size();
        const int numFaces    = (int)mesh.faces.size();

        // Record where this mesh blob starts (for the header entry).
        const int meshStartAbs = (int)(s_pData - s_pBase);
        const int hdrEntryAbs  = s_pHdr->uiPanelOffset + mi * (int)sizeof(RuiHdrEntry);
        headers[mi].ruimeshindex = meshStartAbs - hdrEntryAbs;

        // -----------------------------------------------------------------------
        // Compute layout offsets (relative to mesh blob start).
        // Vertices must be 16-byte-aligned inside the output buffer.
        //   pre-vertex size = 28 (header) + nameLen + padding + 2*numParents
        //                   + 6*numFaces (vertmap) + 2*numFaces (fourthvert)
        // -----------------------------------------------------------------------
        const int nameLen = (int)mesh.name.size() + 1; // include NUL

        // Bytes from meshStart through end of fourthvert (excluding padding):
        const int preVertexFixed = 28 + nameLen + 2 * numParents + 8 * numFaces;
        const int padding = (16 - (meshStartAbs + preVertexFixed) % 16) % 16;

        const int parentindex   = 28 + nameLen + padding;
        const int vertmapindex  = parentindex  + 2 * numParents;
        const int unkindex      = vertmapindex + 6 * numFaces;   // fourthvert array
        const int vertexindex   = unkindex     + 2 * numFaces;
        const int facedataindex = vertexindex  + 16 * numVertices;

        // -----------------------------------------------------------------------
        // Write mstudioruimesh_t_v54 (28 bytes, explicit layout).
        // -----------------------------------------------------------------------
#pragma pack(push, 1)
        struct RuiMeshHdr
        {
            int16_t numparents;
            int16_t numvertices;
            int16_t numfaces;
            int16_t unk;
            int32_t parentindex;
            int32_t vertexindex;
            int32_t unkindex;
            int32_t vertmapindex;
            int32_t facedataindex;
        };
#pragma pack(pop)
        static_assert(sizeof(RuiMeshHdr) == 28, "RuiMeshHdr must be 28 bytes");

        RuiMeshHdr mhdr = {};
        mhdr.numparents   = (int16_t)numParents;
        mhdr.numvertices  = (int16_t)numVertices;
        mhdr.numfaces     = (int16_t)numFaces;
        mhdr.unk          = (mesh.unk >= 0) ? mesh.unk : (int16_t)numFaces;
        mhdr.parentindex  = parentindex;
        mhdr.vertexindex  = vertexindex;
        mhdr.unkindex     = unkindex;
        mhdr.vertmapindex = vertmapindex;
        mhdr.facedataindex = facedataindex;
        memcpy(s_pData, &mhdr, 28);
        s_pData += 28;

        // Name string (null-terminated).
        memcpy(s_pData, mesh.name.c_str(), nameLen);
        s_pData += nameLen;

        // Padding to align vertices to 16 bytes.
        memset(s_pData, 0, padding);
        s_pData += padding;

        // Parent bone index array (int16 each).
        for (int16_t idx : parentIndices)
        {
            memcpy(s_pData, &idx, 2);
            s_pData += 2;
        }

        // Vertex map: 3x int16 per face (mstudioruivertmap_t).
        for (const RuiFace& face : mesh.faces)
        {
            memcpy(s_pData, face.vertid, 6);
            s_pData += 6;
        }

        // Fourth-vert extra bytes: 2 bytes per face (mstudioruifourthvertv54_t).
        for (const RuiFace& face : mesh.faces)
        {
            s_pData[0] = (char)face.vertextra;
            s_pData[1] = (char)face.vertextra1;
            s_pData += 2;
        }

        // Vertices: int32 parent + 3 floats = 16 bytes each (mstudioruivert_t).
        for (const RuiVertex& vert : mesh.vertices)
        {
            auto it = boneMap.find(vert.boneName);
            int32_t boneIdx = (it != boneMap.end()) ? it->second : 0;
            memcpy(s_pData, &boneIdx, 4); s_pData += 4;
            memcpy(s_pData, &vert.x,    4); s_pData += 4;
            memcpy(s_pData, &vert.y,    4); s_pData += 4;
            memcpy(s_pData, &vert.z,    4); s_pData += 4;
        }

        // Face data: 8 floats = 32 bytes each (mstudioruimeshface_t).
        for (const RuiFace& face : mesh.faces)
        {
            memcpy(s_pData, &face.uvminx,    4); s_pData += 4;
            memcpy(s_pData, &face.uvminy,    4); s_pData += 4;
            memcpy(s_pData, &face.uvmaxx,    4); s_pData += 4;
            memcpy(s_pData, &face.uvmaxy,    4); s_pData += 4;
            memcpy(s_pData, &face.scaleminx, 4); s_pData += 4;
            memcpy(s_pData, &face.scaleminy, 4); s_pData += 4;
            memcpy(s_pData, &face.scalemaxx, 4); s_pData += 4;
            memcpy(s_pData, &face.scalemaxy, 4); s_pData += 4;
        }

        printf("  [RUI] mesh[%d] '%s'  parents=%d  verts=%d  faces=%d\n",
               mi, mesh.name.c_str(), numParents, numVertices, numFaces);
    }

    // 4-byte align after the whole RUI blob before the next section.
    R5_AlignData(s_pData);
}

void WriteRMDLFiles(const studiohdr_t* pInMemMDL, const char* mdlFilePath)
{
    s_bZeroGuids    = (CommandLine()->FindParm("-vmtext")      != 0);
    s_bConvertAnims = (CommandLine()->FindParm("-convertanims") != 0);
    s_bCdPick       = (CommandLine()->FindParm("-cdpick")       != 0);

    const char* rpVal = CommandLine()->ParmValue("-rp", (const char*)nullptr);
    s_overrideRrigPath = rpVal ? rpVal : "";

    const char* spVal = CommandLine()->ParmValue("-sp", (const char*)nullptr);
    s_overrideRseqPath = spVal ? spVal : "";

    if (s_bConvertAnims)
        R5_RunAnimConv(mdlFilePath);

    printf("\n[RMDL] Writing v10...\n");

    std::string mdlPath(mdlFilePath);
    std::string vvdPath  = R5_ReplaceExt(mdlPath, ".vvd");
    std::string vtxPath  = R5_ReplaceExt(mdlPath, ".dx90.vtx");

    // All output goes into gamedir\compiled\<modelname_path>\:
    //   gamedir\compiled\weapons\smr\w_smr.rmdl
    //   gamedir\compiled\weapons\smr\w_smr.vg
    //   gamedir\compiled\weapons\smr\w_smr.phy
    //   gamedir\compiled\animrig\...     (written by animconv, includes .rrig + .txt)
    //   gamedir\compiled\animseq\...     (written by animconv)
    std::string gamedirStr(gamedir);
    if (!gamedirStr.empty() && gamedirStr.back() != '\\' && gamedirStr.back() != '/')
        gamedirStr += '\\';

    // compiled output root
    const std::string compiledDir = gamedirStr + "compiled\\";

    // Relative path from $modelname (strip gamedir + "models\\" prefix, keep subdirs)
    const std::string relPath = R5_RelativeToGamedir(mdlPath, gamedirStr);
    const std::string relStem = R5_ReplaceExt(relPath, "");

    // Build output dir preserving $modelname subdirectory structure
    const std::string rmdlPath = compiledDir + relStem + ".rmdl";
    const std::string vgPath   = compiledDir + relStem + ".vg";

    // Create all directories in the output path
    {
        size_t lastSlash = rmdlPath.find_last_of("\\/");
        if (lastSlash != std::string::npos)
            R5_CreateDirsRecursive(rmdlPath.substr(0, lastSlash));
    }

    // Where collisionmodel.cpp wrote the phy (gamedir\models\<rel>\stem.phy)
    const std::string phySrc   = gamedirStr + "models\\" + relStem + ".phy";
    const std::string phyDst   = compiledDir + relStem + ".phy";

    // MDL is already in memory — no disk read needed.
    const studiohdr_t* pOldHdr = pInMemMDL;

    // VVD and VTX must be read from disk: they are finalized by studiomdl's
    // LOD-fixup pass which modifies them after writing.

    // Load VVD
    size_t vvdSize = 0;
    char*  vvdBuf  = R5_LoadFile(vvdPath.c_str(), vvdSize);
    if (!vvdBuf)
    {
        printf("ERROR: Could not load VVD '%s'\n", vvdPath.c_str());
        return;
    }

    // Load VTX
    size_t vtxSize = 0;
    char*  vtxBuf  = R5_LoadFile(vtxPath.c_str(), vtxSize);
    if (!vtxBuf)
    {
        printf("ERROR: Could not load VTX '%s'\n", vtxPath.c_str());
        delete[] vvdBuf;
        return;
    }
    const r5_vertexFileHeader_t* pVVD = reinterpret_cast<const r5_vertexFileHeader_t*>(vvdBuf);
    const OptimizedModel::FileHeader_t* pVTX =
        reinterpret_cast<const OptimizedModel::FileHeader_t*>(vtxBuf);

    // Allocate output buffer
    s_pBase = new char[RMDL_FILEBUF_SIZE]();
    s_pData = s_pBase;
    s_pHdr  = nullptr;

    R5_BeginStringTable();

    //-----------------------------------------------------------------------
    // Write RMDL header
    //-----------------------------------------------------------------------
    s_pHdr = reinterpret_cast<r5_studiohdr_t*>(s_pData);
    memset(s_pHdr, 0, sizeof(r5_studiohdr_t));

    s_pHdr->id       = R5_IDSTUDIOHEADER;
    s_pHdr->version  = R5_STUDIO_VERSION;
    s_pHdr->checksum = pOldHdr->checksum;
    s_pHdr->length   = 0xBADF00D; // filled later

    s_pHdr->eyeposition  = pOldHdr->eyeposition;
    s_pHdr->illumposition = pOldHdr->illumposition;
    s_pHdr->hull_min     = pOldHdr->hull_min;
    s_pHdr->hull_max     = pOldHdr->hull_max;
    s_pHdr->mins         = pOldHdr->hull_min;
    s_pHdr->maxs         = pOldHdr->hull_max;
    s_pHdr->view_bbmin   = pOldHdr->view_bbmin;
    s_pHdr->view_bbmax   = pOldHdr->view_bbmax;

    s_pHdr->flags          = pOldHdr->flags;
    s_pHdr->numbones       = pOldHdr->numbones;
    s_pHdr->numhitboxsets  = pOldHdr->numhitboxsets;
    s_pHdr->numlocalseq    = 0; // set later
    s_pHdr->numlocalanim   = 0; // unused
    s_pHdr->activitylistversion = pOldHdr->activitylistversion;
    s_pHdr->numtextures    = pOldHdr->numtextures;
    s_pHdr->numskinref     = pOldHdr->numskinref;
    s_pHdr->numskinfamilies = pOldHdr->numskinfamilies;
    s_pHdr->numbodyparts   = pOldHdr->numbodyparts;
    s_pHdr->numlocalattachments = pOldHdr->numlocalattachments;
    s_pHdr->keyvaluesize   = pOldHdr->keyvaluesize;
    s_pHdr->numincludemodels = -1;
    s_pHdr->numsrcbonetransform = pOldHdr->pStudioHdr2()->numsrcbonetransform;
    s_pHdr->mass           = pOldHdr->mass;
    s_pHdr->contents       = pOldHdr->contents;
    s_pHdr->constdirectionallightdot = pOldHdr->constdirectionallightdot;
    s_pHdr->rootLOD        = pOldHdr->rootLOD;
    s_pHdr->numAllowedRootLODs = pOldHdr->numAllowedRootLODs;
    s_pHdr->flVertAnimFixedPointScale = pOldHdr->flVertAnimFixedPointScale;
    s_pHdr->phyOffset = -123456;  // sentinel for no embedded .phy (matches rmdlconv and working game models)
    s_pHdr->phySize   = 0;

    s_pData += sizeof(r5_studiohdr_t);

    // Model name (add "mdl/" prefix, change extension to .rmdl)
    std::string oldName = pOldHdr->pszName() ? pOldHdr->pszName() : pOldHdr->name;
    std::string modelName = oldName;
    if (modelName.find("mdl/") != 0)
        modelName = "mdl/" + modelName;
    if (modelName.size() > 4 && modelName.compare(modelName.size()-4, 4, ".mdl") == 0)
        modelName = modelName.substr(0, modelName.size()-4) + ".rmdl";

    memset(s_pHdr->name, 0, 64);
    strncpy(s_pHdr->name, modelName.c_str(), 63);

    R5_AddToStringTable((char*)s_pHdr, &s_pHdr->sznameindex, modelName.c_str());

    // surface prop
    const char* surfProp = "";
    if (pOldHdr->surfacepropindex)
        surfProp = (const char*)pOldHdr + pOldHdr->surfacepropindex;
    R5_AddToStringTable((char*)s_pHdr, &s_pHdr->surfacepropindex, surfProp);

    // unk string (usually "" or "Titan")
    R5_AddToStringTable((char*)s_pHdr, &s_pHdr->unkStringOffset, "");

    //-----------------------------------------------------------------------
    // Convert data sections
    //-----------------------------------------------------------------------
    R5_ConvertBones(pOldHdr);
    R5_ConvertAttachments(pOldHdr);
    R5_ConvertHitboxes(pOldHdr);
    R5_ConvertBoneByName(pOldHdr);
    R5_WriteDefaultSequence();
    R5_ConvertBodyParts(pOldHdr);
    R5_ConvertPoseParams(pOldHdr);
    R5_ConvertIKChains(pOldHdr);

    // RUI mesh section (optional — only written when $ruimeshfile is set).
    if (!s_ruiMeshFilePath.empty())
    {
        RuiFile ruiFile;
        if (ParseRuiFile(s_ruiMeshFilePath.c_str(), ruiFile))
        {
            printf("  [RUI] Loaded %d mesh(es) from '%s'\n",
                   (int)ruiFile.meshes.size(), s_ruiMeshFilePath.c_str());
            R5_WriteRuiSection(ruiFile, pOldHdr);
        }
        else
        {
            printf("  [RUI] WARNING: failed to parse '%s', skipping RUI section\n",
                   s_ruiMeshFilePath.c_str());
        }
    }

    R5_ConvertTextures(pOldHdr);
    R5_ConvertSkins(pOldHdr);
    R5_WriteKeyValues();
    R5_ConvertSrcBoneTransforms(pOldHdr);
    R5_ConvertLinearBones(pOldHdr);

    // Write string table
    s_pData = R5_WriteStringTable(s_pData);
    R5_AlignData(s_pData);

    //-----------------------------------------------------------------------
    // Build and append BVH4 collision data
    //-----------------------------------------------------------------------
    if (pOldHdr->numbodyparts > 0)
    {
        std::vector<uint8_t> bvhBlob = R5_BuildBVHCollision(pOldHdr, pVTX, pVVD);
        if (!bvhBlob.empty())
        {
            // Align to 64 bytes (collision data uses aligned SIMD loads)
            R5_AlignData(s_pData, 64);

            int bvhOffset = (int)(s_pData - s_pBase);
            memcpy(s_pData, bvhBlob.data(), bvhBlob.size());
            s_pData += bvhBlob.size();

            s_pHdr->bvhOffset = bvhOffset;
            printf("  [BVH] Written at offset 0x%X (%u bytes)\n", bvhOffset, (unsigned)bvhBlob.size());
        }
    }

    s_pHdr->length = (int)(s_pData - s_pBase);

    //-----------------------------------------------------------------------
    // Write .rmdl file
    //-----------------------------------------------------------------------
    FILE* rmdlOut = fopen(rmdlPath.c_str(), "wb");
    if (rmdlOut)
    {
        fwrite(s_pBase, 1, s_pHdr->length, rmdlOut);
        fclose(rmdlOut);
        printf("  [RMDL] Written: %s (%d bytes)\n", rmdlPath.c_str(), s_pHdr->length);
    }
    else
    {
        printf("ERROR: Could not write RMDL '%s'\n", rmdlPath.c_str());
    }

    //-----------------------------------------------------------------------
    // Write .rson file (RMDL → RRIG linkage)
    // Only generated when -convertanims produced a .rrig.
    // Format: LF line endings, TAB-indented paths with backslashes.
    //-----------------------------------------------------------------------
    if (s_bConvertAnims)
    {
        std::string rsonPath    = R5_ReplaceExt(rmdlPath, ".rson");

        // Build RRIG relative path matching animconv logic:
        // animconv replaces a leading "mdl" prefix with "animrig" in
        // the MDL header name.  relStem may start with "mdl\" when
        // $modelname includes the mdl/ prefix, so mirror that transform.
        std::string rrigRelPath = relStem;
        if (rrigRelPath.size() >= 4 &&
            (_strnicmp(rrigRelPath.c_str(), "mdl\\", 4) == 0 ||
             _strnicmp(rrigRelPath.c_str(), "mdl/", 4) == 0))
        {
            rrigRelPath = "animrig" + rrigRelPath.substr(3);
        }
        else
        {
            rrigRelPath = "animrig\\" + rrigRelPath;
        }
        rrigRelPath += ".rrig";

        FILE* rsonOut = fopen(rsonPath.c_str(), "wb");
        if (rsonOut)
        {
            fprintf(rsonOut, "rigs:\n[\n\t%s\n]\nseqs:\n[\n]\n",
                    rrigRelPath.c_str());
            fclose(rsonOut);
            printf("  [RMDL] Written .rson: %s\n", rsonPath.c_str());
        }
        else
        {
            printf("  [RMDL] WARNING: could not write .rson '%s'\n",
                   rsonPath.c_str());
        }
    }

    //-----------------------------------------------------------------------
    // Build and write .vg file
    //-----------------------------------------------------------------------
    if (pOldHdr->numbodyparts > 0)
    {
        r5_VGBuilder vgBuilder{};
        memset(&vgBuilder.hdr, 0, sizeof(r5_vg_FileHeader_t));
        vgBuilder.currentVertBufferSize = 0;
        vgBuilder.defaultFlags = 0;

        bool ok = R5_BuildVGData(vgBuilder, s_pHdr, pVTX, pVVD);
        if (ok)
        {
            R5_WriteVGFile(vgPath.c_str(), vgBuilder);
        }
    }
    else
    {
        printf("  [VG] No body parts, skipping VG generation.\n");
    }

    //-----------------------------------------------------------------------
    // Cleanup in-memory buffers
    //-----------------------------------------------------------------------
    const int savedChecksum = s_pHdr ? s_pHdr->checksum : pOldHdr->checksum;
    delete[] s_pBase;
    delete[] vvdBuf;
    delete[] vtxBuf;

    s_pBase = nullptr;
    s_pData = nullptr;
    s_pHdr  = nullptr;
    s_stringTable.clear();

    //-----------------------------------------------------------------------
    // Convert studiomdl .phy → Apex relocatable-blob PHY format.
    //
    // Source Engine produces IVP compact-surface collision data which is
    // incompatible with Apex's native physics geometry format.  We
    // deserialise each CPhysCollide, extract polyhedron data via
    // IPhysicsCollision, and repack as an Apex relocatable blob (id >= 1).
    //
    // Apex .phy file layout:
    //   [20-byte IVPS header]  { size=20, id>=1, solidCount, checkSum, kvOffset }
    //   [relocatable blob]     self-relative pointers, fixed up by loader
    //   [key-value text]       Source-style physics properties
    //
    // Blob layout (all offsets relative to blob start):
    //   BlobHeader (32 B)  →  Solid[] (144 B each)  →  Convex[] (64 B each)
    //   →  vertex data (float[3] per vert)
    //   →  face data   (32 B per face, byte vertex indices, 0xFF pad)
    //   →  edge data   (4 B per edge: u8 v0, u8 v1, u8 faceA, u8 faceB)
    //-----------------------------------------------------------------------
    {
        extern IPhysicsCollision *physcollision;

        DWORD attr = GetFileAttributesA(phySrc.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            printf("  [PHY] Found source: %s\n", phySrc.c_str());
            FILE* phyIn = fopen(phySrc.c_str(), "rb");
            if (phyIn)
            {
                fseek(phyIn, 0, SEEK_END);
                long phyFileSize = ftell(phyIn);
                fseek(phyIn, 0, SEEK_SET);
                char* phyBuf = new char[phyFileSize];
                fread(phyBuf, 1, phyFileSize, phyIn);
                fclose(phyIn);
                remove(phySrc.c_str());

                // --- Parse Source phyheader_t (16-byte) ---
                int srcHdrSize    = *reinterpret_cast<int*>(phyBuf + 0);
                int srcSolidCount = *reinterpret_cast<int*>(phyBuf + 8);

                // Walk solids to find key-value text boundary.
                long cursor = srcHdrSize;
                for (int i = 0; i < srcSolidCount; i++)
                {
                    if (cursor + 4 > phyFileSize) break;
                    int solidSize = *reinterpret_cast<int*>(phyBuf + cursor);
                    cursor += 4 + solidSize;
                }
                long kvStart = cursor;
                long kvSize  = phyFileSize - kvStart;
                if (kvSize < 0) kvSize = 0;

                // ---------------------------------------------------------------
                // Deserialise each solid's collision data into CPhysCollide
                // objects, extract convex polyhedron geometry, and build the
                // Apex relocatable blob.
                // ---------------------------------------------------------------

                // --- Collect per-solid / per-convex / raw geometry data ---
                struct ConvexData {
                    std::vector<Vector>                       verts;
                    std::vector<std::vector<uint8_t>>         faces;     // per face: vertex indices
                    struct Edge { uint8_t v[4]; };             // {v0,v1,faceA,faceB}
                    std::vector<Edge>                         edges;
                    float center[3];
                    float inradius;
                };
                struct SolidData {
                    std::vector<ConvexData> convexes;
                    Vector aabbMins, aabbMaxs;
                    Vector massCenter;
                    float  volume;
                };
                std::vector<SolidData> solids;

                long solidCursor = srcHdrSize;
                for (int si = 0; si < srcSolidCount; si++)
                {
                    if (solidCursor + 4 > phyFileSize) break;
                    int solidSize = *reinterpret_cast<int*>(phyBuf + solidCursor);
                    char* solidBuf = phyBuf + solidCursor + 4;
                    solidCursor += 4 + solidSize;

                    CPhysCollide* pCollide = physcollision->UnserializeCollide(
                        solidBuf, solidSize, si);
                    if (!pCollide) {
                        printf("  [PHY] WARNING: UnserializeCollide failed for solid %d\n", si);
                        continue;
                    }

                    SolidData sd;
                    sd.volume = physcollision->CollideVolume(pCollide);
                    physcollision->CollideGetMassCenter(pCollide, &sd.massCenter);
                    physcollision->CollideGetAABB(&sd.aabbMins, &sd.aabbMaxs,
                        pCollide, vec3_origin, vec3_angle);

                    // Extract convex pieces.
                    const int kMaxConvexes = 256;
                    CPhysConvex* convexArray[kMaxConvexes];
                    int numConvex = physcollision->GetConvexesUsedInCollideable(
                        pCollide, convexArray, kMaxConvexes);

                    for (int ci = 0; ci < numConvex; ci++)
                    {
                        CPolyhedron* poly = physcollision->PolyhedronFromConvex(
                            convexArray[ci], false);
                        if (!poly || poly->iVertexCount == 0) {
                            printf("  [PHY] WARNING: PolyhedronFromConvex returned "
                                   "null/empty for solid %d convex %d\n", si, ci);
                            if (poly) poly->Release();
                            continue;
                        }

                        ConvexData cd;

                        // --- Vertices ---
                        for (int vi = 0; vi < poly->iVertexCount; vi++)
                            cd.verts.push_back(poly->pVertices[vi]);

                        // --- Faces (polygon vertex indices from line refs) ---
                        for (int fi = 0; fi < poly->iPolygonCount; fi++)
                        {
                            const Polyhedron_IndexedPolygon_t& pg = poly->pPolygons[fi];
                            std::vector<uint8_t> faceVerts;
                            for (int ri = 0; ri < pg.iIndexCount; ri++)
                            {
                                const Polyhedron_IndexedLineReference_t& ref =
                                    poly->pIndices[pg.iFirstIndex + ri];
                                const Polyhedron_IndexedLine_t& line =
                                    poly->pLines[ref.iLineIndex];
                                faceVerts.push_back(
                                    (uint8_t)line.iPointIndices[ref.iEndPointIndex]);
                            }
                            cd.faces.push_back(std::move(faceVerts));
                        }

                        // --- Edges → {v0, v1, faceA, faceB} ---
                        // Build edge-to-face map: each line is shared by 2 faces.
                        std::vector<uint8_t> lineFaceA(poly->iLineCount, 0xFF);
                        std::vector<uint8_t> lineFaceB(poly->iLineCount, 0xFF);
                        for (int fi = 0; fi < poly->iPolygonCount; fi++)
                        {
                            const Polyhedron_IndexedPolygon_t& pg = poly->pPolygons[fi];
                            for (int ri = 0; ri < pg.iIndexCount; ri++)
                            {
                                unsigned short li =
                                    poly->pIndices[pg.iFirstIndex + ri].iLineIndex;
                                if (lineFaceA[li] == 0xFF)
                                    lineFaceA[li] = (uint8_t)fi;
                                else
                                    lineFaceB[li] = (uint8_t)fi;
                            }
                        }
                        for (int li = 0; li < poly->iLineCount; li++)
                        {
                            ConvexData::Edge e;
                            e.v[0] = (uint8_t)poly->pLines[li].iPointIndices[0];
                            e.v[1] = (uint8_t)poly->pLines[li].iPointIndices[1];
                            e.v[2] = lineFaceA[li];
                            e.v[3] = lineFaceB[li];
                            cd.edges.push_back(e);
                        }

                        // --- Convex center & inradius ---
                        Vector cvxMins = cd.verts[0], cvxMaxs = cd.verts[0];
                        for (size_t vi = 1; vi < cd.verts.size(); vi++) {
                            VectorMin(cd.verts[vi], cvxMins, cvxMins);
                            VectorMax(cd.verts[vi], cvxMaxs, cvxMaxs);
                        }
                        cd.center[0] = (cvxMins.x + cvxMaxs.x) * 0.5f;
                        cd.center[1] = (cvxMins.y + cvxMaxs.y) * 0.5f;
                        cd.center[2] = (cvxMins.z + cvxMaxs.z) * 0.5f;
                        float hx = (cvxMaxs.x - cvxMins.x) * 0.5f;
                        float hy = (cvxMaxs.y - cvxMins.y) * 0.5f;
                        float hz = (cvxMaxs.z - cvxMins.z) * 0.5f;
                        cd.inradius = hx;
                        if (hy < cd.inradius) cd.inradius = hy;
                        if (hz < cd.inradius) cd.inradius = hz;

                        poly->Release();
                        sd.convexes.push_back(std::move(cd));
                    }

                    physcollision->DestroyCollide(pCollide);
                    solids.push_back(std::move(sd));
                }

                // ---------------------------------------------------------------
                // Build the relocatable blob.
                // Layout: BlobHeader → Solid[] → Convex[] → vertex/face/edge data
                // ---------------------------------------------------------------
                const int BLOB_HDR  = 32;
                const int SOLID_SZ  = 144;
                const int CONVEX_SZ = 64;

                int totalConvex = 0;
                for (auto& s : solids)
                    totalConvex += (int)s.convexes.size();

                // Phase 1: calculate offsets for each data block.
                int solidArrayOff  = BLOB_HDR;
                int convexArrayOff = solidArrayOff + SOLID_SZ * (int)solids.size();
                int dataOff        = convexArrayOff + CONVEX_SZ * totalConvex;

                // Per-convex data offsets.
                struct ConvexOffsets { int verts, faces, edges; };
                std::vector<ConvexOffsets> cvxOffsets;
                for (auto& s : solids) {
                    for (auto& c : s.convexes) {
                        ConvexOffsets co;
                        co.verts = dataOff;
                        dataOff += (int)c.verts.size() * 12; // 3 floats
                        co.faces = dataOff;
                        dataOff += (int)c.faces.size() * 32; // 32 B per face
                        co.edges = dataOff;
                        dataOff += (int)c.edges.size() * 4;  // 4 B per edge
                        cvxOffsets.push_back(co);
                    }
                }
                int totalBlobSize = dataOff;

                // Phase 2: allocate and fill the blob.
                std::vector<uint8_t> blob(totalBlobSize, 0);

                // -- Blob header (32 bytes) --
                *reinterpret_cast<int64_t*>(&blob[0])  = solidArrayOff; // relptr
                *reinterpret_cast<int32_t*>(&blob[8])  = (int)solids.size();
                *reinterpret_cast<int32_t*>(&blob[24]) = totalBlobSize;

                // -- Solid entries --
                int cvxIdx = 0;
                int curConvexOff = convexArrayOff;
                for (int si = 0; si < (int)solids.size(); si++)
                {
                    uint8_t* sp = &blob[solidArrayOff + si * SOLID_SZ];
                    SolidData& sd = solids[si];

                    // +0: relptr to convex array
                    *reinterpret_cast<int64_t*>(sp + 0) = curConvexOff;
                    // +8: numConvexes
                    *reinterpret_cast<int32_t*>(sp + 8) = (int)sd.convexes.size();
                    // +16: scale = 1.0f
                    *reinterpret_cast<float*>(sp + 16)  = 1.0f;
                    // +32: volume center (geometric center of AABB)
                    float vc[3] = {
                        (sd.aabbMins.x + sd.aabbMaxs.x) * 0.5f,
                        (sd.aabbMins.y + sd.aabbMaxs.y) * 0.5f,
                        (sd.aabbMins.z + sd.aabbMaxs.z) * 0.5f
                    };
                    memcpy(sp + 32, vc, 12);
                    // +48: approximate inertia tensor (diagonal, box approx)
                    float dx = sd.aabbMaxs.x - sd.aabbMins.x;
                    float dy = sd.aabbMaxs.y - sd.aabbMins.y;
                    float dz = sd.aabbMaxs.z - sd.aabbMins.z;
                    float vf = (sd.volume > 0) ? sd.volume : (dx * dy * dz);
                    float c12 = vf / 12.0f;
                    float Ixx = c12 * (dy*dy + dz*dz);
                    float Iyy = c12 * (dx*dx + dz*dz);
                    float Izz = c12 * (dx*dx + dy*dy);
                    *reinterpret_cast<float*>(sp + 48) = Ixx;
                    *reinterpret_cast<float*>(sp + 64) = Iyy;
                    *reinterpret_cast<float*>(sp + 80) = Izz;
                    // off-diagonal = 0 (already zeroed)

                    // +96: center of mass
                    *reinterpret_cast<float*>(sp + 96)  = sd.massCenter.x;
                    *reinterpret_cast<float*>(sp + 100) = sd.massCenter.y;
                    *reinterpret_cast<float*>(sp + 104) = sd.massCenter.z;
                    // +108: reserved (0)
                    // +112: AABB mins
                    *reinterpret_cast<float*>(sp + 112) = sd.aabbMins.x;
                    *reinterpret_cast<float*>(sp + 116) = sd.aabbMins.y;
                    *reinterpret_cast<float*>(sp + 120) = sd.aabbMins.z;
                    // +124: AABB maxs
                    *reinterpret_cast<float*>(sp + 124) = sd.aabbMaxs.x;
                    *reinterpret_cast<float*>(sp + 128) = sd.aabbMaxs.y;
                    *reinterpret_cast<float*>(sp + 132) = sd.aabbMaxs.z;

                    curConvexOff += CONVEX_SZ * (int)sd.convexes.size();

                    // -- Convex entries --
                    for (int ci = 0; ci < (int)sd.convexes.size(); ci++, cvxIdx++)
                    {
                        int cOff = convexArrayOff + cvxIdx * CONVEX_SZ;
                        uint8_t* cp = &blob[cOff];
                        ConvexData& cd = sd.convexes[ci];
                        ConvexOffsets& co = cvxOffsets[cvxIdx];

                        // +0..15: convex center + inradius
                        *reinterpret_cast<float*>(cp + 0)  = cd.center[0];
                        *reinterpret_cast<float*>(cp + 4)  = cd.center[1];
                        *reinterpret_cast<float*>(cp + 8)  = cd.center[2];
                        *reinterpret_cast<float*>(cp + 12) = cd.inradius;
                        // +16: relptr to vertex list
                        *reinterpret_cast<int64_t*>(cp + 16) = co.verts;
                        *reinterpret_cast<int32_t*>(cp + 24) = (int)cd.verts.size();
                        // +32: relptr to face-vert list
                        *reinterpret_cast<int64_t*>(cp + 32) = co.faces;
                        *reinterpret_cast<int32_t*>(cp + 40) = (int)cd.faces.size();
                        // +48: relptr to edge list
                        *reinterpret_cast<int64_t*>(cp + 48) = co.edges;
                        *reinterpret_cast<int32_t*>(cp + 56) = (int)cd.edges.size();

                        // --- Write vertex data ---
                        for (int vi = 0; vi < (int)cd.verts.size(); vi++)
                        {
                            float* dst = reinterpret_cast<float*>(&blob[co.verts + vi * 12]);
                            dst[0] = cd.verts[vi].x;
                            dst[1] = cd.verts[vi].y;
                            dst[2] = cd.verts[vi].z;
                        }

                        // --- Write face data (32 bytes each, 0xFF pad) ---
                        for (int fi = 0; fi < (int)cd.faces.size(); fi++)
                        {
                            uint8_t* fp = &blob[co.faces + fi * 32];
                            memset(fp, 0xFF, 32);
                            int n = (int)cd.faces[fi].size();
                            if (n > 32) n = 32;
                            for (int k = 0; k < n; k++)
                                fp[k] = cd.faces[fi][k];
                        }

                        // --- Write edge data (4 bytes each) ---
                        for (int ei = 0; ei < (int)cd.edges.size(); ei++)
                        {
                            uint8_t* ep = &blob[co.edges + ei * 4];
                            ep[0] = cd.edges[ei].v[0];
                            ep[1] = cd.edges[ei].v[1];
                            ep[2] = cd.edges[ei].v[2];
                            ep[3] = cd.edges[ei].v[3];
                        }
                    }
                }

                // ---------------------------------------------------------------
                // Write the Apex .phy file:  IVPS header + blob + keyvalues
                // ---------------------------------------------------------------
                struct IVPSHeader {
                    int size;            // 20
                    int id;              // >= 1 → relocatable blob path
                    int solidCount;
                    int checkSum;
                    int keyValuesOffset; // from file start
                };
                IVPSHeader ivpsHdr;
                ivpsHdr.size            = 20;
                ivpsHdr.id              = 1;
                ivpsHdr.solidCount      = (int)solids.size();
                ivpsHdr.checkSum        = savedChecksum;
                ivpsHdr.keyValuesOffset = 20 + totalBlobSize;

                FILE* phyOut = fopen(phyDst.c_str(), "wb");
                if (phyOut)
                {
                    fwrite(&ivpsHdr, sizeof(IVPSHeader), 1, phyOut);
                    fwrite(blob.data(), 1, totalBlobSize, phyOut);
                    if (kvSize > 0)
                        fwrite(phyBuf + kvStart, 1, kvSize, phyOut);
                    fclose(phyOut);

                    // Patch the already-written RMDL header on disk:
                    // set phySize so the mod loader knows to load the .phy file.
                    // Note: do NOT set STUDIOHDR_FLAGS_HAS_PHYSICS_DATA (0x40000) —
                    // that flag is for pak-embedded physics only. For separate .phy
                    // files, phySize > 0 is sufficient (verified against reference
                    // models like slumcity_fencewall which have phySize set but no
                    // 0x40000 flag).
                    {
                        int phyFileSize = (int)(sizeof(IVPSHeader) + totalBlobSize + kvSize);
                        FILE* rmdlPatch = fopen(rmdlPath.c_str(), "r+b");
                        if (rmdlPatch)
                        {
                            fseek(rmdlPatch, offsetof(r5_studiohdr_t, phySize), SEEK_SET);
                            fwrite(&phyFileSize, sizeof(int), 1, rmdlPatch);
                            fclose(rmdlPatch);
                            printf("  [PHY] Patched RMDL: phySize=%d\n", phyFileSize);
                        }
                    }

                    printf("  [PHY] Written Apex relocatable blob: %s "
                           "(%d solid(s), %d convex(es), %d bytes)\n",
                           phyDst.c_str(), (int)solids.size(), totalConvex,
                           totalBlobSize);
                }
                else
                {
                    printf("  [PHY] WARNING: could not write '%s' (error %lu)\n",
                           phyDst.c_str(), GetLastError());
                }

                delete[] phyBuf;
            }
            else
            {
                printf("  [PHY] WARNING: could not read '%s'\n", phySrc.c_str());
            }
        }
        else
        {
            printf("  [PHY] No source .phy found at '%s' (model has no "
                   "$collisionmodel?)\n", phySrc.c_str());
        }
    }

    //-----------------------------------------------------------------------
    // Remove all intermediate files studiomdl writes (MDL, VVD, all VTX
    // variants, ANI) — RMDL + VG are the only outputs we keep.
    //-----------------------------------------------------------------------
    {
        // Build stem (path without any extension) to derive all variants.
        std::string stem_ = mdlPath;
        size_t dot_ = stem_.rfind('.');
        if (dot_ != std::string::npos) stem_ = stem_.substr(0, dot_);

        static const char* kTmpExts[] = {
            ".mdl", ".vvd", ".ani",
            ".sw.vtx", ".dx80.vtx", ".dx90.vtx",
            nullptr
        };
        for (int ei = 0; kTmpExts[ei]; ei++)
            remove((stem_ + kTmpExts[ei]).c_str());
    }

    //-----------------------------------------------------------------------
    // Remove the now-empty gamedir\models\ directory tree
    // (must happen AFTER the MDL is deleted above)
    //-----------------------------------------------------------------------
    {
        std::string modelsRoot = gamedirStr + "models";  // no trailing slash
        std::string cleanDir;
        {
            size_t slash = phySrc.find_last_of("\\/");
            cleanDir = (slash != std::string::npos) ? phySrc.substr(0, slash) : "";
        }
        while (!cleanDir.empty() &&
               cleanDir.size() >= modelsRoot.size() &&
               _strnicmp(cleanDir.c_str(), gamedirStr.c_str(), gamedirStr.size()) == 0)
        {
            if (!RemoveDirectoryA(cleanDir.c_str()))
                break;  // directory not empty or already gone — stop
            size_t slash = cleanDir.find_last_of("\\/");
            if (slash == std::string::npos) break;
            cleanDir = cleanDir.substr(0, slash);
        }
    }

    printf("[RMDL] Write complete.\n\n");
}
