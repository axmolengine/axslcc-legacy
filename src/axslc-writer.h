//
// Copyright 2018 Sepehr Taghdisian (septag@github). All rights reserved.
// License: https://github.com/septag/glslcc#license-bsd-2-clause
//

//
// File version: 1.2.0
// File endianness: little
// 
// v1.1.0 CHANGES
//      - added num_storages_images, num_storage_buffers (CS specific) variables to sc_chunk_refl
// 
// v1.2.0 CHANGES
//      - added uniform block members
//
#pragma once

#include "sx/allocator.h"
#include "axslc-spec.h"

namespace axslc {

struct sc_file;

sc_file* sc_create_file(const sx_alloc* alloc, const char* filepath, uint16_t major_ver, uint16_t min_ver, uint32_t lang, uint32_t profile_ver);
void      sc_destroy_file(sc_file* f);
void      sc_add_stage_code(sc_file* f, uint32_t stage, const char* code);
void      sc_add_stage_code_bin(sc_file* f, uint32_t stage, const void* bytecode, int len);
void      sc_add_stage_reflect(sc_file* f, uint32_t stage, const void* reflect, int reflect_size);
bool      sc_commit(sc_file* f);

}
