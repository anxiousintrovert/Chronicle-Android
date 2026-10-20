#include "mesh_skin.hpp"
#include "vu0_ops.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
namespace mods {
namespace {
void SkinVertex(float *out, float *deformed, const float *source, const float base[4][4], const float bone[4][4],
                const float inverse[4][4], float weight) {
    float local[4];
    for (int i = 0; i < 3; i++) {
        local[i] = source[i] - base[3][i];
    }
    local[3] = 1.0f;

    float accumulated[4];
    vu0::Apply(accumulated, base, local);
    for (int i = 0; i < 3; i++) {
        local[i] = accumulated[i] - base[3][i];
    }

    vu0::Apply(accumulated, bone, local);
    float moved[3];
    for (int i = 0; i < 3; i++) {
        moved[i] = accumulated[i] - source[i];
    }

    for (int i = 0; i < 3; i++) {
        deformed[i] = deformed[i] + moved[i] * weight;
    }

    float result[4];
    vu0::Apply(result, inverse, deformed);
    vu0::Copy(out, result);
}
using Point=MeshSkinBinding::Point;
struct Node { std::uint32_t point; int left=-1,right=-1; unsigned axis=0; };
class Nearest {
    std::span<const Point> points;
    std::vector<std::uint32_t> indices;
    std::vector<Node> nodes;
    mutable std::uint64_t visits=0;
    int Build(std::size_t begin,std::size_t end,unsigned axis) {
        if(begin==end) return -1;
        const auto middle=begin+(end-begin)/2;
        std::nth_element(indices.begin()+begin,indices.begin()+middle,indices.begin()+end,[&](auto a,auto b) {
            return points[a][axis]<points[b][axis] || (points[a][axis]==points[b][axis] && a<b);
        });
        const int node=static_cast<int>(nodes.size()); nodes.push_back({indices[middle],-1,-1,axis});
        const int left=Build(begin,middle,(axis+1)%3),right=Build(middle+1,end,(axis+1)%3);
        nodes[node].left=left;nodes[node].right=right;return node;
    }
    void Visit(int index,const Point &point,std::uint32_t &best,double &distance) const {
        if(index<0 || (distance==0 && best==0)) return;
        if(++visits>25000000) throw std::runtime_error("mesh binding nearest search exceeds work budget");
        const auto &node=nodes[index]; const auto &candidate=points[node.point];
        double d=0;for(unsigned i=0;i<3;++i) {const double delta=double(point[i])-candidate[i];d+=delta*delta;}
        if(d<distance || (d==distance && node.point<best)) {best=node.point;distance=d;}
        const double split=double(point[node.axis])-candidate[node.axis];
        Visit(split<=0?node.left:node.right,point,best,distance);
        if(split*split<=distance) Visit(split<=0?node.right:node.left,point,best,distance);
    }
public:
    explicit Nearest(std::span<const Point> source):points(source),indices(source.size()) {
        nodes.reserve(source.size());std::iota(indices.begin(),indices.end(),0);Build(0,indices.size(),0);
    }
    std::uint32_t Find(const Point &point) const {
        std::uint32_t best=std::numeric_limits<std::uint32_t>::max();double distance=INFINITY;Visit(0,point,best,distance);return best;
    }
};
bool Valid(const MeshSkinBinding::Vertex &vertex) {
    return std::ranges::all_of(vertex,[](float value){return std::isfinite(value);});
}
}
MeshSkinBinding MeshSkinBinding::Prepare(std::span<const Point> source,std::span<const Point> replacement,std::span<const std::uint32_t> mapping) {
    if(source.empty() || source.size()>100000 || replacement.empty() || replacement.size()>100000)
        throw std::runtime_error("mesh binding requires 1..100000 source and replacement points");
    for(auto points:{source,replacement}) for(const auto &point:points) for(float value:point)
        if(!std::isfinite(value) || std::abs(value)>1000000) throw std::runtime_error("mesh binding point must be finite and bounded");
    if(!mapping.empty() && (mapping.size()!=replacement.size() || std::ranges::any_of(mapping,[&](auto i){return i>=source.size();})))
        throw std::runtime_error("invalid explicit mesh source mapping");
    std::optional<Nearest> nearest;if(mapping.empty())nearest.emplace(source);MeshSkinBinding result;
    result.rest_.reserve(replacement.size());result.sources_.reserve(replacement.size());result.offsets_.resize(source.size()+1);
    for(const auto &point:replacement) {
        const auto index=mapping.empty()?nearest->Find(point):mapping[result.sources_.size()];result.sources_.push_back(index);++result.offsets_[index+1];
        result.rest_.push_back({point[0],point[1],point[2],1});
    }
    std::partial_sum(result.offsets_.begin(),result.offsets_.end(),result.offsets_.begin());
    auto next=result.offsets_;result.targets_.resize(replacement.size());
    for(std::uint32_t i=0;i<result.sources_.size();++i) result.targets_[next[result.sources_[i]]++]=i;
    result.deformed_=result.rest_;result.positions_=result.rest_;return result;
}
void MeshSkinBinding::BeginPose() noexcept {
    std::copy(rest_.begin(),rest_.end(),deformed_.begin());std::copy(rest_.begin(),rest_.end(),positions_.begin());
}
bool MeshSkinBinding::ApplyBone(std::uint32_t vertex,const float base[4][4],const float bone[4][4],const float inverse[4][4],float weight) noexcept {
    if(vertex+std::uint64_t(1)>=offsets_.size() || !base || !bone || !inverse || !std::isfinite(weight) || weight<0 || weight>1) return false;
    for(auto matrix:{base,bone,inverse}) for(unsigned c=0;c<4;++c) for(unsigned r=0;r<4;++r) if(!std::isfinite(matrix[c][r])) return false;
    // Validate the whole influence before publishing any vertex. Two bounded
    // arithmetic passes avoid allocating a temporary pose during animation.
    for(unsigned pass=0;pass<2;++pass) for(auto at=offsets_[vertex];at<offsets_[vertex+1];++at) {
        const auto target=targets_[at];Vertex deformed=deformed_[target],position{};
        SkinVertex(position.data(),deformed.data(),rest_[target].data(),base,bone,inverse,weight);
        if(!Valid(deformed) || !Valid(position)) return false;
        if(pass) {deformed_[target]=deformed;positions_[target]=position;}
    }
    return true;
}
}
