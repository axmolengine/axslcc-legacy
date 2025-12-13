//
// Copyright 2018 Sepehr Taghdisian (septag@github). All rights reserved.
// License: https://github.com/septag/glslcc#license-bsd-2-clause
//

#include "axslc-writer.h"

#include "sx/io.h"
#include "sx/array.h"
#include "sx/os.h"
#include "sx/string.h"

#include <string>

namespace axslc {

struct sc_stage {
    uint32_t    stage;
    union {
        char*   code;
        void*   data;
    };  
    uint32_t    data_size;     // =0 if it's not bytecode

    void*       refl;
    uint32_t    refl_size;

    void*       cs_refl;
    uint32_t    cs_refl_size;
};

struct sc_file
{
    const sx_alloc* alloc               = nullptr;
    std::string     filepath            = {};
    uint16_t        major_ver           = 0;
    uint16_t        minor_ver           = 0;
    uint32_t        lang                = 0;
    uint16_t        profile_ver         = 0;
    sc_stage*       stages              = nullptr;
};

sc_file* sc_create_file(const sx_alloc* alloc, const char* filepath, uint16_t major_ver, uint16_t min_ver, uint32_t lang, uint32_t profile_ver)
{
    sc_file* sc = new (sx_malloc(alloc, sizeof(sc_file))) sc_file;
    sc->alloc = alloc;
    sc->filepath = filepath;

    sc->major_ver = major_ver;
    sc->minor_ver = min_ver;
    sc->lang = lang;
    sc->profile_ver = profile_ver;

    return sc;
}

void sc_destroy_file(sc_file* f)
{
    sx_assert(f);
    sx_array_free(f->alloc, f->stages);
    f->~sc_file();
    sx_free(f->alloc, f);
}

void sc_add_stage_code(sc_file* f, uint32_t stage, const char* code)
{
    sc_stage* s = nullptr;
    // search in stages and see if find it
    for (int i = 0; i < sx_array_count(f->stages); i++) {
        if (f->stages[i].stage == stage) {
            s = &f->stages[i];
            break;
        }
    }

    if (!s) {
        s = sx_array_add(f->alloc, f->stages, 1);
        sx_memset(s, 0x0, sizeof(sc_stage));
        s->stage = stage;
    }

    int len = sx_strlen(code) + 1;
    sx_assert(s->code == nullptr);
    sx_assert(s->data_size == 0);

    s->code = (char*)sx_malloc(f->alloc, len);
    sx_assert(s->code);
    sx_memcpy(s->code, code, len);
}

void sc_add_stage_code_bin(sc_file* f, uint32_t stage, const void* bytecode, int len)
{
    sx_assert(len > 0);

    sc_stage* s = nullptr;
    // search in stages and see if find it
    for (int i = 0; i < sx_array_count(f->stages); i++) {
        if (f->stages[i].stage == (int)stage) {
            s = &f->stages[i];
            break;
        }
    }

    if (!s) {
        s = sx_array_add(f->alloc, f->stages, 1);
        sx_memset(s, 0x0, sizeof(sc_stage));
        s->stage = stage;
    }
    
    sx_assert(s->data == nullptr);
    sx_assert(s->data_size == 0);

    s->data = (char*)sx_malloc(f->alloc, len);
    sx_memcpy(s->data, bytecode, len);
    s->data_size = len;
}

void sc_add_stage_reflect(sc_file* f, uint32_t stage, const void* reflect, int refl_size)
{
    sc_stage* s = nullptr;
    // search in stages and see if find it
    for (int i = 0; i < sx_array_count(f->stages); i++) {
        if (f->stages[i].stage == (int)stage) {
            s = &f->stages[i];
            break;
        }
    }

    if (!s) {
        s = sx_array_add(f->alloc, f->stages, 1);
        sx_memset(s, 0x0, sizeof(sc_stage));
        s->stage = stage;
    }

    sx_assert(s->refl == nullptr);
    sx_assert(s->refl_size == 0);

    s->refl = sx_malloc(f->alloc, refl_size);
    sx_memcpy(s->refl, reflect, refl_size);
    s->refl_size = refl_size;
}

bool sc_commit(sc_file* f)
{
    sx_file_writer writer;
    if (!sx_file_open_writer(&writer, f->filepath.c_str(), 0))
        return false;

    // write main chunk
    const uint32_t sc_magic = SC_CHUNK;
    uint32_t sc_size = 0; // place holder

    sc_size += sx_file_write_var(&writer, sc_magic);
    const uint32_t sc_size_offset = sizeof(sc_magic);
    sc_size += sx_file_write_var(&writer, sc_size);

    sc_chunk sc_header;
    sc_header.major = f->major_ver;
    sc_header.minor = f->minor_ver;
    sc_header.lang = f->lang;
    sc_header.profile_ver = f->profile_ver;
    sc_size += sx_file_write_var(&writer, sc_header);

    // write stages
    for (int i = 0; i < sx_array_count(f->stages); i++) {
        const sc_stage* s = &f->stages[i];

        const uint32_t code_size = (s->data_size == 0 ? (sx_strlen(s->code)+1) : 0);
        const uint32_t data_size = s->data_size;
        sx_assert(code_size || data_size);

        const uint32_t stage_size = 
            (s->refl ? (8 + s->refl_size) : 0) +
            (8 + code_size + data_size) +
            sizeof(uint32_t);
        
        // `STAG`
        const uint32_t _stage = SC_CHUNK_STAG;
        sc_size += sx_file_write_var(&writer, _stage);
        sc_size += sx_file_write_var(&writer, stage_size);
        sc_size += sx_file_write_var(&writer, s->stage);

        if (code_size) {
            // `CODE`
            const uint32_t _code = SC_CHUNK_CODE;
            const uint32_t code_size = sx_strlen(s->code) + 1;
            sc_size += sx_file_write_var(&writer, _code);
            sc_size += sx_file_write_var(&writer, code_size);
            sc_size += sx_file_write(&writer, s->code, code_size);
        } else if (data_size) {
            // `DATA`
            const uint32_t _data = SC_CHUNK_DATA;
            sc_size += sx_file_write_var(&writer, _data);
            sc_size += sx_file_write_var(&writer, s->data_size);
            sc_size += sx_file_write(&writer, s->data, s->data_size);
        }

        // `REFL`
        if (s->refl) {
            const uint32_t _refl = SC_CHUNK_REFL;
            sc_size += sx_file_write_var(&writer, _refl);
            sc_size += sx_file_write_var(&writer, s->refl_size);
            sc_size += sx_file_write(&writer, s->refl, s->refl_size);
        }
    }

    // finish sc size
    sx_file_seekw(&writer, sc_size_offset, SX_WHENCE_BEGIN);
    sx_file_write_var(&writer, sc_size);

    sx_file_close_writer(&writer);

    return true;
}

}
