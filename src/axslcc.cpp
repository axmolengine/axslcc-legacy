//
// Copyright 2018 Sepehr Taghdisian (septag@github). All rights reserved.
// Copyright 2023~2025 axmol.dev, All rights reserved.
// License: https://github.com/axmolengine/axslcc#license-bsd-2-clause
// Original: https://github.com/septag/glslcc#license-bsd-2-clause
//
//
// Version History
//      1.0.0       Initial release
//      1.1.0       SGS file support (native binary format that holds all shaders and reflection data)
//      1.2.0       Added HLSL vertex semantics
//      1.2.1       Linux build
//      1.2.2       shader filename fix
//      1.2.3       memory corruption fix
//      1.3.0       D3D11 compiler for windows
//      1.3.1       Added -g for byte-code compiler flags
//      1.3.2       Fixed vertex semantic names for reflection
//      1.4.0       Added GLSL shader support
//      1.4.1       More reflection data, like uniform block members and types
//      1.4.2       Small bug fixed in flatten uniform block array size
//      1.4.3       Bug fixed for MSL shaders where vertex input attributes needed to be sequential (SEMANTIC conflict)
//      1.4.4       Minor bug fixes in SGS export
//      1.4.5       Improved binary header writer to uint32_t
//      1.5.0       Sgs format is now IFF like
//      1.5.1       updated sx lib
//      1.6.0       shader validation, output custom compiler error formats
//      1.61.0      gcc output error format
//      1.62.0      removed "#version 200 es" inclusion from spirv_glsl.cpp, 330 default for GLSL
//      1.63.0      added relfection data to compute shader for SGS files
//      1.7.0       spirv-cross / glslang update
//      1.7.1       bug fixed in spirv_glsl::emit_header for OpenGL 3+ GLSL shaders
//      1.7.2       bugs fixed in parsing --defines and --include-dirs flags
//      1.7.3       Added current file directory to the include-dirs
//      1.7.4       Added //@begin_vert //@begin_frag //@end tags in .glsl files
//      1.7.5       List include names in the shader with -L argument
//      1.7.6       Fixed bugs in parse output
//      1.8.0       Add uniform block member reflection info
//                  Strip invalid reflect data for sgs file
//                  Fix attribs reflect info location not match with translated MSL shaders
//                  Update SPIRV-Cross & glslang to latest on 7/13/2023
//                  Add SPIRV output support
//                  Indent with spaces instead Tab when output cvar header file
//                  Rename shader lang `gles` to `essl`
//                  Add option -a --automap to remove binding and location requirement in shader
//      1.8.1       Add option -n --no-suffix for don't add _fs or _vs suffix in output files
//      1.9.0       Report error when catch type which is not supported by glslcc
//                  Auto fix struct alignment size_bytes for MSL
//                  Remove mat3x4, mat4x3
//      1.9.1       Vertex shader: emit precision qualifiers for essl profile
//                  Update glslang to: 12.3.1
//                  Update spirv-corss to: 633dc30 (Aug 17, 2023)
//      1.9.2       Revert `Vertex shader: emit precision qualifiers for essl profile`
//      1.9.3       Expand uniform block members for GLSL/ESSL100
//      1.9.4       Fix MSL texture order does not follow GLSL binding order
//      1.9.5       Build for macos-arm64
//                  Build for macos-10.15
//      1.9.6       Rename glslcc to axslcc
//      1.10.0      Update SPIRV-corss to git-7fde353 (Until Aug 11, 2025)
//                  Fix compile error
//                  Fix sgs refl mat4 semantic name for HLSL
//      1.11.0      Enables HLSL input support
//      1.12.0      Fix msl vertex location overlaps when contains mat4
//                  Add option --msl_ios for target iOS MSL
//                  Add option --fixup_clipspace
//                  Add option --msl_reset_vlocs
//      1.13.0      Target MSL default version to 2.0
//      1.13.1      Split legacy --automap option into two distinct flags: --auto-map-bindings (for resource bindings) and --auto-map-locations (for shader I/O locations).
//      1.13.2      Add option --inline-ubo-members, previous option name: --flatten-ubos is deprecated
//      1.15.0      Fix spirv code output truncation when --sgs set
//                  Add --profile version support for SPIRV (default: 100)
//                  Update spirv-cross: 542db37(4130) (Until Nov 7, 2025)
//                  Update glslang: 1c7030f(5357) (Until Nov 11, 2025)
//                  Remove SPVRemapper linkage
//      3.0.0       Optimized sc_refl_texture by introducing field 'count' to clearly represent descriptor array length
//      3.1.0       Add layout decoration 'sampler_slot` support for uniform sampler2D
//      3.1.1       Register builtin sampler state symbols
// 
//      3.2.0       Unify and re-enumerate vertex input & uniform variable types
//      3.3.0       Write sc size into binary header
//      3.3.1       Use debian-11 to build linux
//

/**
 * @since 1.9.5
 * - Fix MSL texture order does not follow GLSL binding order
 *   - https://github.com/KhronosGroup/SPIRV-Cross/issues/2140
 *   - https://github.com/KhronosGroup/SPIRV-Cross/issues/1971
 *   - https://github.com/KhronosGroup/SPIRV-Cross/issues/1464
 */

#define _ALLOW_KEYWORD_MACROS
#define ENABLE_OPT 1

#include "sx/allocator.h"
#include "sx/array.h"
#include "sx/cmdline.h"
#include "sx/io.h"
#include "sx/os.h"
#include "sx/string.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <iterator>

#include "SPIRV/GlslangToSpv.h"
#include "SPIRV/SpvTools.h"
#include "SPIRV/disassemble.h"
#include "spirv-tools/libspirv.hpp"
#include "spirv-tools/optimizer.hpp"

#include "glslang/Public/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"

#include "spirv_cross.hpp"
#include "spirv_glsl.hpp"
#include "spirv_hlsl.hpp"
#include "spirv_msl.hpp"

#include "axslc-writer.h"

#ifdef _WIN32
#include <d3dcompiler.h>
#endif

// sjson
#define sjson_malloc(user, size) sx_malloc((const sx_alloc*)user, size)
#define sjson_free(user, ptr) sx_free((const sx_alloc*)user, ptr)
#define sjson_realloc(user, ptr, size) sx_realloc((const sx_alloc*)user, ptr, size)
#define sjson_assert(e) sx_assert(e);
#define sjson_snprintf sx_snprintf
#define sjson_strcpy(dst, n, src) sx_strcpy(dst, n, src)
#define SJSON_IMPLEMENTATION
#include "../3rdparty/sjson/sjson.h"

#define AXSLCC_VERSION_MAJOR 3
#define AXSLCC_VERSION_MINOR 3
#define AXSLCC_VERSION_REVISION 1

using namespace axslc;

static const sx_alloc* g_alloc = sx_alloc_malloc();
static sc_file* sc_file_handle = nullptr;

struct p_define {
    char* def;
    char* val;
};

enum shader_lang {
    SHADER_LANG_ESSL = 0,
    SHADER_LANG_HLSL,
    SHADER_LANG_MSL,
    SHADER_LANG_GLSL,
    SHADER_LANG_SPIRV,
    SHADER_LANG_COUNT
};

enum output_error_format {
    OUTPUT_ERRORFORMAT_GLSLANG = 0,
    OUTPUT_ERRORFORMAT_MSVC,
    OUTPUT_ERRORFORMAT_GCC
};

static const char* k_shader_types[SHADER_LANG_COUNT] = {
    "essl",
    "hlsl",
    "msl",
    "glsl",
    "spirv"
};

static const uint32_t k_shader_langs_fourcc[SHADER_LANG_COUNT] = {
    SC_LANG_GLES,
    SC_LANG_HLSL,
    SC_LANG_MSL,
    SC_LANG_GLSL,
    SC_LANG_SPIRV,
};

enum vertex_attribs {
    VERTEX_POSITION = 0,
    VERTEX_NORMAL,
    VERTEX_TEXCOORD0,
    VERTEX_TEXCOORD1,
    VERTEX_TEXCOORD2,
    VERTEX_TEXCOORD3,
    VERTEX_TEXCOORD4,
    VERTEX_TEXCOORD5,
    VERTEX_TEXCOORD6,
    VERTEX_TEXCOORD7,
    VERTEX_COLOR0,
    VERTEX_COLOR1,
    VERTEX_COLOR2,
    VERTEX_COLOR3,
    VERTEX_TANGENT,
    VERTEX_BITANGENT,
    VERTEX_INDICES,
    VERTEX_WEIGHTS,
    VERTEX_ATTRIB_COUNT
};

static const char* k_attrib_names[VERTEX_ATTRIB_COUNT] = {
    "POSITION",
    "NORMAL",
    "TEXCOORD0",
    "TEXCOORD1",
    "TEXCOORD2",
    "TEXCOORD3",
    "TEXCOORD4",
    "TEXCOORD5",
    "TEXCOORD6",
    "TEXCOORD7",
    "COLOR0",
    "COLOR1",
    "COLOR2",
    "COLOR3",
    "TANGENT",
    "BINORMAL",
    "BLENDINDICES",
    "BLENDWEIGHT"
};

static const char* k_attrib_sem_names[VERTEX_ATTRIB_COUNT] = {
    "POSITION",
    "NORMAL",
    "TEXCOORD",
    "TEXCOORD",
    "TEXCOORD",
    "TEXCOORD",
    "TEXCOORD",
    "TEXCOORD",
    "TEXCOORD",
    "TEXCOORD",
    "COLOR",
    "COLOR",
    "COLOR",
    "COLOR",
    "TANGENT",
    "BINORMAL",
    "BLENDINDICES",
    "BLENDWEIGHT"
};

static int k_attrib_sem_indices[VERTEX_ATTRIB_COUNT] = {
    0,
    0,
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    0,
    1,
    2,
    3,
    0,
    0,
    0,
    0
};

static const char* k_builtin_sampler_states[] = {
    "LinearClamp", // 0
    "LinearWrap", // 1
    "LinearMirror", // 2
    "LinearBorder", // 3

    "PointClamp", // 4
    "PointWrap", // 5
    "PointMirror", // 6
    "PointBorder", // 7

    "LinearMipClamp", // 8
    "LinearMipWrap", // 9
    "LinearMipMirror", // 10
    "LinearMipBorder", // 11

    "AnisoClamp", // 12
    "AnisoWrap", // 13
    "AnisoMirror", // 14
    "AnisoBorder", // 15

    "ShadowCmpClamp", // 16
    "ShadowCmpWrap", // 17
    "ShadowCmpMirror", // 18
    "ShadowCmpBorder", // 19

    "LinearNoMipClamp", // 20
    "PointNoMipClamp" // 21
};


// Includer
class Includer : public glslang::TShader::Includer {
public:
    Includer()
        : glslang::TShader::Includer()
    {
        m_listIncludes = false;
    }

    explicit Includer(bool list_files)
        : glslang::TShader::Includer()
    {
        m_listIncludes = list_files;
    }

