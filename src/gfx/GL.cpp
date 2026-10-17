#include "gfx/GL.h"
#include "core/Base.h"
#include <string>

namespace qc::gl {
#define QC_GL_DEFINE(ret, name, params) PFN_##name name = nullptr;
QC_GL_FUNCTIONS(QC_GL_DEFINE)
QC_GL_OPTIONAL(QC_GL_DEFINE)
#undef QC_GL_DEFINE

bool load(LoadProc proc) {
    bool ok = true;
#define QC_GL_LOAD(ret, name, params)                                                                                                      \
    name = reinterpret_cast<PFN_##name>(proc("gl" #name));                                                                                 \
    if (!name) {                                                                                                                           \
        QC_ERROR("missing OpenGL function gl%s", #name);                                                                                   \
        ok = false;                                                                                                                        \
    }
    QC_GL_FUNCTIONS(QC_GL_LOAD)
#undef QC_GL_LOAD
#define QC_GL_LOAD_OPT(ret, name, params) name = reinterpret_cast<PFN_##name>(proc("gl" #name));
    QC_GL_OPTIONAL(QC_GL_LOAD_OPT)
#undef QC_GL_LOAD_OPT
    return ok;
}

}
