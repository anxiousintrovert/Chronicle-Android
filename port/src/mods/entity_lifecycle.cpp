// Retail lifecycle bodies, with dormant sidecar notifications only.
// Keep the native statements identical to ps2/src/monstorunit.cpp.
#include "monstorunit.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "btactstatus.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "frame.hpp"
#include "framevu1.hpp"
#include "gameutil.hpp"
#include "hitmark.hpp"
#include "hitvalue.hpp"
#include "itemdata.hpp"
#include "mathutil.hpp"
#include "menu_inventory.hpp"
#include "mglib.hpp"
#include "nowload.hpp"
#include "randomitem.hpp"
#include "rect.hpp"
#include "runscript_opcodes.hpp"
#include "savedata.hpp"
#include "shot_effect_pack.hpp"
#include "snd.hpp"
#include "stealitem.hpp"
#include "texture.hpp"
#include "userstatus.hpp"
#include "weaponelement.hpp"

#include "entity_tracking.hpp"
#include "model_identity.hpp"
#include "runtime.hpp"

PORT_OVERRIDE void CMonstorUnit::CleanViewMonstor(int back_floor) {
    mods::EntityUnitCleared(this);
    for (int i = 0; i < SlotLimit(); i++) {
        monster[i].state = -1;
        monster[i].stop_timer = 0;
        monster[i].poison_timer = 0;
        monster[i].anger_timer = 0;
        monster[i].slow_timer = 0;
        monster[i].collision_poly_count = 0;
        monster[i].movement[2] = 0;
        monster[i].movement[1] = 0;
        monster[i].movement[0] = 0;
        monster[i].turn_target[2] = 0;
        monster[i].turn_target[1] = 0;
        monster[i].turn_target[0] = 0;
        monster[i].movement_speed = 0;
        monster[i].turn_speed = 0;
        monster[i].falls = true;
        monster[i].body_radius = 13.0f;
        monster[i].collision_radius = 13.0f;
        monster[i].unk_094 = 0;
        monster[i].invincible_timer = 0;
        monster[i].drop_item = -1;
        monster[i].clip_distance = 300.0f;
        monster[i].collision_off_timer = 0;
        monster[i].shot_effect = -1;
        monster[i].shot_effect2 = -1;
        monster[i].last_hit_damage = -1;
        monster[i].shadow_length = 1.0f;
        monster[i].lockon_frame = 0;
        monster[i].lockon_scale_x = 1.0f;
        monster[i].lockon_scale_y = 1.0f;
        monster[i].lock_range = 120.0f;
        monster[i].lockon_enabled = true;
        monster[i].revealed = -1;
        monster[i].steal_item = -1;
        monster[i].stolen_money = 0;
        monster[i].event_flag2 = -1;
        monster[i].event_flag2_pending = 0;
        monster[i].palette_alpha = 128.0f;
        monster[i].palette_alpha_step = 0;
        monster[i].palette_delay = 0;
        monster[i].palette_cycles = 0;
        monster[i].free_fall = 0;
        monster[i].palette_override_pending = false;
        monster[i].motion_reset_pending = false;
        monster[i].view_held = false;
        monster[i].attachment_count = 0;
        monster[i].shadow_visible = true;
        monster[i].shadow_enabled = true;
        monster[i].knockback_direction[0] = 0;
        monster[i].knockback_direction[1] = 0;
        monster[i].knockback_direction[2] = 0;
        monster[i].knockback_direction[3] = 1;
        monster[i].knockback[0] = 0;
        monster[i].knockback[1] = 0;
        monster[i].knockback[2] = 1;
        script_state[i] = 0;

        if (script[i] != 0) {
            script[i]->used = 0;
        }

        for (int j = 0; j < 16; j++) {
            effect[i].timer[j] = 0;
            effect[i].motion_start[j] = 0;
        }

        for (int j = 0; j < 16; j++) {
            effect2[i].active[j] = 0;
        }

        for (int j = 0; j < 12; j++) {
            effect3[i].frame[j] = 0;
            effect3[i].timer[j] = 0;
            effect3[i].count = 0;
        }

        for (int j = 0; j < 16; j++) {
            sound[i].id[j] = -1;
            sound[i].cooldown[j] = 0;
        }

        sound[i].sequence_id = -1;
        event[i].frame = 0;
        event[i].timer = 0;
        event2[i].frame = 0;
        event2[i].timer = 0;
    }

    back_dungeon = back_floor;
    alive_count = 0;
}