    virtual ~Includer() {}

    IncludeResult* includeSystem(const char* headerName,
        const char* includerName,
        size_t inclusionDepth) override
    {
        for (auto i = m_systemDirs.begin(); i != m_systemDirs.end(); ++i) {
            std::string header_path(*i);
            if (!header_path.empty() && header_path.back() != '/')
                header_path += "/";
            header_path += headerName;

            if (sx_os_stat(header_path.c_str()).type == SX_FILE_TYPE_REGULAR) {
                sx_mem_block* mem = sx_file_load_bin(g_alloc, header_path.c_str());
                if (mem) {
                    if (m_listIncludes) {
                        puts(header_path.c_str());
                    }
                    return new (sx_malloc(g_alloc, sizeof(IncludeResult)))
                        IncludeResult(header_path, (const char*)mem->data, (size_t)mem->size, mem);
                }
            }
        }
        return nullptr;
    }

    IncludeResult* includeLocal(const char* headerName,
        const char* includerName,
        size_t inclusionDepth) override
    {
        char cur_dir[256];
        sx_os_path_pwd(cur_dir, sizeof(cur_dir));
        std::string header_path(cur_dir);
        std::replace(header_path.begin(), header_path.end(), '\\', '/');
        if (header_path.back() != '/')
            header_path += "/";
        header_path += headerName;

        sx_mem_block* mem = sx_file_load_bin(g_alloc, header_path.c_str());
        if (mem) {
            if (m_listIncludes) {
                puts(headerName);
            }
            return new (sx_malloc(g_alloc, sizeof(IncludeResult)))
                IncludeResult(header_path, (const char*)mem->data, (size_t)mem->size, mem);
        }
        return nullptr;
    }

    // Signals that the parser will no longer use the contents of the
    // specified IncludeResult.
    void releaseInclude(IncludeResult* result) override
    {
        if (result) {
            sx_mem_block* mem = (sx_mem_block*)result->userData;
            if (mem)
                sx_mem_destroy_block(mem);
            result->~IncludeResult();
            sx_free(g_alloc, result);
        }
    }

    void addSystemDir(const char* dir)
    {
        std::string std_dir(dir);
        std::replace(std_dir.begin(), std_dir.end(), '\\', '/');
        m_systemDirs.push_back(std_dir);
    }

    void addIncluder(const Includer& includer)
    {
        for (const std::string& inc : includer.m_systemDirs) {
            m_systemDirs.push_back(inc);
        }
    }

private:
    std::vector<std::string> m_systemDirs;
    bool m_listIncludes;
};

struct cmd_args {
    const char* vs_filepath;
    const char* fs_filepath;
    const char* cs_filepath;
    const char* out_filepath;
    shader_lang lang;
    p_define* defines;
    Includer includer;
    int profile_ver;
    int invert_y;
    int preprocess;
    int no_suffix;
    int sc_file;
    int reflect;
    int compile_bin;
    int debug_info;
    int optimize;
    int silent;
    int validate;
    int list_includes;
    int flatten_ubo; // for GLES < 3.0 only
    int inline_ubo_members; // for GLES < 3.0 only
    int automap; // deprecated
    int auto_map_bindings;
    int auto_map_locations;
    int msl_ios;
    int fixup_clipspace;
    int msl_reset_vlocs;
    output_error_format err_format;
    const char* cvar;
    const char* reflect_filepath;
};

static void print_version()
{
    printf("axslcc v%d.%d.%d\n\nAxslcc suite maintained and supported by axmol community (axmol.dev)\n", AXSLCC_VERSION_MAJOR, AXSLCC_VERSION_MINOR, AXSLCC_VERSION_REVISION);
}

static void print_help(sx_cmdline_context* ctx)
{
    char buffer[4096];
    print_version();
    puts("");
    puts(sx_cmdline_create_help_string(ctx, buffer, sizeof(buffer)));
    puts("Current supported shader stages are:\n"
         "\t- Vertex shader (--vert)\n"
         "\t- Fragment shader (--frag)\n"
         "\t- Compute shader (--compute)\n");
    exit(0);
}

static shader_lang parse_shader_lang(const char* arg)
{
    if (sx_strequalnocase(arg, "metal"))
        arg = "msl";

    for (int i = 0; i < SHADER_LANG_COUNT; i++) {
        if (sx_strequalnocase(k_shader_types[i], arg)) {
            return (shader_lang)i;
        }
    }

    if (sx_strequalnocase(arg, "gles")) // compatible shader lang name, prefer to: essl
        return shader_lang::SHADER_LANG_ESSL;

    puts("Invalid shader type");
    exit(-1);
}

static output_error_format parse_output_errorformat(const char* arg)
{
    if (sx_strequalnocase(arg, "msvc")) {
        return OUTPUT_ERRORFORMAT_MSVC;
    } else if (sx_strequalnocase(arg, "glslang")) {
        return OUTPUT_ERRORFORMAT_GLSLANG;
    }

    return OUTPUT_ERRORFORMAT_GLSLANG;
}

static void parse_defines(cmd_args* args, const char* defines)
{
    sx_assert(defines);
    const char* def = defines;

    do {
        def = sx_skip_whitespace(def);
        if (def[0]) {
            const char* next_def = sx_strchar(def, ',');
            if (!next_def)
                next_def = sx_strchar(def, ';');
            int len = next_def ? (int)(uintptr_t)(next_def - def) : sx_strlen(def);

            if (len > 0) {
                p_define d = { 0x0 };
                d.def = (char*)sx_malloc(g_alloc, len + 1);
                sx_strncpy(d.def, len + 1, def, len);
                sx_trim_whitespace(d.def, len + 1, d.def);

                // Check def=value pair
                char* equal = (char*)sx_strchar(d.def, '=');
                if (equal) {
                    *equal = 0;
                    d.val = equal + 1;
                }

                sx_array_push(g_alloc, args->defines, d);
            }

            def = next_def;
            if (def) {
                def++;
            }
        }
    } while (def);
}

static sx_mem_block* d3d_compile_binary(const char* code, const char* filename,
    int profile_version, EShLanguage stage, int debug)
{
#ifdef _WIN32
    ID3DBlob* output = NULL;
    ID3DBlob* errors = NULL;

    int major_ver = profile_version / 10;
    int minor_ver = profile_version % 10;
    char target[32];
    switch (stage) {
    case EShLangVertex:
        sx_snprintf(target, sizeof(target), "vs_%d_%d", major_ver, minor_ver);
        break;
    case EShLangFragment:
        sx_snprintf(target, sizeof(target), "ps_%d_%d", major_ver, minor_ver);
        break;
    case EShLangCompute:
        sx_snprintf(target, sizeof(target), "cs_%d_%d", major_ver, minor_ver);
        break;
    }

    uint32_t compile_flags;
    if (!debug)
        compile_flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    else
        compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

    HRESULT hr = D3DCompile(
        code, /* pSrcData */
        sx_strlen(code), /* SrcDataSize */
        filename, /* pSourceName */
        NULL, /* pDefines */
        NULL, /* pInclude */
        "main", /* pEntryPoint */
        target, /* pTarget (vs_5_0 or ps_5_0) */
        compile_flags, /* Flags1 */
        0, /* Flags2 */
        &output, /* ppCode */
        &errors); /* ppErrorMsgs */

    if (FAILED(hr)) {
        if (errors) {
            puts((LPCSTR)errors->GetBufferPointer());
            errors->Release();
        }
        return nullptr;
    }

    sx_mem_block* mem = sx_mem_create_block(g_alloc,
        (int)output->GetBufferSize(),
        output->GetBufferPointer());
    output->Release();
    return mem;
#else
    return nullptr;
#endif
}

static void cleanup_args(cmd_args* args)
{
    for (int i = 0; i < sx_array_count(args->defines); i++) {
        if (args->defines[i].def)
            sx_free(g_alloc, args->defines[i].def);
    }
    sx_array_free(g_alloc, args->defines);
}

static const char* get_stage_name(EShLanguage stage)
{
    switch (stage) {
    case EShLangVertex:
        return "vs";
    case EShLangFragment:
        return "fs";
    case EShLangCompute:
        return "cs";
    default:
        sx_assert(0);
        return nullptr;
    }
}

static void parse_includes(cmd_args* args, const char* includes)
{
    sx_assert(includes);
    const char* inc = includes;

    do {
        inc = sx_skip_whitespace(inc);
        if (inc[0]) {
            const char* next_inc = sx_strchar(inc, ';');
            int len = next_inc ? (int)(uintptr_t)(next_inc - inc) : sx_strlen(inc);

            if (len > 0) {
                char* inc_str = (char*)sx_malloc(g_alloc, len + 1);
                sx_assert(inc_str);
                sx_strncpy(inc_str, len + 1, inc, len);
                args->includer.addSystemDir(inc_str);
                sx_free(g_alloc, inc_str);
            }

            inc = next_inc;
            if (inc) {
                inc++;
            }
        }
    } while (inc);
}

static void add_defines(glslang::TShader* shader, const cmd_args& args, std::string& def)
{
    std::vector<std::string> processes;

    for (int i = 0; i < sx_array_count(args.defines); i++) {
        const p_define& d = args.defines[i];
        def += "#define " + std::string(d.def);
        if (d.val) {
            def += std::string(" ") + std::string(d.val);
        }
        def += std::string("\n");

        char process[256];
        sx_snprintf(process, sizeof(process), "D%s", d.def);
        processes.push_back(process);
    }

    shader->setPreamble(def.c_str());
    shader->addProcesses(processes);
}

enum resource_type {
    RES_TYPE_REGULAR = 0,
    RES_TYPE_SSBO,
    RES_TYPE_VERTEX_INPUT,
    RES_TYPE_TEXTURE,
    RES_TYPE_UNIFORM_BUFFER
};

struct variable_type_mapping {
    spirv_cross::SPIRType::BaseType base_type;
    int vec_size;
    int columns;
    const char* type_str;
    uint16_t sc_type;
};

static const variable_type_mapping k_variable_type_map[] = {
    { spirv_cross::SPIRType::Float, 1, 1, "float", SC_TYPE_FLOAT },
    { spirv_cross::SPIRType::Float, 2, 1, "float2", SC_TYPE_FLOAT2 },
    { spirv_cross::SPIRType::Float, 3, 1, "float3", SC_TYPE_FLOAT3 },
    { spirv_cross::SPIRType::Float, 4, 1, "float4", SC_TYPE_FLOAT4 },
    { spirv_cross::SPIRType::Float, 3, 3, "mat3", SC_TYPE_MAT3 },
    { spirv_cross::SPIRType::Float, 4, 4, "mat4", SC_TYPE_MAT4 },
    { spirv_cross::SPIRType::Int, 1, 1, "int", SC_TYPE_INT },
    { spirv_cross::SPIRType::Int, 2, 1, "int2", SC_TYPE_INT2 },
    { spirv_cross::SPIRType::Int, 3, 1, "int3", SC_TYPE_INT3 },
    { spirv_cross::SPIRType::Int, 4, 1, "int4", SC_TYPE_INT4 },
    { spirv_cross::SPIRType::Half, 4, 1, "float", SC_TYPE_HALF },
    { spirv_cross::SPIRType::Half, 4, 2, "float2", SC_TYPE_HALF2 },
    { spirv_cross::SPIRType::Half, 4, 3, "float3", SC_TYPE_HALF3 },
    { spirv_cross::SPIRType::Half, 4, 4, "float4", SC_TYPE_HALF4 },
    { spirv_cross::SPIRType::UShort, 4, 1, "ushort4", SC_TYPE_USHORT4 },
    { spirv_cross::SPIRType::UShort, 2, 1, "ushort2", SC_TYPE_USHORT2 },
    { spirv_cross::SPIRType::UByte, 4, 1, "ubyte4", SC_TYPE_UBYTE4 },
};

