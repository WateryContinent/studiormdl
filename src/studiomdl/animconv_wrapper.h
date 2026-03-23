#pragma once
// animconv_wrapper.h
// Thin interface between studiormdl and the embedded R5-AnimConv code.
// Only plain C++ types are exposed here to avoid any type conflicts with
// studiormdl's mathlib (Quaternion, matrix3x4_t, etc.).

// Convert a v49 or v53 MDL file to .rrig + .rseq files.
//   mdl_path      - full filesystem path to the .mdl to convert
//   game_root     - game content root dir (gamedir); rrig/rseq are written here
//   rrig_override - override internal rrig asset path (empty = use name from MDL)
//   rseq_override - override internal rseq asset path (empty = use name from MDL)
//   verbose       - enable verbose output
void R5_ConvertMDLAnimations(const char* mdl_path,
                              const char* game_root,
                              const char* rrig_override,
                              const char* rseq_override,
                              bool        verbose_output);
