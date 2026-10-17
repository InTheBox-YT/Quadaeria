#pragma once
#include "core/Base.h"
#include "gfx/GL.h"
#include <string>
#include <utility>
#include <vector>

namespace qc {
class Shader {
public:
    bool build(const char* name, const char* vs, const char* fs) {
        name_ = name;
        GLuint v = compile(gl::VERTEX_SHADER, vs), f = compile(gl::FRAGMENT_SHADER, fs);
        if (!v || !f) return false;
        prog_ = gl::CreateProgram();
        gl::AttachShader(prog_, v);
        gl::AttachShader(prog_, f);
        gl::LinkProgram(prog_);
        gl::DeleteShader(v);
        gl::DeleteShader(f);
        GLint ok = 0;
        gl::GetProgramiv(prog_, gl::LINK_STATUS, &ok);
        if (!ok) {
            char log[2048];
            gl::GetProgramInfoLog(prog_, sizeof(log), nullptr, log);
            QC_ERROR("shader %s link failed: %s", name, log);
            return false;
        }
        return true;
    }
    void use() const { gl::UseProgram(prog_); }
    GLint loc(const char* n) {
        for (const auto& e : locs_)
            if (e.first == n) return e.second;
        GLint l = gl::GetUniformLocation(prog_, n);
        locs_.push_back({n, l});
        return l;
    }
    void set(const char* n, float v) { gl::Uniform1f(loc(n), v); }
    void set(const char* n, int v) { gl::Uniform1i(loc(n), v); }
    void set(const char* n, vec2 v) { gl::Uniform2f(loc(n), v.x, v.y); }
    void set(const char* n, vec3 v) { gl::Uniform3f(loc(n), v.x, v.y, v.z); }
    void set(const char* n, vec4 v) { gl::Uniform4f(loc(n), v.x, v.y, v.z, v.w); }
    void set(const char* n, const mat4& m) { gl::UniformMatrix4fv(loc(n), 1, gl::FALSE_, m.m); }
    GLuint id() const { return prog_; }
    void destroy() {
        if (prog_) gl::DeleteProgram(prog_);
        prog_ = 0;
    }

private:
    GLuint compile(GLenum type, const char* src) {
        GLuint s = gl::CreateShader(type);
        gl::ShaderSource(s, 1, &src, nullptr);
        gl::CompileShader(s);
        GLint ok = 0;
        gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            gl::GetShaderInfoLog(s, sizeof(log), nullptr, log);
            QC_ERROR("shader %s compile failed: %s", name_.c_str(), log);
            gl::DeleteShader(s);
            return 0;
        }
        return s;
    }
    GLuint prog_ = 0;
    std::string name_;
    std::vector<std::pair<const char*, GLint>> locs_;
};

}