static const char* spirv_basetype_to_name(int basetype)
{
    switch (basetype) {
    case spirv_cross::SPIRType::Float:
        return "float";
    case spirv_cross::SPIRType::Int:
        return "int";
    case spirv_cross::SPIRType::Half:
        return "half";
    case spirv_cross::SPIRType::Boolean:
        return "bool";
    case spirv_cross::SPIRType::UShort:
        return "ushort";
    case spirv_cross::SPIRType::UByte:
        return "ubyte";
    default:
        return "unknown";
    }
}

enum ImageFormat {
    ImageFormatUnknown = 0,
    ImageFormatRgba32f = 1,
    ImageFormatRgba16f = 2,
    ImageFormatR32f = 3,
    ImageFormatRgba8 = 4,
    ImageFormatRgba8Snorm = 5,
    ImageFormatRg32f = 6,
    ImageFormatRg16f = 7,
    ImageFormatR11fG11fB10f = 8,
    ImageFormatR16f = 9,
    ImageFormatRgba16 = 10,
    ImageFormatRgb10A2 = 11,
    ImageFormatRg16 = 12,
    ImageFormatRg8 = 13,
    ImageFormatR16 = 14,
    ImageFormatR8 = 15,
    ImageFormatRgba16Snorm = 16,
    ImageFormatRg16Snorm = 17,
    ImageFormatRg8Snorm = 18,
    ImageFormatR16Snorm = 19,
    ImageFormatR8Snorm = 20,
    ImageFormatRgba32i = 21,
    ImageFormatRgba16i = 22,
    ImageFormatRgba8i = 23,
    ImageFormatR32i = 24,
    ImageFormatRg32i = 25,
    ImageFormatRg16i = 26,
    ImageFormatRg8i = 27,
    ImageFormatR16i = 28,
    ImageFormatR8i = 29,
    ImageFormatRgba32ui = 30,
    ImageFormatRgba16ui = 31,
    ImageFormatRgba8ui = 32,
    ImageFormatR32ui = 33,
    ImageFormatRgb10a2ui = 34,
    ImageFormatRg32ui = 35,
    ImageFormatRg16ui = 36,
    ImageFormatRg8ui = 37,
    ImageFormatR16ui = 38,
    ImageFormatR8ui = 39,
    ImageFormatMax = 0x7fffffff,
};

const char* k_texture_format_str[spv::ImageFormatR8ui + 1] = {
    "unknown",
    "Rgba32f",
    "Rgba16f",
    "R32f",
    "Rgba8",
    "Rgba8Snorm",
    "Rg32f",
    "Rg16f",
    "R11fG11fB10f",
    "R16f",
    "Rgba16",
    "Rgb10A2",
    "Rg16",
    "Rg8",
    "R16",
    "R8",
    "Rgba16Snorm",
    "Rg16Snorm",
    "Rg8Snorm",
    "R16Snorm",
    "R8Snorm",
    "Rgba32i",
    "Rgba16i",
    "Rgba8i",
    "R32i",
    "Rg32i",
    "Rg16i",
    "Rg8i",
    "R16i",
    "R8i",
    "Rgba32ui",
    "Rgba16ui",
    "Rgba8ui",
    "R32ui",
    "Rgb10a2ui",
    "Rg32ui",
    "Rg16ui",
    "Rg8ui",
    "R16ui",
    "R8ui"
};

const char* k_texture_dim_str[spv::DimSubpassData + 1] = {
    "1d",
    "2d",
    "3d",
    "cube",
    "rect",
    "buffer",
    "subpass_data"
};

// https://github.com/KhronosGroup/SPIRV-Cross/wiki/Reflection-API-user-guide
static void output_resource_info_json(sjson_context* jctx, sjson_node* jparent,
    const spirv_cross::Compiler& compiler,
    const spirv_cross::SmallVector<spirv_cross::Resource>& ress,
    resource_type res_type = RES_TYPE_REGULAR,
    bool flatten_ubo = false)
{

    auto resolve_variable_type = [](const spirv_cross::SPIRType& type) -> const char* {
        int count = static_cast<int>(std::size(k_variable_type_map));
        for (int i = 0; i < count; i++) {
            if (k_variable_type_map[i].base_type == type.basetype && k_variable_type_map[i].vec_size == type.vecsize && k_variable_type_map[i].columns == type.columns) {
                return k_variable_type_map[i].type_str;
            }
        }

        fprintf(stderr, "Unsupported type: %s, vecsize: %u, colums: %u\n", spirv_basetype_to_name(type.basetype), type.vecsize, type.columns);
        exit(-1);

        return "unknown";
    };

    auto fill_members = [&jctx, &compiler, &resolve_variable_type](sjson_node* jres, const spirv_cross::SPIRType& type) {
        sjson_node* jmembers = sjson_put_array(jctx, jres, "members");
        // members
        int member_idx = 0;
        for (auto& member_id : type.member_types) {
            sjson_node* jmember = sjson_mkobject(jctx);
            auto& member_type = compiler.get_type(member_id);

            sjson_put_string(
                jctx, jmember, "name",
                compiler.get_member_name(type.self, member_idx).c_str());
            sjson_put_string(jctx, jmember, "type",
                resolve_variable_type(member_type));
            sjson_put_int(jctx, jmember, "offset",
                compiler.type_struct_member_offset(type, member_idx));
            sjson_put_int(jctx, jmember, "size",
                (int)compiler.get_declared_struct_member_size(
                    type, member_idx));
            if (!member_type.array.empty()) {
                int arr_sz = 0;
                for (auto arr : member_type.array)
                    arr_sz += arr;
                sjson_put_int(jctx, jmember, "array", arr_sz);
            }

            sjson_append_element(jmembers, jmember);
            member_idx++;
        }
    };

    for (auto& res : ress) {
        sjson_node* jres = sjson_mkobject(jctx);
        auto& type = compiler.get_type(res.type_id);
        if (res_type == RES_TYPE_SSBO && compiler.buffer_is_hlsl_counter_buffer(res.id))
            continue;

        // If we don't have a name, use the fallback for the type instead of the variable
        // for SSBOs and UBOs since those are the only meaningful names to use externally.
        // Push constant blocks are still accessed by name and not block name, even though they are technically Blocks.
        bool is_push_constant = compiler.get_storage_class(res.id) == spv::StorageClassPushConstant;
        bool is_block = compiler.get_decoration_bitset(type.self).get(spv::DecorationBlock) || compiler.get_decoration_bitset(type.self).get(spv::DecorationBufferBlock);
        bool is_sized_block = is_block && (compiler.get_storage_class(res.id) == spv::StorageClassUniform || compiler.get_storage_class(res.id) == spv::StorageClassUniformConstant);
        uint32_t fallback_id = !is_push_constant && is_block ? (uint32_t)res.base_type_id : (uint32_t)res.id;

        uint32_t block_size = 0;
        uint32_t runtime_array_stride = 0;
        if (is_sized_block) {
            auto& base_type = compiler.get_type(res.base_type_id);
            block_size = uint32_t(compiler.get_declared_struct_size(base_type));
            runtime_array_stride = uint32_t(compiler.get_declared_struct_size_runtime_array(base_type, 1) - compiler.get_declared_struct_size_runtime_array(base_type, 0));
        }

        spirv_cross::Bitset mask;
        if (res_type == RES_TYPE_SSBO)
            mask = compiler.get_buffer_block_flags(res.id);
        else
            mask = compiler.get_decoration_bitset(res.id);

        sjson_put_int(jctx, jres, "id", res.id);
        sjson_put_string(jctx, jres, "name",
            !res.name.empty() ? res.name.c_str() : compiler.get_fallback_name(fallback_id).c_str());

        if (!type.array.empty()) {
            int arr_sz = 0;
            for (auto arr : type.array)
                arr_sz += arr;
            sjson_put_int(jctx, jres, "array", arr_sz);
        }

        int loc = -1;
        if (mask.get(spv::DecorationLocation)) {
            loc = compiler.get_decoration(res.id, spv::DecorationLocation);
            sjson_put_int(jctx, jres, "location", loc);
        }

        if (mask.get(spv::DecorationDescriptorSet)) {
            sjson_put_int(jctx, jres, "set",
                compiler.get_decoration(res.id, spv::DecorationDescriptorSet));
        }
        if (mask.get(spv::DecorationBinding)) {
            sjson_put_int(jctx, jres, "binding",
                compiler.get_decoration(res.id, spv::DecorationBinding));
        }
        if (mask.get(spv::DecorationInputAttachmentIndex)) {
            sjson_put_int(jctx, jres, "attachment",
                compiler.get_decoration(res.id, spv::DecorationInputAttachmentIndex));
        }
        if (mask.get(spv::DecorationNonReadable))
            sjson_put_bool(jctx, jres, "writeonly", true);
        if (mask.get(spv::DecorationNonWritable))
            sjson_put_bool(jctx, jres, "readonly", true);
        if (is_sized_block) {
            sjson_put_int(jctx, jres, "block_size", block_size);
            if (runtime_array_stride)
                sjson_put_int(jctx, jres, "unsized_array_stride", runtime_array_stride);
        }

        if (res_type == RES_TYPE_VERTEX_INPUT && loc != -1) {
            sjson_put_string(jctx, jres, "semantic", k_attrib_sem_names[loc]);
            sjson_put_int(jctx, jres, "semantic_index", k_attrib_sem_indices[loc]);
        }

        uint32_t counter_id = 0;
        if (res_type == RES_TYPE_SSBO && compiler.buffer_get_hlsl_counter_buffer(res.id, counter_id))
            sjson_put_int(jctx, jres, "hlsl_counter_buffer_id", counter_id);

        if (res_type == RES_TYPE_UNIFORM_BUFFER) {
            if (flatten_ubo) {
                sjson_put_string(jctx, jres, "type", "float4");
                sjson_put_int(jctx, jres, "array",
                    sx_max((int)block_size, 16) / 16);
            }

            fill_members(jres, type);
        } else if (is_push_constant && is_block) {
            fill_members(jres, type);
        } else if (res_type == RES_TYPE_TEXTURE) {
            sjson_put_string(jctx, jres, "dimension", k_texture_dim_str[type.image.dim]);
            sjson_put_string(jctx, jres, "format", k_texture_format_str[type.image.format]);
            if (type.image.ms)
                sjson_put_bool(jctx, jres, "multisample", true);
            if (type.image.arrayed)
                sjson_put_bool(jctx, jres, "array", true);
            if (mask.get(spv::DecorationSamplerSlot)) {
                sjson_put_int(jctx, jres, "sampler_slot",
                    compiler.get_decoration(res.id, spv::DecorationSamplerSlot));
            }
        } else if (res_type == RES_TYPE_VERTEX_INPUT) {
            sjson_put_string(jctx, jres, "type", resolve_variable_type(type));
        }

        sjson_append_element(jparent, jres);
    }
}

