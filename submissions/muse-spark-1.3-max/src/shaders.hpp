// Minimal shader program helper.
#pragma once

#include <string>

class Shader {
  public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Loads, compiles and links vert/frag files. Returns false on error
    // (message printed to stderr).
    bool load(const std::string& vert_path, const std::string& frag_path);
    void use() const;
    int uniform(const char* name) const;
    unsigned id() const { return prog_; }

  private:
    unsigned prog_ = 0;
};

std::string find_shader(const std::string& name);
