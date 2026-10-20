#include "combat_damage.hpp"
#include "userstatus.hpp"

PORT_OVERRIDE void CUserStatus::AddNowLife(int chara_no, s16 amount, float ratio) {
    if (mods::ModifyIncomingDamage(this, chara_no, amount)) return;
    int valid;

    if (this->life_step[chara_no] != 0) {
        this->hp[chara_no] = this->next_hp[chara_no];
        this->life_step[chara_no] = 0;
    }

    if (0.0f == ratio) {
        this->hp[chara_no] = this->hp[chara_no] + amount;

        if (this->hp[chara_no] <= 0) {
            this->hp[chara_no] = 0;
        }

        {
            s16 life_value = this->hp[chara_no];

            if (!(valid = life_value < this->max_hp[chara_no])) {
                this->hp[chara_no] = this->max_hp[chara_no];
            }
        }
    } else {
        this->next_hp[chara_no] = this->hp[chara_no] + amount;

        if (this->next_hp[chara_no] <= 0) {
            this->next_hp[chara_no] = 0;
        }

        {
            s16 life_value = this->next_hp[chara_no];

            if (!(valid = life_value < this->max_hp[chara_no])) {
                this->next_hp[chara_no] = this->max_hp[chara_no];
            }
        }

        this->life_step[chara_no] = ratio * ((float) (this->next_hp[chara_no] - this->hp[chara_no]) / 100.0f);

        if (this->life_step[chara_no] == 0) {
            if (this->next_hp[chara_no] - this->hp[chara_no] < 0) {
                this->life_step[chara_no] = -1;
            } else {
                this->life_step[chara_no] = 1;
            }
        }
    }
}