static void output_reflection_json(const cmd_args& args, const spirv_cross::Compiler& compiler,
    const spirv_cross::ShaderResources& ress,
    const char* filename,
    EShLanguage stage, std::string* reflect_json, bool pretty = false)
{
    sjson_context* jctx = sjson_create_context(0, 0, (void*)g_alloc);
    sx_assert(jctx);

    sjson_node* jroot = sjson_mkobject(jctx);
    sjson_put_string(jctx, jroot, "language", k_shader_types[args.lang]);
    sjson_put_int(jctx, jroot, "profile_version", args.profile_ver);
    if (args.compile_bin)
        sjson_put_bool(jctx, jroot, "bytecode", true);
    if (args.debug_info)
        sjson_put_bool(jctx, jroot, "debug_info", true);
    if (args.flatten_ubo)
        sjson_put_bool(jctx, jroot, "flatten_ubo", true);
    if (args.inline_ubo_members)
        sjson_put_bool(jctx, jroot, "inline_ubo", true);

    sjson_node* jshader = sjson_put_obj(jctx, jroot, get_stage_name(stage));
    sjson_put_string(jctx, jshader, "file", filename);

    if (!ress.subpass_inputs.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "subpass_inputs"), compiler, ress.subpass_inputs);
    if (!ress.stage_inputs.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "inputs"), compiler, ress.stage_inputs,
            (stage == EShLangVertex) ? RES_TYPE_VERTEX_INPUT : RES_TYPE_REGULAR);
    if (!ress.stage_outputs.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "outputs"), compiler, ress.stage_outputs);
    if (!ress.sampled_images.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "textures"), compiler, ress.sampled_images, RES_TYPE_TEXTURE);
    if (!ress.separate_images.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "sep_images"), compiler, ress.separate_images);
    if (!ress.separate_samplers.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "sep_samplers"), compiler, ress.separate_samplers);
    if (!ress.storage_images.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "storage_images"), compiler, ress.storage_images, RES_TYPE_TEXTURE);
    if (!ress.storage_buffers.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "storage_buffers"), compiler, ress.storage_buffers, RES_TYPE_SSBO);
    if (!ress.uniform_buffers.empty()) {
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "uniform_buffers"), compiler, ress.uniform_buffers,
            RES_TYPE_UNIFORM_BUFFER, args.flatten_ubo ? true : false);
    }
    if (!ress.push_constant_buffers.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "push_cbs"), compiler, ress.push_constant_buffers);
    if (!ress.atomic_counters.empty())
        output_resource_info_json(jctx, sjson_put_array(jctx, jshader, "counters"), compiler, ress.atomic_counters);

    char* json_str;
    if (!pretty)
        json_str = sjson_encode(jctx, jroot);
    else
        json_str = sjson_stringify(jctx, jroot, "  ");
    *reflect_json = json_str;
    sjson_free_string(jctx, json_str);
    sjson_destroy_context(jctx);
}

static int compute_array_size(const spirv_cross::SPIRType& type)
{
    if (type.array.empty())
        return 1;
    else {
        int arr_sz = 0;
        for (auto arr : type.array)
            arr_sz += static_cast<int>(arr);
        return arr_sz;
    }
}

static void output_resource_info_bin(sx_mem_writer* w, uint32_t* num_values,
    const spirv_cross::Compiler& compiler,
    const spirv_cross::SmallVector<spirv_cross::Resource>& ress,
    resource_type res_type = RES_TYPE_REGULAR,
    bool flatten_ubo = false)
{
    auto resolve_variable_type = [](const spirv_cross::SPIRType& type) -> uint16_t {
        int count = static_cast<int>(std::size(k_variable_type_map));
        for (int i = 0; i < count; i++) {
            if (k_variable_type_map[i].base_type == type.basetype && k_variable_type_map[i].vec_size == type.vecsize && k_variable_type_map[i].columns == type.columns) {
                return k_variable_type_map[i].sc_type;
            }
        }

        fprintf(stderr, "Unsupported type: %s, vecsize: %u, colums: %u\n", spirv_basetype_to_name(type.basetype), type.vecsize, type.columns);
        exit(-1);

        return 0;
    };

    const bool is_msl = typeid(compiler) == typeid(spirv_cross::CompilerMSL);

    auto resolve_variable_align = [=](const spirv_cross::SPIRType& type) -> int {
        switch (type.basetype) {
        case spirv_cross::SPIRType::Int:
        case spirv_cross::SPIRType::Float:
            return 4 * type.vecsize;
        case spirv_cross::SPIRType::Half:
            return 2 * type.vecsize;
        default:
            return 1;
        }
    };

    for (auto& res : ress) {
        auto& type = compiler.get_type(res.type_id);
        if (res_type == RES_TYPE_SSBO && compiler.buffer_is_hlsl_counter_buffer(res.id))
            continue;

        // If we don't have a name, use the fallback for the type instead of the variable
        // for SSBOs and UBOs since those are the only meaningful names to use externally.
        // Push constant blocks are still accessed by name and not block name, even though they are technically Blocks.
        bool is_push_constant = compiler.get_storage_class(res.id) == spv::StorageClassPushConstant;
        bool is_block = compiler.get_decoration_bitset(type.self).get(spv::DecorationBlock) || compiler.get_decoration_bitset(type.self).get(spv::DecorationBufferBlock);
        bool is_sized_block = is_block && (compiler.get_storage_class(res.id) == spv::StorageClassUniform || compiler.get_storage_class(res.id) == spv::StorageClassUniformConstant);
        uint32_t fallback_id = !is_push_constant && is_block ? (uint32_t)res.base_type_id : (uint32_t)res.id;

        uint32_t block_size = 0;
        uint32_t runtime_array_stride = 0;
        if (is_sized_block) {
            auto& base_type = compiler.get_type(res.base_type_id);
            block_size = uint32_t(compiler.get_declared_struct_size(base_type));
            runtime_array_stride = uint32_t(compiler.get_declared_struct_size_runtime_array(base_type, 1) - compiler.get_declared_struct_size_runtime_array(base_type, 0));
        }

        spirv_cross::Bitset mask;
        if (res_type == RES_TYPE_SSBO)
            mask = compiler.get_buffer_block_flags(res.id);
        else
            mask = compiler.get_decoration_bitset(res.id);

        auto&& name = !res.name.empty() ? res.name : compiler.get_fallback_name(fallback_id);

        int array_size = compute_array_size(type);

        int loc = -1;
        int binding = -1;
        if (mask.get(spv::DecorationLocation)) {
            loc = compiler.get_decoration(res.id, spv::DecorationLocation);
        }

        if (mask.get(spv::DecorationBinding)) {
            binding = compiler.get_decoration(res.id, spv::DecorationBinding);
        }

        // Some extra
        if (res_type == RES_TYPE_UNIFORM_BUFFER) {
            sc_refl_ub u = { 0 };

            sx_strcpy(u.name, sizeof(u.name), name.c_str());
            u.binding = binding;
            u.size_bytes = block_size;
            if (flatten_ubo) {
                u.array_size = (uint16_t)sx_max((int)block_size, 16) / 16;
                u.num_members = 0;
            } else {
                u.array_size = array_size;
                u.num_members = static_cast<uint16_t>(type.member_types.size());
            }

            auto block_size_offset = w->pos + offsetof(sc_refl_ub, size_bytes);
            sx_mem_write_var(w, u);

            if (u.num_members > 0) {
                struct align_data_st {
                    int offset = -1;
                    int size_bytes = -1;
                    int align = 1;
                } align_data;
                for (int member_idx = 0; member_idx < type.member_types.size(); ++member_idx) {
                    auto& member_type = compiler.get_type(type.member_types[member_idx]);
                    sc_refl_ub_member um = { 0 };
                    sx_strcpy(um.name, sizeof(um.name), compiler.get_member_name(type.self, member_idx).c_str());
                    um.offset = compiler.type_struct_member_offset(type, member_idx);
                    um.size_bytes = static_cast<uint32_t>(compiler.get_declared_struct_member_size(type, member_idx));
                    um.array_size = static_cast<uint16_t>(compute_array_size(member_type));
                    um.var_type = resolve_variable_type(member_type);

                    if (is_msl) {
                        auto align = resolve_variable_align(member_type);
                        if (align_data.offset < um.offset) {
                            align_data.offset = um.offset;
                            align_data.size_bytes = um.size_bytes;
                        }
                        if (align_data.align < align) {
                            align_data.align = align;
                        }
                    }
                    sx_mem_write_var(w, um);
                }

                if (is_msl && align_data.offset >= 0) {
#define GLSLCC_ALIGN_VALUE(d, a) (((d) + ((a) - 1)) & ~((a) - 1))
                    int& stored_block_size = *(int*)(w->data + block_size_offset);
                    int aligned_block_size = GLSLCC_ALIGN_VALUE((align_data.offset + align_data.size_bytes), align_data.align);
                    if (stored_block_size != aligned_block_size) {
                        memcpy(&stored_block_size, &aligned_block_size, 4);
                    }
                }
            }
        } else if (res_type == RES_TYPE_TEXTURE) {
            sc_refl_texture t = { 0 };

            sx_strcpy(t.name, sizeof(t.name), name.c_str());
            t.binding = binding;
            t.image_dim = type.image.dim;
            t.multisample = type.image.ms ? 1 : 0;
            t.arrayed = type.image.arrayed ? 1 : 0;

            int arr_sz = 0;
            for (auto arr : type.array)
                arr_sz += arr;
            t.count = arr_sz;

            if (compiler.has_decoration(res.id, spv::DecorationSamplerSlot))
                t.sampler_slot = compiler.get_decoration(res.id, spv::DecorationSamplerSlot);
            else
                t.sampler_slot = 0;
            sx_mem_write_var(w, t);
        } else if (res_type == RES_TYPE_VERTEX_INPUT) {
            sc_refl_input i = { 0 };

            sx_strcpy(i.name, sizeof(i.name), name.c_str());
            i.location = loc;
            sx_strcpy(i.semantic, sizeof(i.semantic), k_attrib_sem_names[loc]);
            i.semantic_index = k_attrib_sem_indices[loc];
            i.var_type = resolve_variable_type(type);
            sx_mem_write_var(w, i);
        } else if (res_type == RES_TYPE_SSBO) {
            sc_refl_buffer b = { 0 };
            sx_strcpy(b.name, sizeof(b.name), name.c_str());
            b.binding = binding;
            b.size_bytes = block_size;
            if (runtime_array_stride) {
                b.array_stride = runtime_array_stride;
            }
            sx_mem_write_var(w, b);
        }

        ++(*num_values);
    }
}

