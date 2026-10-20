#include "skills.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace mods {
namespace {
using Json=nlohmann::json;
void Require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
bool Id(const Json &value) {
    if(!value.is_string())return false;
    const auto &s=value.get_ref<const std::string &>();
    return !s.empty()&&s.size()<=96&&s[0]>='a'&&s[0]<='z'&&std::ranges::all_of(s,[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-';});
}
bool Text(const Json &value) {
    if(!value.is_string())return false;const auto &s=value.get_ref<const std::string &>();
    return !s.empty()&&s.size()<=256&&std::ranges::all_of(s,[](unsigned char c){return c>=32;});
}
std::string Definition(const Json &a) {
    if(!a.is_object()||a.size()!=4||!a.contains("id")||!Id(a["id"])||!a.contains("title")||!Text(a["title"])||
       !a.contains("description")||!Text(a["description"])||!a.contains("nodes")||!a["nodes"].is_array()||a["nodes"].empty()||a["nodes"].size()>128)
        return "tree requires id, title, description and 1..128 nodes";
    std::map<std::string,const Json *> nodes;
    for(const auto &node:a["nodes"]) {
        if(!node.is_object()||node.size()!=4u+unsigned(node.contains("prerequisites"))||
           !node.contains("id")||!Id(node["id"])||!node.contains("title")||!Text(node["title"])||
           !node.contains("description")||!Text(node["description"])||!node.contains("max_rank")||
           !node["max_rank"].is_number_integer()||node["max_rank"]<1||node["max_rank"]>99||
           !nodes.emplace(node["id"].get<std::string>(),&node).second)return "invalid or duplicate skill node";
        if(node.contains("prerequisites")&&(!node["prerequisites"].is_array()||node["prerequisites"].size()>128))return "invalid prerequisites";
    }
    for(const auto &[id,node]:nodes)if(node->contains("prerequisites")) {
        std::set<std::string> seen;
        for(const auto &requirement:(*node)["prerequisites"]) {
            if(!requirement.is_object()||requirement.size()!=2||!requirement.contains("id")||!Id(requirement["id"])||
               !requirement.contains("rank")||!requirement["rank"].is_number_integer())return "prerequisite requires id and rank";
            const auto key=requirement["id"].get<std::string>();const auto target=nodes.find(key);
            if(key==id||target==nodes.end()||!seen.insert(key).second||requirement["rank"]<1||requirement["rank"]>(*target->second)["max_rank"])return "invalid prerequisite target or rank";
        }
    }
    std::map<std::string,int> marks;
    std::function<bool(const std::string &)> visit=[&](const std::string &id) {
        if(marks[id]==1)return false;if(marks[id]==2)return true;marks[id]=1;
        const auto &node=*nodes.at(id);
        if(node.contains("prerequisites"))for(const auto &r:node["prerequisites"])if(!visit(r["id"].get<std::string>()))return false;
        marks[id]=2;return true;
    };
    for(const auto &[id,node]:nodes)if(!visit(id))return "skill prerequisites contain a cycle";
    return {};
}
int Rank(const Json &state,const std::string &tree,const std::string &id) {
    return state.contains("skill_ranks")&&state["skill_ranks"].contains(tree)?state["skill_ranks"][tree].value(id,0):0;
}
Json NodeStatus(const Json &state,const std::string &tree,const Json &node) {
    auto result=node;result["tree"]=tree;result["rank"]=Rank(state,tree,node["id"].get<std::string>());
    result["missing"]=Json::array();
    if(node.contains("prerequisites"))for(const auto &r:node["prerequisites"])
        if(Rank(state,tree,r["id"].get<std::string>())<r["rank"].get<int>())result["missing"].push_back(r);
    result["available"]=result["missing"].empty()&&result["rank"]<node["max_rank"];
    return result;
}
}
std::string ValidateSkillOperation(std::string_view operation,const Json &a) {
    if(!a.is_object())return "skill arguments must be an object";
    if(operation=="skills.list")return a.empty()?"":"skills.list takes no arguments";
    if(operation=="skills.register")return Definition(a);
    if(!a.contains("tree")||!Id(a["tree"])||!a.contains("id")||!Id(a["id"]))return "provide tree and node id";
    if(operation=="skills.get"||operation=="skills.unlock")return a.size()==2?"":"provide only tree and node id";
    if(operation=="skills.set_rank")return a.size()==3&&a.contains("rank")&&a["rank"].is_number_integer()&&a["rank"]>=0&&a["rank"]<=99?"":"provide tree, id and rank 0..99";
    return "unsupported skill operation";
}
void ValidateSkillsState(const Json &state) {
    if(state.contains("skill_trees")) {
        Require(state["skill_trees"].is_object()&&state["skill_trees"].size()<=32,"at most 32 owned skill trees");
        for(const auto &[id,definition]:state["skill_trees"].items()) {
            Require(definition.is_object()&&definition.size()==3&&!definition.contains("id"),"invalid saved skill tree");
            auto a=definition;a["id"]=id;const auto error=Definition(a);if(!error.empty())throw std::runtime_error(error);
        }
    }
    if(state.contains("skill_ranks")) {
        Require(state["skill_ranks"].is_object()&&state["skill_ranks"].size()<=32,"invalid skill rank storage");
        for(const auto &[tree,ranks]:state["skill_ranks"].items()) {
            Require(state.contains("skill_trees")&&state["skill_trees"].contains(tree)&&ranks.is_object()&&ranks.size()<=128,"orphan skill ranks");
            const auto &nodes=state["skill_trees"][tree]["nodes"];
            for(const auto &[id,rank]:ranks.items()) {
                const auto node=std::ranges::find_if(nodes,[&](const auto &n){return n["id"]==id;});
                Require(node!=nodes.end()&&rank.is_number_integer()&&rank>=0&&rank<=(*node)["max_rank"],"invalid or orphan skill rank");
            }
        }
    }
}
Json CallOwnedSkill(Json &state,std::string_view operation,const Json &a) {
    const auto error=ValidateSkillOperation(operation,a);if(!error.empty())throw std::runtime_error(error);
    ValidateSkillsState(state);
    const auto treeStatus=[&](const std::string &id,const Json &definition) {
        auto result=definition;result["id"]=id;
        for(auto &node:result["nodes"])node=NodeStatus(state,id,node);
        return result;
    };
    if(operation=="skills.list") {
        Json list=Json::array();if(state.contains("skill_trees"))for(const auto &[id,definition]:state["skill_trees"].items())list.push_back(treeStatus(id,definition));
        return {{"trees",std::move(list)}};
    }
    if(operation=="skills.register") {
        const auto id=a["id"].get<std::string>();auto definition=a;definition.erase("id");
        if(state.contains("skill_trees")&&state["skill_trees"].contains(id)) {
            Require(state["skill_trees"][id]==definition,"skill tree already has a different immutable definition");
            return treeStatus(id,definition);
        }
        Require(!state.contains("skill_trees")||state["skill_trees"].size()<32,"skill tree limit reached");
        auto staged=state;staged["skill_trees"][id]=definition;const auto result=treeStatus(id,definition);state.swap(staged);return result;
    }
    const auto tree=a["tree"].get<std::string>(),id=a["id"].get<std::string>();
    Require(state.contains("skill_trees")&&state["skill_trees"].contains(tree),"unknown owned skill tree");
    const auto &nodes=state["skill_trees"][tree]["nodes"];
    const auto node=std::ranges::find_if(nodes,[&](const auto &n){return n["id"]==id;});Require(node!=nodes.end(),"unknown skill node");
    auto result=NodeStatus(state,tree,*node);
    if(operation=="skills.get")return result;
    const int old=result["rank"].get<int>(),rank=operation=="skills.unlock"?old+1:a["rank"].get<int>();
    Require(rank<=(*node)["max_rank"].get<int>(),"maximum skill rank exceeded");
    Require(rank<=old||result["missing"].empty(),"skill prerequisites are unmet");
    auto staged=state;staged["skill_ranks"][tree][id]=rank;result=NodeStatus(staged,tree,*node);state.swap(staged);return result;
}
}
