#include "save_migrations.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace mods {
namespace {
using Json=nlohmann::json;
void Require(bool good,const char *message){if(!good)throw std::runtime_error(message);}
bool Key(std::string_view key,bool quest=false) {
    if(key.empty()||key.size()>(quest?96u:64u))return false;
    if(quest&&(key.front()<'a'||key.front()>'z'))return false;
    return std::ranges::all_of(key,[quest](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||(quest&&c=='-');});
}
void Rename(Json &state,const char *field,const Json &renames) {
    if(!state.contains(field))return;
    const auto original=state.at(field);
    Require(original.is_object(),"migration source state is not an object");
    auto result=original;
    for(const auto &[from,to]:renames.items()) {
        if(!original.contains(from))continue;
        const auto target=to.get<std::string>();
        // Simultaneous renames allow swaps, but never overwrite an unrelated value.
        Require(!original.contains(target)||renames.contains(target),"save migration rename collides with existing state");
        result.erase(from);
    }
    for(const auto &[from,to]:renames.items())if(original.contains(from))result[to.get<std::string>()]=original.at(from);
    state[field]=std::move(result);
}
}
void ValidateSaveMigrations(const Json &rules,std::string_view current_version) {
    Require(rules.is_array()&&rules.size()<=16,"save_migrations must contain at most 16 direct migrations");
    std::set<std::string> versions;
    for(const auto &rule:rules) {
        Require(rule.is_object(),"save migration must be an object");
        for(const auto &[key,value]:rule.items())Require(key=="from"||key=="rename_values"||key=="rename_quests"||key=="remove_values"||key=="defaults","unknown save migration field");
        Require(rule.contains("from")&&rule["from"].is_string(),"save migration requires exact from version");
        const auto from=rule["from"].get<std::string>();
        Require(!from.empty()&&from.size()<=256&&std::ranges::all_of(from,[](unsigned char c){return c>=32;})&&from!=current_version&&versions.insert(from).second,"invalid, current, or duplicate save migration version");
        std::set<std::string> value_names;
        for(const char *field:{"rename_values","rename_quests"})if(rule.contains(field)) {
            const auto &map=rule[field];const bool quest=std::string_view(field)=="rename_quests";
            Require(map.is_object()&&map.size()<=(quest?64u:128u),"invalid save migration rename map");
            std::set<std::string> destinations;
            for(const auto &[source,target]:map.items()) {
                Require(Key(source,quest)&&target.is_string(),"invalid save migration rename key");
                const auto name=target.get<std::string>();
                Require(Key(name,quest)&&name!=source&&destinations.insert(name).second,"invalid or duplicate save migration destination");
                if(!quest){value_names.insert(source);value_names.insert(name);}
            }
        }
        if(rule.contains("remove_values")) {
            const auto &remove=rule["remove_values"];
            Require(remove.is_array()&&remove.size()<=128,"invalid save migration removal list");
            for(const auto &key:remove)Require(key.is_string()&&Key(key.get_ref<const std::string &>())&&value_names.insert(key.get<std::string>()).second,"invalid, repeated, or renamed migration removal key");
        }
        if(rule.contains("defaults")) {
            const auto &defaults=rule["defaults"];
            Require(defaults.is_object()&&defaults.size()<=128,"invalid save migration defaults");
            for(const auto &[key,value]:defaults.items()) {
                Require(Key(key),"invalid save migration default key");
                Require(value.is_boolean()||(value.is_number()&&std::isfinite(value.get<double>()))||(value.is_string()&&value.get_ref<const std::string &>().size()<=256),"save migration defaults must be bounded scalar values");
                if(rule.contains("remove_values"))Require(std::ranges::find(rule["remove_values"],Json(key))==rule["remove_values"].end(),"save migration cannot remove and default the same key");
            }
        }
    }
}
bool MigrateSavedState(Json &state,const Json &rules,std::string_view from_version) {
    for(const auto &rule:rules)if(rule.at("from").get_ref<const std::string &>()==from_version) {
        auto staged=state;
        Require(staged.is_object(),"invalid migration owner state");
        for(const auto &[field,operation]:{std::pair{"values","rename_values"},std::pair{"quests","rename_quests"}})
            if(rule.contains(operation))Rename(staged,field,rule[operation]);
        if(rule.contains("rename_quests")){Rename(staged,"quest_definitions",rule["rename_quests"]);Rename(staged,"quest_objectives",rule["rename_quests"]);}
        if(rule.contains("remove_values")&&staged.contains("values"))for(const auto &key:rule["remove_values"])staged["values"].erase(key.get<std::string>());
        if(rule.contains("defaults")&&!rule["defaults"].empty()) {
            if(!staged.contains("values"))staged["values"]=Json::object();
            Require(staged["values"].is_object(),"invalid migration values");
            for(const auto &[key,value]:rule["defaults"].items())if(!staged["values"].contains(key))staged["values"][key]=value;
        }
        Require(!staged.contains("values")||(staged["values"].is_object()&&staged["values"].size()<=128),"migrated values exceed save limits");
        Require(!staged.contains("quests")||(staged["quests"].is_object()&&staged["quests"].size()<=64),"migrated quests exceed save limits");
        state.swap(staged);return true;
    }
    return false;
}
}
