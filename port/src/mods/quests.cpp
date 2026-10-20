#include "quests.hpp"
#include "api.hpp"
#include <algorithm>
#include <cmath>
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
bool ExplicitStart(const std::string &id,const Json &state) {
    return state.contains("quest_definitions")&&state["quest_definitions"].contains(id)&&state["quest_definitions"][id].value("explicit_start",false);
}
bool Failed(const std::string &id,const Json &state) {return state.contains("quest_failed")&&state["quest_failed"].contains(id);}
bool Started(const std::string &id,const Json &state) {
    return !ExplicitStart(id,state)||(state.contains("quest_started")&&state["quest_started"].value(id,false));
}
int ObjectiveCount(const Json &state,const std::string &quest,const std::string &objective) {
    return state.contains("quest_objectives")&&state["quest_objectives"].contains(quest)?state["quest_objectives"][quest].value(objective,0):0;
}
Json BlockedBy(const Json &definition,const Json &objective,const Json &state,const std::string &quest) {
    Json pending=Json::array();
    if(objective.contains("after"))for(const auto &required:objective["after"]) {
        const auto found=std::ranges::find_if(definition["objectives"],[&](const Json &item){return item["id"]==required;});
        if(found==definition["objectives"].end()||ObjectiveCount(state,quest,required.get<std::string>())<(*found)["goal"].get<int>())pending.push_back(required);
    }
    return pending;
}
Json Status(const Quest &quest,const Json &state) {
    const int progress=state.contains("quests")?state["quests"].value(quest.id,0):0;
    Json status={{"id",quest.id},{"title",quest.title},{"description",quest.description},
                 {"goal",quest.goal},{"progress",std::min(progress,quest.goal)},{"complete",progress>=quest.goal}};
    status["status"]=progress>=quest.goal?"completed":Failed(quest.id,state)?"failed":Started(quest.id,state)?"active":"registered";
    if(Failed(quest.id,state))status["failure_reason"]=state["quest_failed"][quest.id];
    if(state.contains("quest_definitions")&&state["quest_definitions"].contains(quest.id)&&state["quest_definitions"][quest.id].contains("objectives")) {
        status["objectives"]=Json::array();
        for(const auto &objective:state["quest_definitions"][quest.id]["objectives"]) {
            const auto id=objective["id"].get<std::string>();
            const int count=state.contains("quest_objectives")&&state["quest_objectives"].contains(quest.id)?state["quest_objectives"][quest.id].value(id,0):0;
            auto value=objective;value["progress"]=count;value["complete"]=count>=objective["goal"].get<int>();
            value["blocked_by"]=BlockedBy(state["quest_definitions"][quest.id],objective,state,quest.id);
            value["available"]=Started(quest.id,state)&&!Failed(quest.id,state)&&!value["complete"].get<bool>()&&value["blocked_by"].empty();
            status["objectives"].push_back(std::move(value));
        }
    }
    if(state.contains("quest_definitions")&&state["quest_definitions"].contains(quest.id)&&state["quest_definitions"][quest.id].contains("rewards")) {
        status["rewards"]=state["quest_definitions"][quest.id]["rewards"];
        status["rewards_granted"]=state.contains("quest_rewarded")&&state["quest_rewarded"].value(quest.id,false);
    }
    return status;
}
}
Json QuestStatusJson(const Quest &quest,const Json &state){return Status(quest,state);}
std::string ValidateQuestOperation(std::string_view operation,const Json &a) {
    if(!a.is_object())return "quest arguments must be an object";
    if(operation=="quests.list")return a.empty()?"":"quests.list takes no arguments";
    if(!a.contains("id")||!Id(a["id"]))return "invalid quest id";
    if(operation=="quests.get"||operation=="quests.start"||operation=="quests.resume")return a.size()==1?"":"provide only quest id";
    if(operation=="quests.fail")return a.size()==2&&a.contains("reason")&&Text(a["reason"])?"":"quest failure requires id and a nonempty reason up to 256 bytes";
    if(operation=="quests.register") {
        const auto fields=4u+unsigned(a.contains("explicit_start"))+unsigned(a.contains("rewards"));
        if(a.size()!=fields||(a.contains("explicit_start")&&!a["explicit_start"].is_boolean())||
           !a.contains("title")||!Text(a["title"])||!a.contains("description")||!Text(a["description"]))
            return "quest requires id, title, description, goal or objectives; optional explicit_start and rewards";
        if(a.contains("rewards")) {
            if(!a["rewards"].is_array()||a["rewards"].empty()||a["rewards"].size()>16)return "rewards require 1..16 grants";
            for(const auto &reward:a["rewards"]) {
                if(!reward.is_object()||reward.size()!=2||!reward.contains("operation")||!reward["operation"].is_string()||!reward.contains("arguments"))return "reward requires operation and arguments";
                const auto operation=reward["operation"].get<std::string>();
                if(operation!="economy.add"&&operation!="inventory.give"&&operation!="weapons.give")return "unsupported quest reward operation";
                const auto error=ValidateOperation(operation,reward["arguments"]);if(!error.empty())return error;
                if(operation=="economy.add"&&reward["arguments"]["amount"]<=0)return "currency reward must be positive";
            }
        }
        if(a.contains("goal")&&a.contains("objectives"))return "choose goal or objectives";
        if(a.contains("goal"))return a["goal"].is_number_integer()&&a["goal"]>=1&&a["goal"]<=1000000?"":"quest goal must be integer 1..1000000";
        if(!a.contains("objectives")||!a["objectives"].is_array()||a["objectives"].empty()||a["objectives"].size()>16)return "quest requires 1..16 objectives";
        std::set<std::string> ids;int total=0;
        for(const auto &objective:a["objectives"]) {
            if(!objective.is_object()||objective.size()!=3u+unsigned(objective.contains("event"))+unsigned(objective.contains("match"))+unsigned(objective.contains("after"))+unsigned(objective.contains("owner"))+unsigned(objective.contains("name"))+unsigned(objective.contains("payload_match"))||!objective.contains("id")||!Id(objective["id"])||!ids.insert(objective["id"].get<std::string>()).second||
               !objective.contains("title")||!Text(objective["title"])||!objective.contains("goal")||!objective["goal"].is_number_integer()||objective["goal"]<1||objective["goal"]>1000000)
                return "objectives require unique id, bounded title and integer goal 1..1000000";
            if(objective.contains("after")) {
                const auto &after=objective["after"];std::set<std::string> prerequisites;
                if(!after.is_array()||after.empty()||after.size()>15)return "after requires 1..15 earlier objective ids";
                for(const auto &required:after) {
                    if(!Id(required)||required==objective["id"]||!ids.contains(required.get<std::string>())||!prerequisites.insert(required.get<std::string>()).second)
                        return "objective prerequisites must be unique earlier objective ids";
                }
            }
            if(objective.contains("event")) {
                if(!objective["event"].is_string()||(objective["event"]!="enemy_killed"&&objective["event"]!="map_enter"&&objective["event"]!="floor_enter"&&objective["event"]!="custom"))return "unsupported quest objective event";
            }
            if(objective.value("event",Json())=="custom") {
                if(!objective.contains("owner")||!Id(objective["owner"])||!objective.contains("name"))return "custom objective requires provider owner and event name";
                const auto error=ValidateOperation("events.emit",{{"name",objective["name"]},{"payload",Json::object()}});if(!error.empty())return error;
                if(objective.contains("payload_match")) {
                    const auto &match=objective["payload_match"];
                    if(!match.is_object()||match.size()>8)return "payload_match requires at most eight scalar fields";
                    for(const auto &[key,value]:match.items()) {
                        if(key.empty()||key.size()>64||!Text(Json(key)))return "invalid custom payload match key";
                        if(!(value.is_null()||value.is_boolean()||(value.is_number()&&std::isfinite(value.get<double>()))||(value.is_string()&&value.get_ref<const std::string &>().size()<=256)))return "payload match values must be bounded scalars";
                    }
                }
            } else if(objective.contains("owner")||objective.contains("name")||objective.contains("payload_match"))return "provider/name/payload_match require a custom event";
            if(objective.contains("match")) {
                const auto &match=objective["match"];
                if(!objective.contains("event")||!match.is_object()||match.size()>8)return "event match requires event and at most eight fields";
                for(const auto &[key,value]:match.items()) {
                    const bool common=key=="map"||key=="dungeon"||key=="floor";
                    const bool death=objective["event"]=="enemy_killed"&&(key=="attacker"||key=="reward_exp"||key=="reward_money"||key=="damage"||key=="source");
                    if(!common&&!death)return "unknown objective event match field";
                    if(key=="source") {if(value!="native"&&value!="scripted")return "event source must be native or scripted";}
                    else if(!value.is_number_integer()||value < -1||value > 2147483647)return "event match must be a bounded integer";
                }
            }
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
            Require(value.is_object()&&(value.size()>=3&&value.size()<=5)&&!value.contains("id"),"invalid saved quest definition");
            auto arguments=value;arguments["id"]=id;
            const auto error=ValidateQuestOperation("quests.register",arguments);if(!error.empty())throw std::runtime_error(error);
            Require(std::ranges::none_of(result,[&](const Quest &quest){return quest.id==id;}),"saved dynamic quest collides with a manifest quest");
            if(!package.id.empty()&&value.contains("objectives"))for(const auto &objective:value["objectives"])if(objective.value("event",Json())=="custom") {
                const auto owner=objective["owner"].get<std::string>();
                Require(owner==package.id||std::ranges::find(package.dependencies,owner)!=package.dependencies.end(),"custom quest event provider must be self or a declared dependency");
            }
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
        for(const auto &objective:definition["objectives"])
            Require(ObjectiveCount(state,id,objective["id"].get<std::string>())==0||BlockedBy(definition,objective,state,id).empty(),"locked objective has saved progress");
    }
    if(state.contains("quest_started")) {
        Require(state["quest_started"].is_object()&&state["quest_started"].size()<=64,"invalid quest start records");
        for(const auto &[id,started]:state["quest_started"].items())
            Require(ExplicitStart(id,state)&&started.is_boolean()&&started.get<bool>(),"invalid or orphan quest start record");
    }
    for(const auto &quest:result)if(!Started(quest.id,state))
        Require(!state.contains("quests")||state["quests"].value(quest.id,Json(0))==0,"unstarted quest has progress");
    if(state.contains("quest_failed")) {
        Require(state["quest_failed"].is_object()&&state["quest_failed"].size()<=64,"invalid quest failure records");
        for(const auto &[id,reason]:state["quest_failed"].items()) {
            const auto found=std::ranges::find(result,id,&Quest::id);
            Require(state.contains("quest_definitions")&&state["quest_definitions"].contains(id)&&found!=result.end()&&Text(reason),"invalid or orphan quest failure record");
            Require(Started(id,state)&&(!state.contains("quests")||state["quests"].value(id,0)<found->goal),"only active incomplete quests can fail");
        }
    }
    if(state.contains("quest_rewarded")) {
        Require(state["quest_rewarded"].is_object()&&state["quest_rewarded"].size()<=64,"invalid reward records");
        for(const auto &[id,claimed]:state["quest_rewarded"].items())
            Require(state.contains("quest_definitions")&&state["quest_definitions"].contains(id)&&state["quest_definitions"][id].contains("rewards")&&claimed.is_boolean()&&claimed.get<bool>(),"invalid or orphan reward record");
    }
    for(const auto &quest:result)if(state.contains("quest_definitions")&&state["quest_definitions"].contains(quest.id)&&state["quest_definitions"][quest.id].contains("rewards")) {
        const bool complete=state.contains("quests")&&state["quests"].value(quest.id,0)>=quest.goal;
        const bool claimed=state.contains("quest_rewarded")&&state["quest_rewarded"].value(quest.id,false);
        Require(complete==claimed,"reward record differs from quest completion");
    }
    return result;
}
bool WantsQuestEvent(const Json &state,std::string_view event) {
    if(!state.contains("quest_definitions"))return false;
    for(const auto &[id,definition]:state["quest_definitions"].items()) {
        if(!definition.contains("objectives")||!Started(id,state)||Failed(id,state))continue;
        for(const auto &objective:definition["objectives"])if(objective.value("event",std::string{})==event) {
            const int count=state.contains("quest_objectives")&&state["quest_objectives"].contains(id)?state["quest_objectives"][id].value(objective["id"].get<std::string>(),0):0;
            if(count<objective["goal"].get<int>()&&BlockedBy(definition,objective,state,id).empty())return true;
        }
    }
    return false;
}
std::vector<Json> QuestEventAdvances(const Package &package,const Json &state,std::string_view event,const Json &payload) {
    QuestDefinitions(package,state);std::vector<Json> advances;
    if(!state.contains("quest_definitions"))return advances;
    for(const auto &[id,definition]:state["quest_definitions"].items()) {
        if(!definition.contains("objectives")||!Started(id,state)||Failed(id,state))continue;
        for(const auto &objective:definition["objectives"]) {
            if(objective.value("event",std::string{})!=event)continue;
            const int count=state.contains("quest_objectives")&&state["quest_objectives"].contains(id)?state["quest_objectives"][id].value(objective["id"].get<std::string>(),0):0;
            if(count>=objective["goal"].get<int>()||!BlockedBy(definition,objective,state,id).empty())continue;
            if(event=="custom") {
                if(!payload.contains("owner")||payload["owner"]!=objective["owner"]||!payload.contains("name")||payload["name"]!=objective["name"])continue;
                if(objective.contains("payload_match")) {
                    bool matches=payload.contains("payload")&&payload["payload"].is_object();
                    if(matches)for(const auto &[key,value]:objective["payload_match"].items())if(!payload["payload"].contains(key)||payload["payload"][key]!=value){matches=false;break;}
                    if(!matches)continue;
                }
            }
            bool matches=true;if(objective.contains("match"))for(const auto &[key,value]:objective["match"].items())if(!payload.contains(key)||payload[key]!=value){matches=false;break;}
            if(matches){Require(advances.size()<64,"quest event objective budget (64) exceeded");advances.push_back({{"id",id},{"objective",objective["id"]},{"amount",1}});}
        }
    }
    return advances;
}
Json CallOwnedQuest(const Package &package,Json &state,std::string_view operation,const Json &arguments,const std::function<void(const Json &)> &enqueue_rewards) {
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
    if(operation=="quests.fail"||operation=="quests.resume") {
        Require(state.contains("quest_definitions")&&state["quest_definitions"].contains(id),"failure lifecycle requires an owned dynamic quest");
        Require(Started(id,state)&&!Status(*found,state)["complete"].get<bool>(),"failure lifecycle requires a started incomplete quest");
        auto staged=state;
        if(operation=="quests.fail") {
            Require(!Failed(id,state)||state["quest_failed"][id]==arguments["reason"],"quest already failed with another reason");
            staged["quest_failed"][id]=arguments["reason"];
        } else {
            Require(Failed(id,state),"quest is not failed");staged["quest_failed"].erase(id);
            if(staged["quest_failed"].empty())staged.erase("quest_failed");
        }
        const auto result=Status(*found,staged);state.swap(staged);return result;
    }
    if(operation=="quests.start") {
        Require(!Failed(id,state),"failed quest requires quests.resume");
        Require(!Status(*found,state)["complete"].get<bool>(),"completed quest cannot be restarted");
        auto staged=state;
        if(ExplicitStart(id,staged))staged["quest_started"][id]=true;
        const auto result=Status(*found,staged);state.swap(staged);return result;
    }
    if(operation=="quests.advance") {
        Require(Started(id,state),"quest must be started before advancing");
        Require(!Failed(id,state),"failed quest cannot advance");
        auto staged=state;
        if(!staged.contains("quests"))staged["quests"]=Json::object();
        Require(staged["quests"].is_object()&&(staged["quests"].contains(id)||staged["quests"].size()<64),"quest progress limit reached");
        const bool multiple=staged.contains("quest_definitions")&&staged["quest_definitions"].contains(id)&&staged["quest_definitions"][id].contains("objectives");
        if(multiple) {
            Require(arguments.contains("objective"),"multi-objective quest advance requires objective id");
            const auto key=arguments["objective"].get<std::string>();const auto &definitions=staged["quest_definitions"][id]["objectives"];
            const auto objective=std::ranges::find_if(definitions,[&](const Json &value){return value["id"]==key;});
            Require(objective!=definitions.end(),"unknown objective for this quest");
            Require(BlockedBy(staged["quest_definitions"][id],*objective,staged,id).empty(),"objective prerequisites are not complete");
            if(!staged.contains("quest_objectives"))staged["quest_objectives"]=Json::object();
            if(!staged["quest_objectives"].contains(id))staged["quest_objectives"][id]=Json::object();
            auto &counts=staged["quest_objectives"][id];
            counts[key]=std::min<long long>((*objective)["goal"].get<int>(),counts.value(key,0)+arguments["amount"].get<long long>());
            int total=0;for(const auto &count:counts)total+=count.get<int>();staged["quests"][id]=total;
        } else {
            Require(!arguments.contains("objective"),"single-objective quest does not accept objective id");
            staged["quests"][id]=std::min<long long>(found->goal,staged["quests"].value(id,0)+arguments["amount"].get<long long>());
        }
        if(staged.contains("quest_definitions")&&staged["quest_definitions"].contains(id)&&staged["quest_definitions"][id].contains("rewards")&&
           staged["quests"][id].get<int>()>=found->goal&&!(staged.contains("quest_rewarded")&&staged["quest_rewarded"].value(id,false))) {
            Require(bool(enqueue_rewards),"quest reward write provider unavailable");
            enqueue_rewards(staged["quest_definitions"][id]["rewards"]);
            staged["quest_rewarded"][id]=true;
        }
        const auto result=Status(*found,staged);state.swap(staged);return result;
    }
    return Status(*found,state);
}
}