static int output_reflection_bin(const cmd_args& args, const spirv_cross::Compiler& compiler,
    const spirv_cross::ShaderResources& ress,
    const char* filename,
    EShLanguage stage, sx_mem_block** refl_mem)
{
    sx_mem_writer w;
    sx_mem_init_writer(&w, g_alloc, 2048);

    sc_chunk_refl refl;
    sx_memset(&refl, 0x0, sizeof(refl));
    sx_os_path_basename(refl.name, sizeof(refl.name), filename);
    refl.flatten_ubo = args.flatten_ubo;
    refl.debug_info = args.debug_info;
    sx_mem_write_var(&w, refl);

    if (!ress.stage_inputs.empty() && stage == EShLangVertex) {
        output_resource_info_bin(&w, &refl.num_inputs, compiler, ress.stage_inputs, RES_TYPE_VERTEX_INPUT);
    }

    if (!ress.uniform_buffers.empty()) {
        output_resource_info_bin(&w, &refl.num_uniform_buffers, compiler, ress.uniform_buffers,
            RES_TYPE_UNIFORM_BUFFER, !!args.flatten_ubo);
    }

    if (!ress.sampled_images.empty()) {
        output_resource_info_bin(&w, &refl.num_textures, compiler, ress.sampled_images, RES_TYPE_TEXTURE);
    }

    // compute shader reflection data
    if (stage == EShLangCompute) {
        if (!ress.storage_images.empty()) {
            output_resource_info_bin(&w, &refl.num_storage_images, compiler, ress.storage_images,
                RES_TYPE_TEXTURE);
        }

        if (!ress.storage_buffers.empty()) {
            output_resource_info_bin(&w, &refl.num_storage_buffers, compiler, ress.storage_buffers,
                RES_TYPE_SSBO);
        }
    }

    sx_mem_seekw(&w, 0, SX_WHENCE_BEGIN);
    sx_mem_write_var(&w, refl);

    *refl_mem = w.mem;

    return static_cast<int>(sx_mem_seekw(&w, 0, SX_WHENCE_END));
}

// if binary_size > 0, then we assume the data is binary
static bool write_file(const std::string& filepath, const char* data, const std::string& cvar,
    bool append = false, int binary_size = -1)
{
    sx_file_writer writer;
    if (!sx_file_open_writer(&writer, filepath.c_str(), append ? SX_FILE_OPEN_APPEND : 0))
        return false;

    if (!cvar.empty()) {
        const int items_per_line = 8;

        // .C file
        if (!append) {
            // file header
            char header[128];
            sx_snprintf(header, sizeof(header),
                "// This file is automatically created by glslcc v%d.%d.%d\n"
                "// http://www.github.com/septag/glslcc\n"
                "// \n"
                "#pragma once\n\n",
                AXSLCC_VERSION_MAJOR, AXSLCC_VERSION_MINOR, AXSLCC_VERSION_REVISION);
            sx_file_write_text(&writer, header);
        }

        char var[128];
        int len;
        int char_offset = 0;
        char hex[32];

        if (binary_size > 0)
            len = binary_size;
        else
            len = sx_strlen(data) + 1; // include the '\0' at the end to null-terminate the string

        // align data to uint32_t (4)
        const uint32_t* aligned_data = (const uint32_t*)data;
        int aligned_len = sx_align_mask(len, 3);
        if (aligned_len > len) {
            uint32_t* tmp = (uint32_t*)sx_malloc(g_alloc, aligned_len);
            if (!tmp) {
                sx_out_of_memory();
                return false;
            }
            sx_memcpy(tmp, data, len);
            sx_memset((uint8_t*)tmp + len, 0x0, aligned_len - len);
            aligned_data = tmp;
        }
        const char* aligned_ptr = (const char*)aligned_data;

        sx_snprintf(var, sizeof(var), "static const unsigned int %s_size = %d;\n", cvar.c_str(), len);
        sx_file_write_text(&writer, var);
        sx_snprintf(var, sizeof(var), "static const unsigned int %s_data[%d/4] = {\n    ", cvar.c_str(), aligned_len);
        sx_file_write_text(&writer, var);

        sx_assert(aligned_len % sizeof(uint32_t) == 0);
        int uint_count = aligned_len / 4;
        for (int i = 0; i < uint_count; i++) {
            if (i != uint_count - 1) {
                sx_snprintf(hex, sizeof(hex), "0x%08x, ", *aligned_data);
            } else {
                sx_snprintf(hex, sizeof(hex), "0x%08x };\n", *aligned_data);
            }
            sx_file_write_text(&writer, hex);

            ++char_offset;
            if (char_offset == items_per_line) {
                sx_file_write_text(&writer, "\n    ");
                char_offset = 0;
            }

            ++aligned_data;
        }

        sx_file_write_text(&writer, "\n");

        if (aligned_ptr != data) {
            sx_free(g_alloc, const_cast<char*>(aligned_ptr));
        }
    } else {
        if (binary_size > 0)
            sx_file_write(&writer, data, binary_size);
        else
            sx_file_write(&writer, data, sx_strlen(data));
    }

    sx_file_close_writer(&writer);
    return true;
}

static int cross_compile(const cmd_args& args, const glslang::TIntermediate& ir, std::vector<uint32_t>& spirv,
    const char* filename, EShLanguage stage, int file_index)
{
    sx_assert(!spirv.empty());
    // Using SPIRV-cross

    try {
        std::unique_ptr<spirv_cross::CompilerGLSL> compiler;
        // Use spirv-cross to convert to other types of shader
        if (args.lang == SHADER_LANG_ESSL || args.lang == SHADER_LANG_GLSL) {
            compiler = std::unique_ptr<spirv_cross::CompilerGLSL>(new spirv_cross::CompilerGLSL(spirv));
        } else if (args.lang == SHADER_LANG_MSL) {
            compiler = std::unique_ptr<spirv_cross::CompilerMSL>(new spirv_cross::CompilerMSL(spirv));
        } else if (args.lang == SHADER_LANG_HLSL) {
            compiler = std::unique_ptr<spirv_cross::CompilerHLSL>(new spirv_cross::CompilerHLSL(spirv));
        } else if (args.lang == SHADER_LANG_SPIRV) {
            compiler = std::unique_ptr<spirv_cross::CompilerGLSL>(new spirv_cross::CompilerGLSL(spirv));
        } else {
            sx_assert(0 && "Language not implemented");
        }

        spirv_cross::ShaderResources ress = compiler->get_shader_resources();

        spirv_cross::CompilerGLSL::Options opts = compiler->get_common_options();
        opts.flatten_multidimensional_arrays = true;
        if (args.fixup_clipspace)
            opts.vertex.fixup_clipspace = !!args.fixup_clipspace;
        if (args.lang == SHADER_LANG_ESSL) {
            opts.es = true;
            opts.version = args.profile_ver;
        } else if (args.lang == SHADER_LANG_GLSL) {
            opts.enable_420pack_extension = false;
            opts.es = false;
            opts.version = args.profile_ver;
        } else if (args.lang == SHADER_LANG_HLSL) {
            spirv_cross::CompilerHLSL* hlsl = (spirv_cross::CompilerHLSL*)compiler.get();
            spirv_cross::CompilerHLSL::Options hlsl_opts = hlsl->get_hlsl_options();

            hlsl_opts.shader_model = args.profile_ver;
            hlsl_opts.point_size_compat = true;
            hlsl_opts.point_coord_compat = true;
            // @v1.10.0
            hlsl_opts.flatten_matrix_vertex_input_semantics = true;

            hlsl->set_hlsl_options(hlsl_opts);

            uint32_t new_builtin = hlsl->remap_num_workgroups_builtin();
            if (new_builtin) {
                hlsl->set_decoration(new_builtin, spv::DecorationDescriptorSet, 0);
                hlsl->set_decoration(new_builtin, spv::DecorationBinding, 0);
            }

            // set hlsl vertex attribute remap
            for (int i = 0; i < VERTEX_ATTRIB_COUNT; ++i) {
                spirv_cross::HLSLVertexAttributeRemap remap = { (uint32_t)i, k_attrib_names[i] };
                hlsl->add_vertex_attribute_remap(remap);
            }

            // since axslcc-3.1.1
            for (auto i = 0; i < std::size(k_builtin_sampler_states); ++i)
            {
                hlsl->add_hlsl_sampler_state(i, k_builtin_sampler_states[i]);
            }
        } else if (args.lang == SHADER_LANG_MSL) {
            spirv_cross::CompilerMSL* msl = (spirv_cross::CompilerMSL*)compiler.get();
            spirv_cross::CompilerMSL::Options msl_opts = msl->get_msl_options();
            msl_opts.enable_decoration_binding = true;
            // msl_opts.enable_base_index_zero = true;
            msl_opts.msl_version = args.profile_ver;
            msl_opts.ios_support_base_vertex_instance = true; // ios-9.0+
            msl_opts.platform = args.msl_ios ? spirv_cross::CompilerMSL::Options::Platform::iOS : spirv_cross::CompilerMSL::Options::Platform::macOS;
            msl->set_msl_options(msl_opts);
        }

        // Flatten ubos
        if (args.flatten_ubo && args.profile_ver < 300) {
            // requires all member of uniform have same base type
            for (auto& ubo : ress.uniform_buffers)
                compiler->flatten_buffer_block(ubo.id);
            for (auto& ubo : ress.push_constant_buffers)
                compiler->flatten_buffer_block(ubo.id);
        }

        if (args.inline_ubo_members && args.profile_ver < 300) {
            opts.inline_ubo_members = true;
        }

        // Reset vertex input locations for MSL
        if (args.msl_reset_vlocs && args.lang == SHADER_LANG_MSL && stage == EShLangVertex) {
            int location = 0;
            spirv_cross::CompilerMSL* msl = (spirv_cross::CompilerMSL*)compiler.get();
            for (int i = 0; i < ress.stage_inputs.size(); i++) {
                spirv_cross::Resource& res = ress.stage_inputs[i];
                spirv_cross::Bitset mask = compiler->get_decoration_bitset(res.id);
                auto& type = compiler->get_type(res.type_id);
                compiler->set_decoration(res.id, spv::DecorationLocation, location);
                location += type.columns;
            }
        }

        // @v1.10.0 drop GLES-2.0 support
        // opts.emit_expanded_uniforms = true;
        compiler->set_common_options(opts);

        std::string code;
        std::vector<uint32_t> clean_spirv;
        if (args.lang == SHADER_LANG_SPIRV) {
            spirv.clear();
            glslang::SpvOptions spv_opts {
                .generateDebugInfo = !!args.debug_info,
                .stripDebugInfo = !args.debug_info,
                .disableOptimizer = !args.optimize,
                .optimizeSize = !!args.optimize,
                .validate = true
            };
            spv::SpvBuildLogger logger;
            glslang::GlslangToSpv(ir, spirv, &logger, &spv_opts);
            if (!logger.getAllMessages().empty())
                puts(logger.getAllMessages().c_str());
        } else {
            code = compiler->compile();
        }

        // Output code
        if (sc_file_handle) {
            uint32_t sstage;
            switch (stage) {
            case EShLangVertex:
                sstage = SC_STAGE_VERTEX;
                break;
            case EShLangFragment:
                sstage = SC_STAGE_FRAGMENT;
                break;
            case EShLangCompute:
                sstage = SC_STAGE_COMPUTE;
                break;
            default:
                sstage = 0;
                break;
            }

            if (args.compile_bin && args.lang == SHADER_LANG_HLSL) {
                sx_mem_block* mem = d3d_compile_binary(code.c_str(), args.out_filepath, args.profile_ver,
                    stage, args.debug_info);
                if (!mem) {
                    printf("HLSL bytecode compilation of '%s' failed\n", args.out_filepath);
                    return -1;
                }

                sc_add_stage_code_bin(sc_file_handle, sstage, mem->data, mem->size);
                sx_mem_destroy_block(mem);
            } else {
                if (args.lang != SHADER_LANG_SPIRV) {
                    sc_add_stage_code(sc_file_handle, sstage, code.c_str());
                } else {
                    sc_add_stage_code_bin(sc_file_handle, sstage, spirv.data(), static_cast<int>(spirv.size() * sizeof(uint32_t)));
                }
            }

            if (args.reflect) {
                sx_mem_block* mem = nullptr;
                auto refl_bytes = output_reflection_bin(args, *compiler, ress, args.out_filepath, stage, &mem);
                sc_add_stage_reflect(sc_file_handle, sstage, mem->data, refl_bytes);
                sx_mem_destroy_block(mem);
            }
        } else {
            std::string cvar_code = args.cvar ? args.cvar : "";
            std::string filepath;
            if (!cvar_code.empty()) {
                cvar_code += "_";
                cvar_code += get_stage_name(stage);
                filepath = args.out_filepath;
            } else {
                if (!args.no_suffix) {
                    char ext[32];
                    char basename[512];
                    sx_os_path_splitext(ext, sizeof(ext), basename, sizeof(basename), args.out_filepath);
                    filepath = std::string(basename) + std::string("_") + std::string(get_stage_name(stage)) + std::string(ext);
                } else {
                    filepath = args.out_filepath;
                }
            }
            bool append = !cvar_code.empty() && (file_index > 0);

            // Check if we have to compile byte-code or output the source only
            if (args.compile_bin && args.lang == SHADER_LANG_HLSL) {
                sx_mem_block* mem = d3d_compile_binary(code.c_str(), filepath.c_str(), args.profile_ver,
                    stage, args.debug_info);
                if (!mem) {
                    printf("HLSL bytecode compilation of '%s' failed\n", filepath.c_str());
                    return -1;
                }

                if (!write_file(filepath, (const char*)mem->data, cvar_code, append, mem->size)) {
                    printf("Writing to '%s' failed\n", filepath.c_str());
                    return -1;
                }

                sx_mem_destroy_block(mem);
            } else {
                // output code file
                auto ok = (args.lang != SHADER_LANG_SPIRV) ? write_file(filepath, code.c_str(), cvar_code, append) : write_file(filepath, reinterpret_cast<const char*>(spirv.data()), cvar_code, append, static_cast<int>(spirv.size() * sizeof(uint32_t)));
                if (!ok) {
                    printf("Writing to '%s' failed\n", filepath.c_str());
                    return -1;
                }
            }

            if (args.reflect) {
                // output json reflection file
                // if --reflect is defined, we just output to that file
                // if --reflect is not defined, check cvar (.C file), and if set, output to the same file (out_filepath)
                // if --reflect is not defined and there is no cvar, output to out_filepath.json
                std::string json_str;
                output_reflection_json(args, *compiler, ress, filepath.c_str(), stage, &json_str, cvar_code.empty());

                std::string reflect_filepath;
                if (args.reflect_filepath) {
                    reflect_filepath = args.reflect_filepath;
                } else if (!cvar_code.empty()) {
                    reflect_filepath = filepath;
                    append = true;
                } else {
                    reflect_filepath = filepath;
                    reflect_filepath += ".json";
                }

                std::string cvar_refl = !cvar_code.empty() ? (cvar_code + "_refl") : "";
                if (!write_file(reflect_filepath.c_str(), json_str.c_str(), cvar_refl.c_str(), append)) {
                    printf("Writing to '%s' failed", reflect_filepath.c_str());
                    return -1;
                }
            }
        }

        if (!args.silent)
            puts(filename); // SUCCESS
        return 0;
    } catch (const std::exception& e) {
        printf("SPIRV-cross: %s\n", e.what());
        return -1;
    }
}

