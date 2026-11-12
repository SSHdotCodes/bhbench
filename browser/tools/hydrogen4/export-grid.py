from pathlib import Path
repo=Path(__file__).resolve().parents[3]
root=repo/'submissions/gemini-4-hydrogen'
s=(root/'src/platform/MetalApp.mm').read_text()
body=s[s.index('std::vector<GridVertex> buildSpacetimeGridMesh'):s.index('bool saveBGRA8TextureToPNG')]
out='''// Native mesh function copied verbatim by tools/export-grid.py.
#include "../../../submissions/gemini-4-hydrogen/src/core/Math.hpp"
#include "../../../submissions/gemini-4-hydrogen/src/core/BlackHolePhysics.hpp"
#include "../../../submissions/gemini-4-hydrogen/src/shaders/ShaderTypes.h"
#include <vector>
#include <cstdint>
#include <fstream>
#include <iostream>
using namespace bh;
'''+body+'''
int main(int argc,char**argv){
 std::vector<uint32_t> indices;
 auto vertices=buildSpacetimeGridMesh(0.90,28.0f,128,160,indices);
 std::ofstream(std::string(argv[1])+"/grid.f32",std::ios::binary).write((char*)vertices.data(),vertices.size()*sizeof(GridVertex));
 std::ofstream(std::string(argv[1])+"/grid.u32",std::ios::binary).write((char*)indices.data(),indices.size()*sizeof(uint32_t));
 std::cout<<vertices.size()<<" vertices, "<<indices.size()<<" indices, "<<sizeof(GridVertex)<<" bytes/vertex\\n";
}
'''
(repo/'browser/tools/hydrogen4/export-grid.cpp').write_text(out)
