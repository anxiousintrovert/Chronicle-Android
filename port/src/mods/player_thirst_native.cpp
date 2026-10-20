// Native calculation with an optional owned tuning hook.
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeonparts.hpp"
#include "itemdata.hpp"
#include "menu_dungeon.hpp"
#include "menu_inventory.hpp"
#include "userstatus.hpp"
#include <cstdio>
#include <cstdlib>
#include "player_tuning_native.hpp"

PORT_OVERRIDE void CUserStatus::Step(int mode) {
    float drain;
    int   dungeon;

    if (this->step_disable != 0) {
        return;
    }

    dungeon = this->cur_georama;
    drain = 1.0f + 0.2f * dungeon;
    drain = 0.003f * drain;

    if (this->water_drain_disable == 0 && mode == 0) {
        if (this->water_now[this->cur_chara] <= 0.0f) {
            this->water_now[this->cur_chara] = 0.0f;
        } else {
            if (this->res_limit_zone_current == RES_LIMIT_ZONE_THIRST) {
                drain = 5.0f * drain;
            }

            if (NowWeaponHave->flags & 0x8) {
                drain *= 0.8f;
            }

            if (NowWeaponHave->flags & 0x10) {
                drain *= 2.0f;
            }

            if (mode == 0) {
                mods::ScalePlayerValue(mods::PlayerTuning::ThirstDrain, drain);
                this->water_now[this->cur_chara] -= drain;
            }

            if (this->water_now[this->cur_chara] <= 0.0f) {
                this->water_now[this->cur_chara] = 0.0f;
            }
        }

        if (this->water_now[this->cur_chara] <= 0.0f) {
            this->thirst_damage[this->cur_chara] += 0.008333334f;
        }

        if (this->thirst_damage[this->cur_chara] >= 1.0f) {
            this->thirst_damage[this->cur_chara] = 0.0f;
            this->AddNowLife(this->cur_chara, -1, 10.0f);
        }
    }

    for (int i = 0; i < 6; i++) {
        if (this->drink_step[i] != 0) {
            this->water_now[i] += this->drink_step[i];

            if (this->drink_step[i] < 0) {
                if (this->water_now[i] <= this->drink_next[i]) {
                    this->water_now[i] = this->drink_next[i];
                    this->drink_step[i] = 0;
                }
            }

            if (this->drink_step[i] > 0) {
                if (this->water_now[i] >= this->drink_next[i]) {
                    this->water_now[i] = this->drink_next[i];
                    this->drink_step[i] = 0;
                }
            }
        }
    }

    for (int i = 0; i < 6; i++) {
        if (this->life_step[i] != 0) {
            this->hp[i] += this->life_step[i];

            if (this->life_step[i] < 0) {
                if (this->hp[i] <= this->next_hp[i]) {
                    this->hp[i] = this->next_hp[i];
                    this->life_step[i] = 0;
                }
            }

            if (this->life_step[i] > 0) {
                if (this->hp[i] >= this->next_hp[i]) {
                    this->hp[i] = this->next_hp[i];
                    this->life_step[i] = 0;
                }
            }
        }
    }
}