struct compile_file_desc {
    EShLanguage stage;
    const char* filename;
    uint32_t offset;
    uint32_t size;
};

#define compile_files_ret(_code)   \
    destroy_shaders(shaders);      \
    sx_array_free(g_alloc, files); \
    prog->~TProgram();             \
    sx_free(g_alloc, prog);        \
    glslang::FinalizeProcess();    \
    return _code;

struct output_parse_result {
    std::string file;
    std::string err;
    int line;
};

static bool parse_output_log_detect_line(const char** str)
{
    const char* err_header = "ERROR: ";
    const char* warn_header = "WARNING: ";

    if (sx_strstr(*str, err_header) == *str) {
        *str = *str + sx_strlen(err_header);
        return true;
    } else if (sx_strstr(*str, warn_header) == *str) {
        *str = *str + sx_strlen(warn_header);
        return true;
    } else {
        return false;
    }
}

static bool parse_output_log(const char* str, std::vector<output_parse_result>* r)
{
    while (parse_output_log_detect_line(&str)) {
        const char* divider = sx_strchar(str, ':');
        if (!divider)
            return r->size() > 0;
        output_parse_result lr;

        if (SX_PLATFORM_WINDOWS && divider == str + 1)
            divider = sx_strchar(divider + 1, ':');
        // sx_strncpy(file, file_sz, str, (intptr_t)(divider - str));
        lr.file.assign(str, divider);
        lr.line = sx_toint(divider + 1);
        const char* next_divider = sx_strchar(divider + 1, ':');
        sx_assert(next_divider);
        // sx_strcpy(desc, desc_sz, next_divider + 1);
        const char* line_sep = sx_strchar(next_divider + 1, '\n');
        std::string err;
        if (line_sep) {
            lr.err.assign(next_divider + 1, line_sep);
            str = line_sep + 1;
        } else {
            lr.err.assign(next_divider + 1);
            str += err.length();
        }

        r->push_back(lr);
    }
    return true;
}

static void output_error(const char* err_str, const cmd_args& args, const char* filename, int start_line = 0)
{
    if (err_str && err_str[0]) {
        std::vector<output_parse_result> lines;
        parse_output_log(err_str, &lines);
        if (args.err_format == OUTPUT_ERRORFORMAT_GLSLANG) {
            fprintf(stdout, "%s\n", filename);
            for (std::vector<output_parse_result>::iterator il = lines.begin();
                il != lines.end(); ++il) {
                fprintf(stdout, "ERROR: 0:%d:%s\n", il->line + start_line, il->err.c_str());
            }
        } else if (args.err_format == OUTPUT_ERRORFORMAT_MSVC) {
            for (std::vector<output_parse_result>::iterator il = lines.begin();
                il != lines.end(); ++il) {
                char fullpath[256];
                sx_os_path_abspath(fullpath, sizeof(fullpath), il->file.c_str());
                fprintf(stderr, "%s(%d,0): error:%s\n", fullpath, il->line + start_line, il->err.c_str());
            }
        } else if (args.err_format == OUTPUT_ERRORFORMAT_GCC) {
            for (std::vector<output_parse_result>::iterator il = lines.begin();
                il != lines.end(); ++il) {
                char fullpath[256];
                sx_os_path_abspath(fullpath, sizeof(fullpath), il->file.c_str());
                fprintf(stderr, "%s:%d:0: error:%s\n", fullpath, il->line + start_line, il->err.c_str());
            }
        }
    }
}

