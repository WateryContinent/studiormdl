// rui_parse.cpp
// Parser for the .rui text file format (RUI mesh descriptions for RMDL v54).
// See rui_parse.h for the full format specification.

#include "rui_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

// ---------------------------------------------------------------------------
// Minimal tokeniser
// ---------------------------------------------------------------------------

namespace
{

struct Tokenizer
{
    FILE*  fp;
    char   path[512];
    int    line;
    char   buf[4096];
    char*  cur;     // current position in buf
    char   tok[1024];
    bool   eof;
    bool   pushedBack; // one-token lookahead

    bool open(const char* p)
    {
        fp = fopen(p, "r");
        if (!fp) return false;
        strncpy(path, p, sizeof(path)-1);
        line = 1;
        buf[0] = '\0';
        cur = buf;
        eof = false;
        pushedBack = false;
        return true;
    }

    void close()
    {
        if (fp) { fclose(fp); fp = nullptr; }
    }

    void error(const char* fmt, ...) const
    {
        char msg[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        fprintf(stderr, "[RUI] %s(%d): %s\n", path, line, msg);
    }

    // Refill buf from file; returns false at true EOF.
    bool refill()
    {
        while (!feof(fp))
        {
            if (!fgets(buf, sizeof(buf), fp))
            {
                eof = true;
                return false;
            }
            line++;
            cur = buf;
            // skip leading whitespace
            while (*cur && isspace((unsigned char)*cur)) { if (*cur == '\n') {} ++cur; }
            // skip comment lines
            if (cur[0] == '/' && cur[1] == '/') continue;
            if (*cur) return true;
        }
        eof = true;
        return false;
    }

    // Read the next token into tok[].  Returns false at EOF.
    bool next()
    {
        if (pushedBack) { pushedBack = false; return true; }

        for (;;)
        {
            // skip whitespace in current line
            while (*cur && (*cur == ' ' || *cur == '\t' || *cur == '\r')) ++cur;

            // end of line / comment — refill
            if (!*cur || *cur == '\n' || (cur[0] == '/' && cur[1] == '/'))
            {
                if (!refill()) return false;
                continue;
            }

            // quoted string
            if (*cur == '"')
            {
                ++cur;
                int i = 0;
                while (*cur && *cur != '"' && *cur != '\n')
                    tok[i++] = *cur++;
                tok[i] = '\0';
                if (*cur == '"') ++cur;
                return true;
            }

            // bare token (no spaces, no {})
            int i = 0;
            while (*cur && !isspace((unsigned char)*cur) && *cur != '{' && *cur != '}')
                tok[i++] = *cur++;
            if (i > 0)
            {
                tok[i] = '\0';
                return true;
            }

            // single-char tokens: { }
            tok[0] = *cur++;
            tok[1] = '\0';
            return true;
        }
    }

    // Push the current token back so next next() returns it again.
    void pushBack() { pushedBack = true; }

    // Expect a specific token; returns false and emits error if not matched.
    bool expect(const char* s)
    {
        if (!next()) { error("expected '%s', got EOF", s); return false; }
        if (strcmp(tok, s) != 0) { error("expected '%s', got '%s'", s, tok); return false; }
        return true;
    }

    float readFloat()
    {
        if (!next()) { error("expected float, got EOF"); return 0.f; }
        return (float)atof(tok);
    }

    int readInt()
    {
        if (!next()) { error("expected int, got EOF"); return 0; }
        return atoi(tok);
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Parser implementation
// ---------------------------------------------------------------------------

bool ParseRuiFile(const char* path, RuiFile& out)
{
    out.version = 0;
    out.meshes.clear();

    Tokenizer tz;
    if (!tz.open(path))
    {
        fprintf(stderr, "[RUI] Cannot open '%s'\n", path);
        return false;
    }

    // --- version line ---
    if (!tz.next()) { tz.error("empty file"); tz.close(); return false; }
    if (strcmp(tz.tok, "version") != 0)
    {
        tz.error("expected 'version', got '%s'", tz.tok);
        tz.close();
        return false;
    }
    out.version = tz.readInt();
    if (out.version != 1)
    {
        tz.error("unsupported version %d (only version 1 is supported)", out.version);
        tz.close();
        return false;
    }

    // --- ruimesh blocks ---
    while (tz.next())
    {
        if (strcmp(tz.tok, "ruimesh") != 0)
        {
            tz.error("expected 'ruimesh', got '%s'", tz.tok);
            tz.close();
            return false;
        }

        RuiMesh mesh;

        // mesh name
        if (!tz.next()) { tz.error("expected mesh name after 'ruimesh'"); tz.close(); return false; }
        mesh.name = tz.tok;

        if (!tz.expect("{")) { tz.close(); return false; }

        // optional namehash immediately before the opening brace is allowed too
        // (handled inside the block loop below)

        // mesh body
        while (tz.next())
        {
            if (strcmp(tz.tok, "}") == 0) break;

            if (strcmp(tz.tok, "namehash") == 0)
            {
                // Explicit hash override written by the decompiler.
                // Value is a hex or decimal integer (e.g. 0xC49D5877 or 3299956855).
                if (!tz.next()) { tz.error("expected hash value"); tz.close(); return false; }
                mesh.namehash = (int32_t)strtoul(tz.tok, nullptr, 0);
            }
            else if (strcmp(tz.tok, "unk") == 0)
            {
                // Raw value of mstudioruimesh_t_v54::unk (offset +6).
                // Written by the decompiler for round-trip fidelity.
                if (!tz.next()) { tz.error("expected unk value"); tz.close(); return false; }
                mesh.unk = (int16_t)atoi(tz.tok);
            }
            else if (strcmp(tz.tok, "bone") == 0)
            {
                if (!tz.next()) { tz.error("expected bone name"); tz.close(); return false; }
                mesh.boneNames.push_back(tz.tok);
            }
            else if (strcmp(tz.tok, "vertex") == 0)
            {
                RuiVertex v;
                if (!tz.next()) { tz.error("expected bone name for vertex"); tz.close(); return false; }
                v.boneName = tz.tok;
                v.x = tz.readFloat();
                v.y = tz.readFloat();
                v.z = tz.readFloat();
                mesh.vertices.push_back(v);
            }
            else if (strcmp(tz.tok, "face") == 0)
            {
                RuiFace f;
                // vertex indices
                f.vertid[0] = (int16_t)tz.readInt();
                f.vertid[1] = (int16_t)tz.readInt();
                f.vertid[2] = (int16_t)tz.readInt();
                // fourthvert bytes
                f.vertextra  = (int8_t)tz.readInt();
                f.vertextra1 = (int8_t)tz.readInt();
                // UV min/max
                f.uvminx = tz.readFloat();
                f.uvminy = tz.readFloat();
                f.uvmaxx = tz.readFloat();
                f.uvmaxy = tz.readFloat();
                // scale min/max
                f.scaleminx = tz.readFloat();
                f.scaleminy = tz.readFloat();
                f.scalemaxx = tz.readFloat();
                f.scalemaxy = tz.readFloat();
                mesh.faces.push_back(f);
            }
            else
            {
                tz.error("unknown keyword '%s' inside ruimesh block", tz.tok);
                tz.close();
                return false;
            }
        }

        out.meshes.push_back(std::move(mesh));
    }

    tz.close();
    return true;
}
