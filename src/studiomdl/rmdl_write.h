// rmdl_write.h
// RMDL v54 (Apex Legends v10) output from studiomdl
// Based on rmdlconv by rexx (GPL v3)

#pragma once

#include "studio.h"

// Write RMDL + VG files directly from the in-memory MDL header, bypassing
// the disk-read roundtrip. VVD and VTX files (written by studiomdl's fixup
// pass) are still read from disk and deleted afterwards, along with the MDL
// file itself. The final outputs are only .rmdl and .vg.
//
//  pInMemMDL  - pointer to the finalized MDL v49 binary in memory (pStart/phdr)
//  mdlFilePath - full path to the .mdl file that was just written; used to
//                derive the VVD/VTX/RMDL/VG sibling paths and then deleted.
void WriteRMDLFiles(const studiohdr_t* pInMemMDL, const char* mdlFilePath);

// Registers a .rui file to be embedded into the next WriteRMDLFiles() call.
// Called by the $ruimeshfile QC command handler in studiomdl.cpp.
// Pass nullptr or "" to clear.
void SetRuiMeshFile(const char* path);