PORT_OVERRIDE int CMonstorUnit::SetupViewMonstor(int model_no, float *position, int event_flag) {
    current_monster = -1;

    for (int i = 0; i < SlotLimit(); i++) {
        if (monster[i].state == -1) {
            current_monster = i;
            break;
        }
    }

    if (current_monster == -1) {
        return 0;
    }

    mods::EntitySlotConstructed(this, current_monster);
    script[current_monster]->Reset();
    BtSetEventScript(&interpreter[current_monster], script_data[model_no], script[current_monster]);
    chara[current_monster][0] = base_chara[model_no][0];
    chara[current_monster][0].motion[0] = &chara[current_monster][0].motion_type;
    chara[current_monster][0].SetPosition(position);
    chara[current_monster][0].SetRotation(0.0f, 0.0f, 0.0f);

    if (UserStatus->cur_georama == 3 && UserStatus->cur_floor == 17 && current_monster == 1) {
        InitBee(chara[1][0].frame, 15);
    }

    monster[current_monster].attachment_count = 0;

    for (int j = 0; j < 3; j++) {
        if (model[model_no].model_name[j + 1][0] != 0) {
            chara[current_monster][j + 1] = base_chara[model_no][j + 1];
            chara[current_monster][j + 1].motion[0] = &chara[current_monster][j + 1].motion_type;
            chara[current_monster][j + 1].frame->SetParent(chara[current_monster][0].frame);
            monster[current_monster].attachment_count++;
        }
    }

    monster[current_monster].state = 1;
    monster[current_monster].base_model = model_no;
    monster[current_monster].max_hp = model[model_no].max_hp;
    monster[current_monster].hp = model[model_no].max_hp;
    monster[current_monster].attachment_kind = model[model_no].attachment_kind;

    for (int j = 0; j < 5; j++) {
        monster[current_monster].attachment_weight[j] = model[model_no].attachment_weight[j];
    }

    monster[current_monster].defense = model[model_no].defense;
    monster[current_monster].hardness = model[model_no].hardness;
    monster[current_monster].money = model[model_no].money;
    monster[current_monster].money_chance = model[model_no].money_chance;
    monster[current_monster].kind = model[model_no].kind;
    monster[current_monster].name_no = model[model_no].name_no;
    monster[current_monster].body_radius = model[model_no].collision_radius;
    monster[current_monster].collision_radius = model[model_no].collision_radius;
    monster[current_monster].shot_effect = model[model_no].shot_effect[0];
    monster[current_monster].shot_effect2 = model[model_no].shot_effect[1];
    monster[current_monster].exp = model[model_no].exp;
    monster[current_monster].steal_item = model[model_no].steal_item;
    monster[current_monster].drops_items = model[model_no].drops_items;
    monster[current_monster].item_damage_rate = model[model_no].item_damage_rate;
    monster[current_monster].status_chance = model[model_no].status_chance;
    monster[current_monster].rare_item = model[model_no].rare_item;
    monster[current_monster].event_flag2 = event_flag;
    monster[current_monster].knockback[2] = model[model_no].knockback_scale;

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 6; j++) {
            effect[current_monster].parameter[i][j] = model[model_no].effect_parameter[j];
        }
    }

    if (model[model_no].attachment_kind == 8) {
        monster[current_monster].revealed = 0;

        switch (model[model_no].kind) {
            case MONSTER_KIND_MIMIC_SMALL:
                NowDngMap->SetMimicEvent(position[0], position[1], position[2], current_monster, TREASURE_BOX_SMALL);
                break;
            case MONSTER_KIND_MIMIC_LARGE:
                NowDngMap->SetMimicEvent(position[0], position[1], position[2], current_monster, TREASURE_BOX_LARGE);
                break;
        }
    } else {
        monster[current_monster].revealed = -1;
    }

    for (int i = 0; i < 3; i++) {
        event_flags[current_monster][i] = 0;
    }

    if (interpreter[current_monster].check_program(1) != 0) {
        interpreter[current_monster].run(1);
    }

    script_state[current_monster] = 0;
    alive_count++;
    return 1;
}

PORT_OVERRIDE int CMonstorUnit::SetupBaseModel(int slot, int model_no, int texture_block, CDataAlloc2<1> *alloc) {
    mods::BeginNativeModelLoad(this, slot);
    MONSTOR_MODEL *description = &MonstorTable[model_no];
    char           filename[64];
    CFrameAttr     attr;
    int            file_size;
    attr.fog_enable = true;
    sprintf(filename, "dun/monstor/%s.chr", description->model_name[0]);
    LoadFile(filename, read_buffer, NULL);
    wait_now_loading_vsync();
    CCharacter *character = &base_chara[slot][0];
    character->InitializeTexAnime(MonsterTexAnim, 320);
    character->LoadPackData3(read_buffer, "info.cfg", alloc, 42, alloc, 1, 0);
    base_chara[slot][0].frame->SetAttr(attr, 1, 64);
    SetFrameAttr(base_chara[slot][0].frame, 1);

    for (int j = 0; j < 3; j++) {
        if (description->model_name[j + 1][0] != 0) {
            sprintf(filename, "dun/monstor/%s.chr", description->model_name[j + 1]);
            LoadFile(filename, read_buffer, NULL);
            wait_now_loading_vsync();
            base_chara[slot][j + 1].LoadPackData(read_buffer, "info.cfg", alloc, alloc);
            base_chara[slot][j + 1].frame->SetAttr(attr, 1, 64);
            SetFrameAttr(base_chara[slot][j + 1].frame, 1);
        }
    }

    sprintf(filename, "dun/monstor/%s.stb", description->script_name);
    LoadFile(filename, read_buffer, &file_size);
    wait_now_loading_vsync();
    script_data[slot] = (char *) (alloc->base + alloc->used * 16);
    alloc->Alloc((((file_size >> 6) + 1) << 6) >> 4);
    memcpy(script_data[slot], read_buffer, file_size);
    memcpy(&model[slot], description, sizeof(MONSTOR_MODEL));
    int count = 2;

    if (description->kind == MONSTER_KIND_NO_LOCK_ON) {
        count = 6;
    }

    if (description->shot_effect[0] != -1) {
        int entry = NowShotEffect->Entry(BtEntryEffectTbl[description->shot_effect[0]], read_buffer, texture_block, alloc, count);

        if (entry == -1) {
            printf("******* ShotEntry Error !!***********\n");
        } else {
            model[slot].shot_effect[0] = entry;
        }
    }

    if (description->shot_effect[1] != -1) {
        int entry = NowShotEffect->Entry(BtEntryEffectTbl[description->shot_effect[1]], read_buffer, texture_block, alloc, count);

        if (entry == -1) {
            printf("******* ShotEntry Error !!***********\n");
        } else {
            model[slot].shot_effect[1] = entry;
        }
    }

    model_count++;
    if (mods::Scripts().HasGameplay()) mods::RecordNativeModelLoad(this, slot, model_no);
    return 1;
}