static int compile_files(cmd_args& args, const TBuiltInResource& limits_conf)
{
    auto destroy_shaders = [](glslang::TShader**& shaders) {
        for (int i = 0; i < sx_array_count(shaders); i++) {
            if (shaders[i]) {
                shaders[i]->~TShader();
                sx_free(g_alloc, shaders[i]);
            }
        }
        sx_array_free(g_alloc, shaders);
    };

    auto find_end_block = [](const char* text) -> const char* {
        const char* end_block = sx_strstr(text, "//@end");
        if (end_block) {
            if (*(end_block - 1) == '\n') {
                if (!end_block[6] || sx_isspace(end_block[6])) {
                    return end_block;
                }
            }
            return nullptr;
        } else {
            return nullptr;
        }
    };

    auto calculate_start_line = [](const char* source, int end_offset) -> int {
        int count = 0;
        const char* start = source;
        while (1) {
            source = sx_strchar(source, '\n');
            if (source && uintptr_t(source - start) < end_offset) {
                count++;
                ++source;
            } else {
                break;
            }
        }
        return count;
    };

    glslang::InitializeProcess();

    // Gather files for compilation
    compile_file_desc* files = nullptr;

    if ((args.vs_filepath || args.fs_filepath) && args.vs_filepath == args.fs_filepath) {
        char ext[32];
        sx_os_path_ext(ext, sizeof(ext), args.vs_filepath);
        sx_assert(sx_strequalnocase(ext, ".glsl"));

        // open the file and check for special tags
        sx_mem_block* mem = sx_file_load_text(g_alloc, args.vs_filepath);
        if (!mem) {
            printf("opening file '%s' failed\n", args.vs_filepath);
            return -1;
        }

        const char* text = (const char*)mem->data;
        text = sx_skip_whitespace(text);

        while (*text) {
            if (sx_strnequal(text, "//@begin_", 9)) {
                text += 9;
                if (sx_strnequal(text, "vert", 4) && (text[4] == '\n' || (text[4] == '\r' && text[5] == '\n'))) {
                    text += (text[4] == '\r' && text[5] == '\n') ? 6 : 5;
                    const char* end_block = find_end_block(text);
                    if (!end_block) {
                        printf("no matching //@end found with //@begin: %s\n", args.vs_filepath);
                        return -1;
                    }

                    compile_file_desc d = {
                        EShLangVertex,
                        args.vs_filepath,
                        static_cast<uint32_t>(text - (const char*)mem->data),
                        static_cast<uint32_t>(end_block - text)
                    };
                    sx_array_push(g_alloc, files, d);

                    text = end_block + 6;
                } else if (sx_strnequal(text, "frag", 4) && (text[4] == '\n' || (text[4] == '\r' && text[5] == '\n'))) {
                    text += (text[4] == '\r' && text[5] == '\n') ? 6 : 5;
                    const char* end_block = find_end_block(text);
                    if (!end_block) {
                        printf("no matching //@end found with //@begin: %s\n", args.fs_filepath);
                        return -1;
                    }
                    compile_file_desc d = {
                        EShLangFragment,
                        args.vs_filepath,
                        static_cast<uint32_t>(text - (const char*)mem->data),
                        static_cast<uint32_t>(end_block - text)
                    };
                    sx_array_push(g_alloc, files, d);

                    text = end_block + 6;
                } else {
                    printf("invalid @begin tag in '%s'\n", args.vs_filepath);
                }
            }
            const char* next_text = sx_skip_whitespace(text);
            if (next_text == text) {
                break;
            }
            text = next_text;
        } // while(text)

        sx_mem_destroy_block(mem);

        // the offsets should not have any conflict with each other
        for (int i = 0; i < sx_array_count(files) - 1; i++) {
            if (files[i].offset + files[i].size > files[i + 1].offset) {
                printf("invalid @begin @end shader blocks: %s\n", args.vs_filepath);
                return -1;
            }
        }
    } else {
        if (args.vs_filepath) {
            compile_file_desc d = { EShLangVertex, args.vs_filepath };
            sx_array_push(g_alloc, files, d);
        }

        if (args.fs_filepath) {
            compile_file_desc d = { EShLangFragment, args.fs_filepath };
            sx_array_push(g_alloc, files, d);
        }

        if (args.cs_filepath) {
            compile_file_desc d = { EShLangCompute, args.cs_filepath };
            sx_array_push(g_alloc, files, d);
        }
    }

    glslang::TProgram* prog = new (sx_malloc(g_alloc, sizeof(glslang::TProgram))) glslang::TProgram();
    glslang::TShader** shaders = nullptr;

    // TODO: add more options for messaging options
    EShMessages messages = EShMsgDefault;
    constexpr int default_version = 100; // vulkan version

    // construct semantics mapping defines
    // to be used in layout(location = SEMANTIC) inside GLSL
    std::string semantics_def;
    for (int i = 0; i < VERTEX_ATTRIB_COUNT; i++) {
        char sem_line[128];
        sx_snprintf(sem_line, sizeof(sem_line), "#define %s %d\n", k_attrib_names[i], i);
        semantics_def += std::string(sem_line);
    }

    // Add SV_Target semantics for more HLSL compatibility
    for (int i = 0; i < 8; i++) {
        char sv_target_line[128];
        sx_snprintf(sv_target_line, sizeof(sv_target_line), "#define SV_Target%d %d\n", i, i);
        semantics_def += std::string(sv_target_line);
    }

    for (int i = 0; i < sx_array_count(files); i++) {
        // Always set include_directive in the preamble, because we may need to include shaders
        std::string def("#extension GL_GOOGLE_include_directive : require\n");
        def += semantics_def;

        if (args.lang == SHADER_LANG_ESSL && args.profile_ver == 200) {
            def += std::string("#define flat\n");
        }

        // Read target file
        sx_mem_block* mem = sx_file_load_bin(g_alloc, files[i].filename);
        if (!mem) {
            printf("opening file '%s' failed\n", files[i].filename);
            compile_files_ret(-1);
        }

        glslang::TShader* shader = new (sx_malloc(g_alloc, sizeof(glslang::TShader))) glslang::TShader(files[i].stage);
        sx_assert(shader);
        sx_array_push(g_alloc, shaders, shader);

        char* shader_str;
        int shader_len;
        int start_line = 0;
        if (files[i].size == 0) {
            shader_str = (char*)mem->data;
            shader_len = (int)mem->size;
        } else {
            shader_str = (char*)mem->data + files[i].offset;
            shader_len = (int)files[i].size;
            start_line = calculate_start_line((const char*)mem->data, files[i].offset);
        }
        shader->setStringsWithLengthsAndNames(&shader_str, &shader_len, &files[i].filename, 1);
        shader->setInvertY(args.invert_y ? true : false);
        shader->setEnvInput(glslang::EShSourceGlsl, files[i].stage, glslang::EShClientVulkan, default_version);
        shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);

        /*
         *  Vulkan 1.0 -> SPIR-V 1.0
         *  Vulkan 1.1 -> SPIR-V 1.3
         *  Vulkan 1.2 -> SPIR-V 1.5
         *  Vulkan 1.3 -> SPIR-V 1.6
         */
        glslang::EShTargetLanguageVersion spv_ver = glslang::EShTargetSpv_1_0;
        if (args.lang == SHADER_LANG_SPIRV) {
            switch (args.profile_ver) {
            case 110:
                spv_ver = glslang::EShTargetSpv_1_1;
                break;
            case 120:
                spv_ver = glslang::EShTargetSpv_1_2;
                break;
            case 130:
                spv_ver = glslang::EShTargetSpv_1_3;
                break;
            case 140:
                spv_ver = glslang::EShTargetSpv_1_4;
                break;
            case 150:
                spv_ver = glslang::EShTargetSpv_1_5;
                break;
            case 160:
                spv_ver = glslang::EShTargetSpv_1_6;
                break;
            }
        }
        shader->setEnvTarget(glslang::EShTargetSpv, spv_ver);

        // refer to: https://github.com/septag/glslcc/issues/18
        if (args.auto_map_bindings || args.automap)
            shader->setAutoMapBindings(true);

        if (args.auto_map_locations || args.automap)
            shader->setAutoMapLocations(true);

        add_defines(shader, args, def);

        std::string prep_str;
        Includer includer(args.list_includes);
        char cur_file_dir[512];
        sx_os_path_dirname(cur_file_dir, sizeof(cur_file_dir), files[i].filename);
        includer.addSystemDir(cur_file_dir);
        includer.addIncluder(args.includer);

        if (args.preprocess || args.list_includes) {
            if (shader->preprocess(&limits_conf, default_version, ENoProfile, false, false, messages, &prep_str, includer)) {
                if (args.preprocess) {
                    puts("-------------------");
                    printf("%s:\n", files[i].filename);
                    puts("-------------------");
                    puts(prep_str.c_str());
                    puts("");
                }
            } else {
                output_error(shader->getInfoLog(), args, files[i].filename, start_line);
                sx_mem_destroy_block(mem);
                compile_files_ret(-1);
            }
        } else {
            if (!shader->parse(&limits_conf, default_version, false, messages, includer)) {
                output_error(shader->getInfoLog(), args, files[i].filename, start_line);
                sx_mem_destroy_block(mem);
                compile_files_ret(-1);
            }

            if (!args.validate)
                prog->addShader(shader);
        }

        sx_mem_destroy_block(mem);
    } // foreach (file)

    // In preprocess mode, do not link, just exit
    if (args.preprocess || args.validate || args.list_includes) {
        compile_files_ret(0);
    }

    if (!prog->link(messages)) {
        puts("Link failed: ");
        fprintf(stderr, "%s\n", prog->getInfoLog());
        fprintf(stderr, "%s\n", prog->getInfoDebugLog());
        compile_files_ret(-1);
    }

    // Output and save SPIR-V for each shader
    std::vector<uint32_t> spirv;
    for (int i = 0; i < sx_array_count(files); i++) {
        spirv.clear();
        auto ir = prog->getIntermediate(files[i].stage);
        sx_assert(ir);

        glslang::SpvOptions spv_opts {
            .generateDebugInfo = true,
            .validate = false,
            .allowNonStandardDecorations = true
        };
        spv::SpvBuildLogger logger;
        glslang::GlslangToSpv(*ir, spirv, &logger, &spv_opts);
        if (!logger.getAllMessages().empty())
            puts(logger.getAllMessages().c_str());

        if (cross_compile(args, *ir, spirv, files[i].filename, files[i].stage, i) != 0) {
            compile_files_ret(-1);
        }
    }

    destroy_shaders(shaders);
    prog->~TProgram();
    sx_free(g_alloc, prog);

    glslang::FinalizeProcess();
    sx_array_free(g_alloc, files);

    return 0;
}

static void detect_input_file(cmd_args* args, const char* file)
{
    char ext[32];
    sx_os_path_ext(ext, sizeof(ext), file);
    if (sx_strequalnocase(ext, ".vert")) {
        args->vs_filepath = file;
    } else if (sx_strequalnocase(ext, ".frag")) {
        args->fs_filepath = file;
    } else if (sx_strequalnocase(ext, ".comp")) {
        args->cs_filepath = file;
    } else if (sx_strequalnocase(ext, ".glsl")) {
        // take a wild guess and put it in both args
        // later, we will look for special tags inside the file to extract the source for each stage
        args->vs_filepath = file;
        args->fs_filepath = file;
    }
}

