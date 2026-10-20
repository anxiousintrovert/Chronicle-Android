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
Quest ParseQuest(const std::string &id,const Json &definition) {
    Quest quest{id,definition["title"].get<std::string>(),definition["description"].get<std::string>(),0,-1};
    if(definition.contains("goal"))quest.goal=definition["goal"].get<int>();
    else for(const auto &objective:definition["objectives"])quest.goal+=objective["goal"].get<int>();
    return quest;
}
Json Status(const Quest &quest,const Json &state) {
    const int progress=state.contains("quests")?state["quests"].value(quest.id,0):0;
    Json status={{"id",quest.id},{"title",quest.title},{"description",quest.description},
                 {"goal",quest.goal},{"progress",std::min(progress,quest.goal)},{"complete",progress>=quest.goal}};
    if(state.contains("quest_definitions")&&state["quest_definitions"].contains(quest.id)&&state["quest_definitions"][quest.id].contains("objectives")) {
        status["objectives"]=Json::array();
        for(const auto &objective:state["quest_definitions"][quest.id]["objectives"]) {
            const auto id=objective["id"].get<std::string>();
            const int count=state.contains("quest_objectives")&&state["quest_objectives"].contains(quest.id)?state["quest_objectives"][quest.id].value(id,0):0;
            auto value=objective;value["progress"]=count;value["complete"]=count>=objective["goal"].get<int>();status["objectives"].push_back(std::move(value));
        }
    }
    return status;
}
}
Json QuestStatusJson(const Quest &quest,const Json &state){return Status(quest,state);}
std::string ValidateQuestOperation(std::string_view operation,const Json &a) {
    if(!a.is_object())return "quest arguments must be an object";
    if(operation=="quests.list")return a.empty()?"":"quests.list takes no arguments";
    if(!a.contains("id")||!Id(a["id"]))return "invalid quest id";
    if(operation=="quests.get")return a.size()==1?"":"provide only quest id";
    if(operation=="quests.register") {
        if(a.size()!=4||!a.contains("title")||!Text(a["title"])||!a.contains("description")||!Text(a["description"]))
            return "quest registration requires only id, bounded title/description, and goal or objectives";
        if(a.contains("goal"))return a["goal"].is_number_integer()&&a["goal"]>=1&&a["goal"]<=1000000?"":"quest goal must be integer 1..1000000";
        if(!a.contains("objectives")||!a["objectives"].is_array()||a["objectives"].empty()||a["objectives"].size()>16)return "quest requires 1..16 objectives";
        std::set<std::string> ids;int total=0;
        for(const auto &objective:a["objectives"]) {
            if(!objective.is_object()||objective.size()!=3||!objective.contains("id")||!Id(objective["id"])||!ids.insert(objective["id"].get<std::string>()).second||
               !objective.contains("title")||!Text(objective["title"])||!objective.contains("goal")||!objective["goal"].is_number_integer()||objective["goal"]<1||objective["goal"]>1000000)
                return "objectives require unique id, bounded title and integer goal 1..1000000";
            total+=objective["goal"].get<int>();if(total>1000000)return "combined objective goals exceed 1000000";
        }
        return {};
    }
    if(operation=="quests.advance") {
        if((a.size()!=2&&a.size()!=3)||!a.contains("amount")||!a["amount"].is_number_integer()||a["amount"]<0||a["amount"]>1000000||
           (a.size()==3&&(!a.contains("objective")||!Id(a["objective"]))))return "quest advance requires id, integer amount 0..1000000 and optional objective id";
        return {};
    }
    return "unsupported quest operation";
}
std::vector<Quest> QuestDefinitions(const Package &package,const Json &state) {
    auto result=package.quests;
    if(state.contains("quest_definitions")) {
        const auto &definitions=state["quest_definitions"];
        Require(definitions.is_object()&&definitions.size()+result.size()<=64,"at most 64 combined manifest and dynamic quests per owner");
        for(const auto &[id,value]:definitions.items()) {
            Require(value.is_object()&&value.size()==3&&!value.contains("id"),"invalid saved quest definition");
            auto arguments=value;arguments["id"]=id;
            const auto error=ValidateQuestOperation("quests.register",arguments);if(!error.empty())throw std::runtime_error(error);
            Require(std::ranges::none_of(result,[&](const Quest &quest){return quest.id==id;}),"saved dynamic quest collides with a manifest quest");
            result.push_back(ParseQuest(id,value));
        }
    }
    if(state.contains("quest_objectives")) {
        const auto &progress=state["quest_objectives"];
        Require(progress.is_object()&&progress.size()<=64,"invalid saved objective progress");
        for(const auto &[id,counts]:progress.items()) {
            Require(state.contains("quest_definitions")&&state["quest_definitions"].contains(id)&&state["quest_definitions"][id].contains("objectives"),"objective progress requires a multi-objective definition");
            Require(counts.is_object()&&counts.size()<=16,"invalid objective counts");
            const auto &definitions=state["quest_definitions"][id]["objectives"];
            for(const auto &[key,value]:counts.items()) {
                const auto found=std::ranges::find_if(definitions,[&](const Json &objective){return objective["id"]==key;});
                Require(found!=definitions.end()&&value.is_number_integer()&&value>=0&&value<=(*found)["goal"],"unknown or out-of-range objective count");
            }
        }
    }
    if(state.contains("quest_definitions"))for(const auto &[id,definition]:state["quest_definitions"].items())if(definition.contains("objectives")) {
        int total=0;
        if(state.contains("quest_objectives")&&state["quest_objectives"].contains(id))for(const auto &count:state["quest_objectives"][id])total+=count.get<int>();
        const auto aggregate=state.contains("quests")?state["quests"].value(id,Json(0)):Json(0);
        Require(aggregate.is_number_integer()&&aggregate==total,"quest aggregate differs from objective progress");
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
        const auto quest=ParseQuest(id,arguments);
        QuestDefinitions(package,staged);
        const auto result=Status(quest,staged);state.swap(staged);return result;
    }
    Require(found!=quests.end(),"quest is not declared or registered by this package");
    if(operation=="quests.advance") {
        auto staged=state;
        if(!staged.contains("quests"))staged["quests"]=Json::object();
        Require(staged["quests"].is_object()&&(staged["quests"].contains(id)||staged["quests"].size()<64),"quest progress limit reached");
        const bool multiple=staged.contains("quest_definitions")&&staged["quest_definitions"].contains(id)&&staged["quest_definitions"][id].contains("objectives");
        if(multiple) {
            Require(arguments.contains("objective"),"multi-objective quest advance requires objective id");
            const auto key=arguments["objective"].get<std::string>();const auto &definitions=staged["quest_definitions"][id]["objectives"];
            const auto objective=std::ranges::find_if(definitions,[&](const Json &value){return value["id"]==key;});
            Require(objective!=definitions.end(),"unknown objective for this quest");
            if(!staged.contains("quest_objectives"))staged["quest_objectives"]=Json::object();
            if(!staged["quest_objectives"].contains(id))staged["quest_objectives"][id]=Json::object();
            auto &counts=staged["quest_objectives"][id];
            counts[key]=std::min<long long>((*objective)["goal"].get<int>(),counts.value(key,0)+arguments["amount"].get<long long>());
            int total=0;for(const auto &count:counts)total+=count.get<int>();staged["quests"][id]=total;
        } else {
            Require(!arguments.contains("objective"),"single-objective quest does not accept objective id");
            staged["quests"][id]=std::min<long long>(found->goal,staged["quests"].value(id,0)+arguments["amount"].get<long long>());
        }
        const auto result=Status(*found,staged);state.swap(staged);return result;
    }
    return Status(*found,state);
}
}
