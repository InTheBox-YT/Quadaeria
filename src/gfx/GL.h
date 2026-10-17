#pragma once
#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#define QC_GLAPI __stdcall
#else
#define QC_GLAPI
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

namespace qc::gl {
enum : GLenum {
    DEPTH_BUFFER_BIT = 0x0100,
    COLOR_BUFFER_BIT = 0x4000,
    TRIANGLES = 0x0004,
    UNSIGNED_BYTE = 0x1401,
    UNSIGNED_INT = 0x1405,
    FLOAT = 0x1406,
    ARRAY_BUFFER = 0x8892,
    ELEMENT_ARRAY_BUFFER = 0x8893,
    STATIC_DRAW = 0x88E4,
    DYNAMIC_DRAW = 0x88E8,
    STREAM_DRAW = 0x88E0,
    VERTEX_SHADER = 0x8B31,
    FRAGMENT_SHADER = 0x8B30,
    COMPILE_STATUS = 0x8B81,
    LINK_STATUS = 0x8B82,
    INFO_LOG_LENGTH = 0x8B84,
    DEPTH_TEST = 0x0B71,
    CULL_FACE = 0x0B44,
    BLEND = 0x0BE2,
    MULTISAMPLE = 0x809D,
    POLYGON_OFFSET_FILL = 0x8037,
    BACK = 0x0405,
    FRONT = 0x0404,
    NONE = 0,
    SRC_ALPHA = 0x0302,
    ONE_MINUS_SRC_ALPHA = 0x0303,
    LESS = 0x0201,
    EQUAL = 0x0202,
    LEQUAL = 0x0203,
    TEXTURE_2D = 0x0DE1,
    TEXTURE0 = 0x84C0,
    TEXTURE_MIN_FILTER = 0x2801,
    TEXTURE_MAG_FILTER = 0x2800,
    TEXTURE_WRAP_S = 0x2802,
    TEXTURE_WRAP_T = 0x2803,
    NEAREST = 0x2600,
    LINEAR = 0x2601,
    CLAMP_TO_EDGE = 0x812F,
    CLAMP_TO_BORDER = 0x812D,
    TEXTURE_BORDER_COLOR = 0x1004,
    DEPTH_COMPONENT = 0x1902,
    DEPTH_COMPONENT24 = 0x81A6,
    TEXTURE_COMPARE_MODE = 0x884C,
    TEXTURE_COMPARE_FUNC = 0x884D,
    COMPARE_REF_TO_TEXTURE = 0x884E,
    FRAMEBUFFER = 0x8D40,
    DEPTH_ATTACHMENT = 0x8D00,
    FRAMEBUFFER_COMPLETE = 0x8CD5,
    RED = 0x1903,
    R8 = 0x8229,
    RGB = 0x1907,
    RGBA = 0x1908,
    UNPACK_ALIGNMENT = 0x0CF5,
    PACK_ALIGNMENT = 0x0D05,
    VENDOR = 0x1F00,
    RENDERER = 0x1F01,
    VERSION = 0x1F02,
    RENDERBUFFER = 0x8D41,
    DEPTH24_STENCIL8 = 0x88F0,
    DEPTH_STENCIL_ATTACHMENT = 0x821A,
    COLOR_ATTACHMENT0 = 0x8CE0,
    READ_FRAMEBUFFER = 0x8CA8,
    DRAW_FRAMEBUFFER = 0x8CA9,
    RGBA8 = 0x8058,
    REPEAT = 0x2901,
    LINEAR_MIPMAP_LINEAR = 0x2703,
    NEAREST_MIPMAP_NEAREST = 0x2700,
    LINES = 0x0001,
    LINE_STRIP = 0x0003,
    TRIANGLE_STRIP = 0x0005,
    SCISSOR_TEST = 0x0C11,
    ONE = 1,
    ZERO = 0,
    ALWAYS = 0x0207,
    UNSIGNED_SHORT = 0x1403,
    TEXTURE1 = 0x84C1,
    TEXTURE2 = 0x84C2,
    FRAMEBUFFER_BINDING = 0x8CA6,
    VIEWPORT = 0x0BA2,
    DEPTH_COMPONENT32F = 0x8CAC,
    RGBA32F = 0x8814,
    R32UI = 0x8236,
    RG32UI = 0x823C,
    RED_INTEGER = 0x8D94,
    RG_INTEGER = 0x8228,
    MAX_ARRAY_TEXTURE_LAYERS = 0x88FF,
    TEXTURE_2D_ARRAY = 0x8C1A,
    TEXTURE_MAX_LEVEL = 0x813D,
    TEXTURE_BASE_LEVEL = 0x813C,
    NEAREST_MIPMAP_LINEAR = 0x2702,
    LINEAR_MIPMAP_NEAREST = 0x2701,
    INT = 0x1404,
    TEXTURE_MAX_ANISOTROPY = 0x84FE,
    SRGB8_ALPHA8 = 0x8C43,
    TEXTURE_LOD_BIAS = 0x8501,
    MAX_TEXTURE_MAX_ANISOTROPY = 0x84FF,
    ONE_MINUS_SRC_COLOR = 0x0301,
    DST_COLOR = 0x0306,
    FUNC_ADD = 0x8006,
    POINTS = 0x0000,
    PROGRAM_POINT_SIZE = 0x8642,
    TEXTURE_CUBE_MAP = 0x8513,
    TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515,
    TEXTURE_3D = 0x806F,
    TEXTURE_WRAP_R = 0x8072,
    RG = 0x8227,
    RG8 = 0x822B,
    TEXTURE3 = 0x84C3,
    TEXTURE4 = 0x84C4,
    TEXTURE5 = 0x84C5,
    DRAW_INDIRECT_BUFFER = 0x8F3F,
    TIME_ELAPSED = 0x88BF,
    QUERY_RESULT = 0x8866,
    QUERY_RESULT_AVAILABLE = 0x8867,
    COPY_READ_BUFFER = 0x8F36,
    COPY_WRITE_BUFFER = 0x8F37,
    TEXTURE6 = 0x84C6,
    TIMESTAMP = 0x8E28,
};
constexpr GLboolean FALSE_ = 0, TRUE_ = 1;

#define QC_GL_FUNCTIONS(X)                                                                                                                 \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                                                                    \
    X(void, Clear, (GLbitfield))                                                                                                           \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                                                              \
    X(void, Enable, (GLenum))                                                                                                              \
    X(void, Disable, (GLenum))                                                                                                             \
    X(void, DepthFunc, (GLenum))                                                                                                           \
    X(void, DepthMask, (GLboolean))                                                                                                        \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                                                                       \
    X(void, CullFace, (GLenum))                                                                                                            \
    X(void, BlendFunc, (GLenum, GLenum))                                                                                                   \
    X(void, PolygonOffset, (GLfloat, GLfloat))                                                                                             \
    X(const GLubyte*, GetString, (GLenum))                                                                                                 \
    X(GLenum, GetError, (void))                                                                                                            \
    X(void, PixelStorei, (GLenum, GLint))                                                                                                  \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))                                                           \
    X(void, ReadBuffer, (GLenum))                                                                                                          \
    X(void, DrawBuffer, (GLenum))                                                                                                          \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                                                                           \
    X(void, BindVertexArray, (GLuint))                                                                                                     \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                                                                  \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                                                                \
    X(void, BindBuffer, (GLenum, GLuint))                                                                                                  \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                                                                         \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))                                                                    \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                                                                       \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))                                                 \
    X(void, EnableVertexAttribArray, (GLuint))                                                                                             \
    X(void, VertexAttribDivisor, (GLuint, GLuint))                                                                                         \
    X(GLuint, CreateShader, (GLenum))                                                                                                      \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                                                           \
    X(void, CompileShader, (GLuint))                                                                                                       \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                                                                         \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                                                        \
    X(void, DeleteShader, (GLuint))                                                                                                        \
    X(GLuint, CreateProgram, (void))                                                                                                       \
    X(void, AttachShader, (GLuint, GLuint))                                                                                                \
    X(void, LinkProgram, (GLuint))                                                                                                         \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                                                                        \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                                                       \
    X(void, DeleteProgram, (GLuint))                                                                                                       \
    X(void, UseProgram, (GLuint))                                                                                                          \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                                                                  \
    X(void, Uniform1i, (GLint, GLint))                                                                                                     \
    X(void, Uniform1f, (GLint, GLfloat))                                                                                                   \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                                                                          \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                                                                 \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                                                        \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                                                                 \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                                                                          \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void*))                                                                          \
    X(void, DrawElementsInstanced, (GLenum, GLsizei, GLenum, const void*, GLsizei))                                                        \
    X(void, GenTextures, (GLsizei, GLuint*))                                                                                               \
    X(void, BindTexture, (GLenum, GLuint))                                                                                                 \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))                                      \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                                                        \
    X(void, TexParameterfv, (GLenum, GLenum, const GLfloat*))                                                                              \
    X(void, ActiveTexture, (GLenum))                                                                                                       \
    X(void, DeleteTextures, (GLsizei, const GLuint*))                                                                                      \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                                                                           \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                                                             \
    X(void, FramebufferTextureLayer, (GLenum, GLenum, GLuint, GLint, GLint))                                                               \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                                                 \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                                                                            \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                                                                  \
    X(void, GenSamplers, (GLsizei, GLuint*))                                                                                               \
    X(void, BindSampler, (GLuint, GLuint))                                                                                                 \
    X(void, SamplerParameteri, (GLuint, GLenum, GLint))                                                                                    \
    X(void, DeleteSamplers, (GLsizei, const GLuint*))                                                                                      \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))                                 \
    X(void, GenRenderbuffers, (GLsizei, GLuint*))                                                                                          \
    X(void, BindRenderbuffer, (GLenum, GLuint))                                                                                            \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                                                                       \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                                                                     \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint*))                                                                                 \
    X(void, GenerateMipmap, (GLenum))                                                                                                      \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*))                                   \
    X(void, Uniform1fv, (GLint, GLsizei, const GLfloat*))                                                                                  \
    X(void, Uniform2fv, (GLint, GLsizei, const GLfloat*))                                                                                  \
    X(void, Uniform3fv, (GLint, GLsizei, const GLfloat*))                                                                                  \
    X(void, Uniform4fv, (GLint, GLsizei, const GLfloat*))                                                                                  \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                                                                     \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                                                           \
    X(void, GetIntegerv, (GLenum, GLint*))                                                                                                 \
    X(void, ClearDepth, (double))                                                                                                          \
    X(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))                             \
    X(void, TexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*))                   \
    X(void, VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void*))                                                           \
    X(void, DrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))                                                                        \
    X(void, TexParameterf, (GLenum, GLenum, GLfloat))                                                                                      \
    X(void, GetFloatv, (GLenum, GLfloat*))                                                                                                 \
    X(void, LineWidth, (GLfloat))                                                                                                          \
    X(void, Uniform2i, (GLint, GLint, GLint))                                                                                              \
    X(void, Uniform3i, (GLint, GLint, GLint, GLint))                                                                                       \
    X(void, Finish, (void))                                                                                                                \
    X(void, DrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void*, GLint))                                                         \
    X(void, CopyBufferSubData, (GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr))                                                           \
    X(void, GenQueries, (GLsizei, GLuint*))                                                                                                \
    X(void, DeleteQueries, (GLsizei, const GLuint*))                                                                                       \
    X(void, BeginQuery, (GLenum, GLuint))                                                                                                  \
    X(void, EndQuery, (GLenum))                                                                                                            \
    X(void, GetQueryObjectiv, (GLuint, GLenum, GLint*))                                                                                    \
    X(void, GetQueryObjectui64v, (GLuint, GLenum, uint64_t*))                                                                              \
    X(void, VertexAttrib3f, (GLuint, GLfloat, GLfloat, GLfloat))                                                                           \
    X(void, VertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat))                                                                  \
    X(void, QueryCounter, (GLuint, GLenum))

#define QC_GL_OPTIONAL(X) X(void, MultiDrawElementsIndirect, (GLenum, GLenum, const void*, GLsizei, GLsizei))

#define QC_GL_DECLARE(ret, name, params)                                                                                                   \
    typedef ret(QC_GLAPI* PFN_##name) params;                                                                                              \
    extern PFN_##name name;
QC_GL_FUNCTIONS(QC_GL_DECLARE)
QC_GL_OPTIONAL(QC_GL_DECLARE)
#undef QC_GL_DECLARE

using LoadProc = void* (*)(const char* name);
bool load(LoadProc proc);

}
