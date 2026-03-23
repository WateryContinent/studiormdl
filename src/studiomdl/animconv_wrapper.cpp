// animconv_wrapper.cpp
// Implements R5_ConvertMDLAnimations() by delegating to the embedded
// R5-AnimConv source.  This translation unit intentionally does NOT include
// any studiormdl or mathlib headers so that the R5-AnimConv types (Vector3,
// Quaternion, Matrix3x4_t, ...) never collide with the Valve SDK types.

#include "animconv_wrapper.h"

// ---- R5-AnimConv headers (relative to this file's directory) ----
#include "animconv/animconv_pch.h"
#include "animconv/mdl/mdl.h"
#include "animconv/rrig/rrig.h"
#include "animconv/rseq/rseq.h"

// Globals declared in animconv/utils/print.cpp
extern bool _enable_verbose;
extern bool _enable_no_entry;

void R5_ConvertMDLAnimations(const char* mdl_path,
                              const char* game_root,
                              const char* rrig_override,
                              const char* rseq_override,
                              bool        verbose_output)
{
    _enable_verbose  = verbose_output;
    _enable_no_entry = true; // suppress inline RePak printout; we write outjson.txt ourselves

    // ------------------------------------------------------------------
    // Read the MDL file into memory
    // ------------------------------------------------------------------
    std::ifstream f(mdl_path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        printf("[animconv] ERROR: cannot open '%s'\n", mdl_path);
        return;
    }
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> buf(sz);
    if (!f.read(buf.data(), sz)) {
        printf("[animconv] ERROR: failed to read '%s'\n", mdl_path);
        return;
    }
    f.close();

    // ------------------------------------------------------------------
    // Detect MDL version (int32 at offset 4, right after the 'IDST' magic)
    // ------------------------------------------------------------------
    if (sz < 8) {
        printf("[animconv] ERROR: MDL file too small\n");
        return;
    }
    int mdl_version = *reinterpret_cast<int*>(buf.data() + 4);

    // ------------------------------------------------------------------
    // output_dir  = game content root (rrig/rseq go here, e.g. gamedir)
    // mdl_dir     = directory of the .mdl file (outjson.txt goes here)
    // ------------------------------------------------------------------
    std::filesystem::path mdl_fs_path(mdl_path);
    std::string mdl_dir    = mdl_fs_path.parent_path().string();

    // Use game_root as the base for rrig/rseq output.
    // Fall back to the MDL's parent dir if game_root is null/empty.
    std::string output_dir = (game_root && game_root[0]) ? game_root : mdl_dir;

    // Strip any trailing slash/backslash so path concatenation is consistent.
    while (!output_dir.empty() && (output_dir.back() == '\\' || output_dir.back() == '/'))
        output_dir.pop_back();

    // All output (animrig/, animseq/, outjson.txt) goes under gamedir/compiled/
    output_dir += "\\compiled";
    std::filesystem::create_directories(output_dir);

    std::string rrig_ov = rrig_override ? rrig_override : "";
    std::string rseq_ov = rseq_override ? rseq_override : "";

    // ------------------------------------------------------------------
    // Parse MDL → temp::rig_t
    // ------------------------------------------------------------------
    temp::rig_t rig{};

    try {
        if (mdl_version == 49 || mdl_version == 54) {
            // v54 uses the same binary layout as v49 (only the version stamp changed)
            ParseMDL_v49(buf.data(), rig, output_dir, rrig_ov, rseq_ov);
        } else if (mdl_version == 53) {
            ParseMDL_v53(buf.data(), rig, output_dir, rrig_ov, rseq_ov);
        } else {
            printf("[animconv] WARNING: unrecognised MDL version %d, skipping\n",
                   mdl_version);
            return;
        }
    } catch (const std::exception& e) {
        printf("[animconv] ERROR while parsing MDL: %s\n", e.what());
        return;
    }

    // ------------------------------------------------------------------
    // Write .rrig  (goes under output_dir\animrig\...)
    // ------------------------------------------------------------------
    try {
        WriteRRIG_v8(output_dir, rig);
    } catch (const std::exception& e) {
        printf("[animconv] ERROR while writing RRIG: %s\n", e.what());
        return;
    }

    // ------------------------------------------------------------------
    // Write .rseq files  (each seq.path = output_dir/animseq/...)
    // ------------------------------------------------------------------
    try {
        WriteRSEQ_v7(rig, /*bSkipEvents=*/false);
    } catch (const std::exception& e) {
        printf("[animconv] ERROR while writing RSEQ: %s\n", e.what());
        return;
    }

    printf("[animconv] Conversion complete: %d sequence(s) written under '%s'\n",
           (int)rig.sequences.size(), output_dir.c_str());

    // ------------------------------------------------------------------
    // Write outjson.txt — RePak JSON entry, placed at the gamedir root
    // ------------------------------------------------------------------
    std::string jsonPath = output_dir + "\\outjson.txt";
    std::ofstream jsonFile(jsonPath, std::ios::out);
    if (!jsonFile.is_open()) {
        printf("[animconv] WARNING: could not write outjson.txt to '%s'\n", jsonPath.c_str());
        return;
    }

    // Helper: normalise backslashes → forward slashes (RePak convention)
    auto normSlash = [](std::string s) {
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };

    std::string rigName    = normSlash(rig.name);
    std::string asset_type = (std::filesystem::path(rigName).extension().string() == ".rmdl")
                             ? "mdl_" : "arig";

    jsonFile << "    {\n"
             << "      \"_type\": \"" << asset_type << "\",\n"
             << "      \"_path\": \"" << rigName << "\"";

    if (!rig.rigpaths.empty()) {
        jsonFile << ",\n      \"$animrigs\": [\n";
        for (size_t i = 0; i < rig.rigpaths.size(); i++) {
            jsonFile << "        \"" << normSlash(rig.rigpaths[i]) << "\""
                     << (i + 1 < rig.rigpaths.size() ? "," : "") << "\n";
        }
        jsonFile << "      ]";
    }

    if (!rig.rseqpaths.empty()) {
        jsonFile << ",\n      \"$sequences\": [\n";
        for (size_t i = 0; i < rig.rseqpaths.size(); i++) {
            jsonFile << "        \"" << normSlash(rig.rseqpaths[i]) << "\""
                     << (i + 1 < rig.rseqpaths.size() ? "," : "") << "\n";
        }
        jsonFile << "      ]";
    }

    jsonFile << "\n    }\n";
    jsonFile.close();
    printf("[animconv] RePak entries written to '%s'\n", jsonPath.c_str());
}