int main(int argc, char* argv[])
{
    cmd_args args = {};
    args.lang = SHADER_LANG_COUNT;
    args.err_format = SX_PLATFORM_WINDOWS ? OUTPUT_ERRORFORMAT_MSVC : OUTPUT_ERRORFORMAT_GCC;

    int version = 0;
    int dump_conf = 0;

    const sx_cmdline_opt opts[] = {
        { "help", 'h', SX_CMDLINE_OPTYPE_NO_ARG, 0x0, 'h', "Print this help text", 0x0 },
        { "version", 'V', SX_CMDLINE_OPTYPE_FLAG_SET, &version, 1, "Print version", 0x0 },
        { "vert", 0x0, SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'v', "Vertex shader source file", "Filepath" },
        { "frag", 0x0, SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'f', "Fragment shader source file", "Filepath" },
        { "compute", 0x0, SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'c', "Compute shader source file", "Filepath" },
        { "output", 'o', SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'o', "Output file", "Filepath" },
        { "lang", 'l', SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'l', "Convert to shader language", "essl/msl/hlsl/glsl/spirv" },

        { "no-suffix", 'u', SX_CMDLINE_OPTYPE_FLAG_SET, &args.no_suffix, 1, "This option is for don't add _fs or _vs suffix in output file", 0x0 },
        { "defines", 'D', SX_CMDLINE_OPTYPE_OPTIONAL, 0x0, 'D', "Preprocessor definitions, seperated by comma or ';'", "Defines" },
        { "invert-y", 'Y', SX_CMDLINE_OPTYPE_FLAG_SET, &args.invert_y, 1, "Invert position.y in vertex shader", 0x0 },
        { "profile", 'p', SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'p', "Shader profile version (HLSL: 40, 50, 60), (ES: 200, 300), (GLSL: 330, 400, 420)", "ProfileVersion" },
        { "dumpc", 'C', SX_CMDLINE_OPTYPE_FLAG_SET, &dump_conf, 1, "Dump shader limits configuration", 0x0 },
        { "include-dirs", 'I', SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'I', "Set include directory for <system> files, seperated by ';'", "Directory(s)" },
        { "preprocess", 'P', SX_CMDLINE_OPTYPE_FLAG_SET, &args.preprocess, 1, "Dump preprocessed result to terminal" },
        { "cvar", 'N', SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'N', "Outputs Hex data to a C include file with a variable name", "VariableName" },

        // axmol spec start
        { "msl-ios", 0x0, SX_CMDLINE_OPTYPE_FLAG_SET, &args.msl_ios, 1, "Target iOS Metal instead of macOS Metal", 0x0 },
        { "msl-reset-vlocs", 0x0, SX_CMDLINE_OPTYPE_FLAG_SET, &args.msl_reset_vlocs, 1, "Whether reset MSL vertex locations", 0x0 },
        { "auto-map-bindings", 0x0, SX_CMDLINE_OPTYPE_FLAG_SET, &args.auto_map_bindings, 1, "This option remove binding requirement in shader", 0x0 },
        { "auto-map-locations", 0x0, SX_CMDLINE_OPTYPE_FLAG_SET, &args.auto_map_locations, 1, "This option remove location requirement in shader", 0x0 },
        { "automap", 0x0, SX_CMDLINE_OPTYPE_FLAG_SET, &args.automap, 1, "This option remove binding and location requirement in shader (deprecated)", 0x0 },
        { "inline-ubo-members", 0x0, SX_CMDLINE_OPTYPE_FLAG_SET, &args.inline_ubo_members, 1, "Converts uniform block members into individual global uniform declarations.\n"
                                                                                              "\tExample : \n"
                                                                                              "\t\tuniform fs_ub {\n"
                                                                                              "\t\t\tfloat a;\n"
                                                                                              "\t\t\tfloat b;\n"
                                                                                              "\t\t};\n"
                                                                                              "\tbecomes : \n"
                                                                                              "\t\tuniform float a;\n"
                                                                                              "\t\tuniform float b;",
            0x0 },
        { "flatten-ubos", 0x0, SX_CMDLINE_OPTYPE_FLAG_SET, &args.inline_ubo_members, 1, "Deprecated, use --inline-ubo-members instead",
            0x0 },
        { "fixup-clipspace", 0x0, SX_CMDLINE_OPTYPE_FLAG_SET, &args.fixup_clipspace, 1, "Fixup Z clip-space at the end of a vertex shader. The behavior is backend-dependent.\n"
                                                                                        "\t\tGLSL: Rewrites [0, w] Z range (D3D/Metal/Vulkan) to GL-style [-w, w].\n"
                                                                                        "\t\tHLSL/MSL: Rewrites [-w, w] Z range (GL) to D3D/Metal/Vulkan-style [0, w].\n",
            0x0 },
        // axmol spec end

        { "flatten-ubo", 0x0, SX_CMDLINE_OPTYPE_FLAG_SET, &args.flatten_ubo, 1, "Emit UBOs as plain uniform arrays which are suitable for use with glUniform4*v().\n"
                                                                                "\t\tThis can be an optimization on GL implementations where this is faster or works around buggy driver implementations.\n"
                                                                                "\t\tE.g.: uniform MyUBO { vec4 a; float b, c, d, e; }; will be emitted as uniform vec4 MyUBO[2];\n"
                                                                                "\t\tCaveat: You cannot mix and match floating-point and integer in the same UBO with this option.\n"
                                                                                "\t\tLegacy GLSL/ESSL (where this flattening makes sense) does not support bit-casting, which would have been the obvious workaround.\n",
            0x0 },

        { "reflect", 'r', SX_CMDLINE_OPTYPE_OPTIONAL, 0x0, 'r', "Output shader reflection information to a json file", "Filepath" },
        { "sgs", 'G', SX_CMDLINE_OPTYPE_FLAG_SET, &args.sc_file, 1, "Output file should be packed axslcc spec binary format", "Filepath" },
        { "sc", 'B', SX_CMDLINE_OPTYPE_FLAG_SET, &args.sc_file, 1, "Output file should be packed axslcc spec binary format", "Filepath" },
        { "bin", 'b', SX_CMDLINE_OPTYPE_FLAG_SET, &args.compile_bin, 1, "Compile to bytecode instead of source. requires ENABLE_D3D11_COMPILER build flag", 0x0 },
        { "debug", 'g', SX_CMDLINE_OPTYPE_FLAG_SET, &args.debug_info, 1, "Generate debug info for binary compilation, should come with --bin", 0x0 },
        { "optimize", 'O', SX_CMDLINE_OPTYPE_FLAG_SET, &args.optimize, 1, "Optimize shader for release compilation", 0x0 },
        { "silent", 'S', SX_CMDLINE_OPTYPE_FLAG_SET, &args.silent, 1, "Does not output filename(s) after compile success" },
        { "input", 'i', SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'i', "Input shader source file. determined by extension (.vert/.frag/.comp)", 0x0 },
        { "validate", '0', SX_CMDLINE_OPTYPE_FLAG_SET, &args.validate, 1, "Only performs shader validatation and error checking", 0x0 },
        { "err-format", 'E', SX_CMDLINE_OPTYPE_REQUIRED, 0x0, 'E', "Output error format", "glslang/msvc" },
        { "list-includes", 'L', SX_CMDLINE_OPTYPE_FLAG_SET, &args.list_includes, 1, "List include files in shaders, does not generate any output files", 0x0 },
        SX_CMDLINE_OPT_END
    };
    sx_cmdline_context* cmdline = sx_cmdline_create_context(g_alloc, argc, (const char**)argv, opts);

    // non-flag options need assign manually
    int opt;
    const char* arg;
    while ((opt = sx_cmdline_next(cmdline, NULL, &arg)) != -1) {
        switch (opt) {
        case '+':
            detect_input_file(&args, arg);
            break;
        case '?':
            printf("Unknown argument: %s\n", arg);
            exit(-1);
            break;
        case '!':
            printf("Invalid use of argument: %s\n", arg);
            exit(-1);
            break;
        case 'v':
            args.vs_filepath = arg;
            break;
        case 'f':
            args.fs_filepath = arg;
            break;
        case 'c':
            args.cs_filepath = arg;
            break;
        case 'o':
            args.out_filepath = arg;
            break;
        case 'D':
            parse_defines(&args, arg);
            break;
        case 'l':
            args.lang = parse_shader_lang(arg);
            break;
        case 'h':
            print_help(cmdline);
            break;
        case 'p':
            args.profile_ver = sx_toint(arg);
            break;
        case 'I':
            parse_includes(&args, arg);
            break;
        case 'N':
            args.cvar = arg;
            break;
        case 'r':
            args.reflect_filepath = arg;
            args.reflect = 1;
            break;
        case 'i':
            detect_input_file(&args, arg);
            break;
        case 'E':
            args.err_format = parse_output_errorformat(arg);
            break;
        default:
            break;
        }
    }

    if (version) {
        print_version();
        exit(0);
    }

    if (dump_conf) {
        puts(GetDefaultTBuiltInResourceString().c_str());
        exit(0);
    }

    if ((args.vs_filepath && !sx_os_path_isfile(args.vs_filepath)) || (args.fs_filepath && !sx_os_path_isfile(args.fs_filepath)) || (args.cs_filepath && !sx_os_path_isfile(args.cs_filepath))) {
        puts("Input files are invalid");
        exit(-1);
    }

    if (!args.vs_filepath && !args.fs_filepath && !args.cs_filepath) {
        puts("You must at least define one input shader file");
        exit(-1);
    }

    if (args.cs_filepath && (args.vs_filepath || args.fs_filepath)) {
        puts("Cannot link compute-shader with either fragment shader or vertex shader");
        exit(-1);
    }

    if (args.out_filepath == nullptr && !(args.preprocess | args.validate | args.list_includes)) {
        puts("Output file is not specified");
        exit(-1);
    }

    if (args.lang == SHADER_LANG_COUNT && !(args.preprocess | args.validate | args.list_includes)) {
        puts("Shader language is not specified");
        exit(-1);
    }

    if (args.out_filepath) {
        // determine if we output SGS format automatically
        char ext[32];
        sx_os_path_ext(ext, sizeof(ext), args.out_filepath);
        if (sx_strequalnocase(ext, ".sgs"))
            args.sc_file = 1;
    }

    // Set default shader profile version
    // HLSL: 50 (5.0)
    // GLSL: 330 (3.3)
    // ESSL: 300 (3.0)
    // MSL: 20000 (2.0)
    if (args.profile_ver == 0) {
        if (args.lang == SHADER_LANG_ESSL)
            args.profile_ver = 300;
        else if (args.lang == SHADER_LANG_HLSL)
            args.profile_ver = 50; // D3D11
        else if (args.lang == SHADER_LANG_GLSL)
            args.profile_ver = 330;
        else if (args.lang == SHADER_LANG_MSL)
            args.profile_ver = spirv_cross::CompilerMSL::Options::make_msl_version(2, 0);
        else if (args.lang == SHADER_LANG_SPIRV)
            args.profile_ver = 100;
    }

#if SX_PLATFORM_WINDOWS
    if (args.compile_bin && (args.lang != SHADER_LANG_HLSL || args.profile_ver >= 60)) {
        puts("ignoring --bin flag, byte-code compilation not implemented for this target");
        args.compile_bin = 0;
    }
#ifndef D3D11_COMPILER
    // Windows + HLSL -> works but requires ENABLE_D3D11_COMPILER
    else if (args.compile_bin) {
        puts("Cannot compile to byte-code, glslcc is not built with ENABLE_D3D11_COMPILER flag");
        exit(-1);
    }
#endif
#else
    if (args.compile_bin) {
        puts("Ignoring --bin flag, byte-code compilation not implemented for this target");
        args.compile_bin = 0;
    }
#endif

    if (args.sc_file && !(args.preprocess | args.validate | args.list_includes)) {
        uint32_t slang = 0;
        switch (args.lang) {
        case SHADER_LANG_ESSL:
            slang = SC_LANG_GLES;
            break;
        case SHADER_LANG_HLSL:
            slang = SC_LANG_HLSL;
            break;
        case SHADER_LANG_MSL:
            slang = SC_LANG_MSL;
            break;
        case SHADER_LANG_GLSL:
            slang = SC_LANG_GLSL;
            break;
        case SHADER_LANG_SPIRV:
            slang = SC_LANG_SPIRV;
            break;
        default:
            sx_assert(0);
            break;
        }
        sc_file_handle = sc_create_file(g_alloc, args.out_filepath, AXSLCC_VERSION_MAJOR, AXSLCC_VERSION_MINOR, slang, args.profile_ver);
        sx_assert(sc_file_handle);
    }

    int r = compile_files(args, *GetDefaultResources());

    if (sc_file_handle) {
        if (r == 0 && !sc_commit(sc_file_handle)) {
            printf("Writing SGS file '%s' failed\n", args.out_filepath);
        }
        sc_destroy_file(sc_file_handle);
    }

    sx_cmdline_destroy_context(cmdline, g_alloc);
    cleanup_args(&args);
    return r;
}
