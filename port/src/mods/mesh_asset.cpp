#include "mesh_asset.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>
#include <stdexcept>
namespace mods {
namespace {
using Json = nlohmann::json;
void Require(bool valid, const char *message) { if (!valid) throw std::runtime_error(message); }
bool Integer(const Json &value, std::uint64_t maximum) {
    return value.is_number_integer() && value.get<double>() >= 0 && value.get<double>() <= static_cast<double>(maximum);
}
}
MeshAsset DecodeMeshAsset(std::string_view source) {
    Require(!source.empty() && source.size() <= 16*1024*1024, "mesh source must be 1 byte..16 MiB");
    std::set<std::string> fields;
    const auto data = Json::parse(source, [&fields](int depth, Json::parse_event_t event, Json &value) {
        // Schema 1 has only one object, with arrays nested at most two levels.
        Require(depth <= 3, "mesh JSON nesting exceeds schema");
        if (event == Json::parse_event_t::object_start) Require(depth == 0, "mesh has a nested object");
        if (event == Json::parse_event_t::key) Require(fields.insert(value.get<std::string>()).second, "duplicate mesh field");
        return true;
    });
    Require(data.is_object() && data.size()==3 && data.contains("schema_version") && data.contains("vertices") && data.contains("triangles"), "invalid mesh fields");
    Require(Integer(data["schema_version"],1) && data["schema_version"]==1, "unsupported mesh schema");
    const auto &vertices = data["vertices"], &triangles = data["triangles"];
    Require(vertices.is_array() && vertices.size()>=3 && vertices.size()<=100000, "mesh vertex count must be 3..100000");
    Require(triangles.is_array() && !triangles.empty() && triangles.size()<=200000, "mesh triangle count must be 1..200000");
    MeshAsset mesh; mesh.vertices.reserve(vertices.size()); mesh.triangles.reserve(triangles.size());
    for (const auto &source_vertex : vertices) {
        Require(source_vertex.is_array() && source_vertex.size()==8, "mesh vertex must have 8 components");
        std::array<float,8> vertex;
        for (unsigned i=0;i<8;++i) {
            Require(source_vertex[i].is_number(), "mesh vertex component must be numeric");
            const double value = source_vertex[i].get<double>();
            Require(std::isfinite(value) && std::abs(value)<=1000000, "mesh component must be finite and bounded");
            vertex[i]=static_cast<float>(value);
        }
        const double length=std::sqrt(double(vertex[3])*vertex[3]+double(vertex[4])*vertex[4]+double(vertex[5])*vertex[5]);
        Require(std::abs(length-1)<=0.001, "mesh normal must have unit length");
        mesh.vertices.push_back(vertex);
    }
    for (const auto &source_triangle : triangles) {
        Require(source_triangle.is_array() && source_triangle.size()==4, "triangle requires strip,a,b,c");
        Require(Integer(source_triangle[0],255), "mesh strip out of range");
        MeshAsset::Triangle triangle{source_triangle[0].get<std::uint32_t>(),{}};
        for(unsigned i=0;i<3;++i) {
            Require(Integer(source_triangle[i+1],mesh.vertices.size()-1), "mesh vertex index out of range");
            triangle.vertices[i]=source_triangle[i+1].get<std::uint32_t>();
        }
        const auto [ia,ib,ic]=triangle.vertices;
        Require(ia!=ib && ia!=ic && ib!=ic, "mesh triangle repeats a vertex");
        const auto &a=mesh.vertices[ia], &b=mesh.vertices[ib], &c=mesh.vertices[ic];
        double u[3],v[3]; for(unsigned i=0;i<3;++i) {u[i]=double(b[i])-a[i];v[i]=double(c[i])-a[i];}
        const double cross[]{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        Require(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]>1e-20, "mesh triangle degenerates at renderer precision");
        mesh.triangles.push_back(triangle);
    }
    return mesh;
}
}
