#include "quests.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace mods {
namespace {
using Json=nlohmann::json;
void Require(bool good,const char *message){if(!good)throw std::runtime_error(message);}
bool Id(const Json &value) {
    if(!value.is_string())return false;
    const auto &id=value.get_ref<const std::string &>();
    return !id.empty()&&id.size()<=96&&id.front()>='a'&&id.front()<='z'&&std::ranges::all_of(id,[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-';});
}
bool Text(const Json &value) {
    if(!value.is_string())return false;const auto &text=value.get_ref<const std::string &>();
    return !text.empty()&&text.size()<=256&&std::ranges::all_of(text,[](unsigned char c){return c>=32;});
}
Json Status(const Quest &quest,const Json &state) {
    const int progress=state.contains("quests")?state["quests"].value(quest.id,0):0;
    return {{"id",quest.id},{"title",quest.title},{"description",quest.description},
            {"goal",quest.goal},{"progress",std::min(progress,quest.goal)},{"complete",progress>=quest.goal}};
}
}
std::string ValidateQuestOperation(std::string_view operation,const Json &a) {
    if(!a.is_object())return "quest arguments must be an object";
    if(operation=="quests.list")return a.empty()?"":"quests.list takes no arguments";
    if(!a.contains("id")||!Id(a["id"]))return "invalid quest id";
    if(operation=="quests.get")return a.size()==1?"":"provide only quest id";
    if(operation=="quests.register") {
        if(a.size()!=4||!a.contains("title")||!Text(a["title"])||!a.contains("description")||!Text(a["description"])||
           !a.contains("goal")||!a["goal"].is_number_integer()||a["goal"]<1||a["goal"]>1000000)
            return "quest registration requires only id, bounded title/description and integer goal 1..1000000";
        return {};
    }
    if(operation=="quests.advance") {
        if(a.size()!=2||!a.contains("amount")||!a["amount"].is_number_integer()||a["amount"]<0||a["amount"]>1000000)return "quest advance requires only id and integer amount 0..1000000";
        return {};
    }
    return "unsupported quest operation";
}
std::vector<Quest> QuestDefinitions(const Package &package,const Json &state) {
    auto result=package.quests;
    if(!state.contains("quest_definitions"))return result;
    const auto &definitions=state["quest_definitions"];
    Require(definitions.is_object()&&definitions.size()+result.size()<=64,"at most 64 combined manifest and dynamic quests per owner");
    for(const auto &[id,value]:definitions.items()) {
        Require(value.is_object()&&value.size()==3&&!value.contains("id"),"invalid saved quest definition");
        auto arguments=value;arguments["id"]=id;
        const auto error=ValidateQuestOperation("quests.register",arguments);if(!error.empty())throw std::runtime_error(error);
        Require(std::ranges::none_of(result,[&](const Quest &quest){return quest.id==id;}),"saved dynamic quest collides with a manifest quest");
        result.push_back({id,value["title"].get<std::string>(),value["description"].get<std::string>(),value["goal"].get<int>(),-1});
    }
    return result;
}
Json CallOwnedQuest(const Package &package,Json &state,std::string_view operation,const Json &arguments) {
    const auto error=ValidateQuestOperation(operation,arguments);if(!error.empty())throw std::runtime_error(error);
    auto quests=QuestDefinitions(package,state);
    if(operation=="quests.list") {
        Json entries=Json::array();for(const auto &quest:quests)entries.push_back(Status(quest,state));
        return {{"quests",std::move(entries)}};
    }
    const auto id=arguments.at("id").get<std::string>();
    auto found=std::ranges::find(quests,id,&Quest::id);
    if(operation=="quests.register") {
        auto definition=arguments;definition.erase("id");
        if(found!=quests.end()) {
            Require(state.contains("quest_definitions")&&state["quest_definitions"].contains(id)&&state["quest_definitions"][id]==definition,"quest already exists with a different definition or manifest ownership");
            return Status(*found,state);
        }
        Require(quests.size()<64,"quest definition limit reached");
        auto staged=state;
        if(!staged.contains("quest_definitions"))staged["quest_definitions"]=Json::object();
        staged["quest_definitions"][id]=std::move(definition);
        Quest quest{id,arguments["title"].get<std::string>(),arguments["description"].get<std::string>(),arguments["goal"].get<int>(),-1};
        const auto result=Status(quest,staged);state.swap(staged);return result;
    }
    Require(found!=quests.end(),"quest is not declared or registered by this package");
    if(operation=="quests.advance") {
        auto staged=state;
        if(!staged.contains("quests"))staged["quests"]=Json::object();
        Require(staged["quests"].is_object()&&(staged["quests"].contains(id)||staged["quests"].size()<64),"quest progress limit reached");
        staged["quests"][id]=std::min<long long>(found->goal,staged["quests"].value(id,0)+arguments["amount"].get<long long>());
        const auto result=Status(*found,staged);state.swap(staged);return result;
    }
    return Status(*found,state);
}
}
