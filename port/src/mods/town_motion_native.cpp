// Native lookup and shadow stepping with scoped town motion resolution only.
// Keep the native statements unchanged; tools/tests/test_town_motion.py checks this.
#include "character.hpp"
#include "frame.hpp"
#include "mglib.hpp"
#include "town_motion.hpp"

PORT_OVERRIDE tagMOTION_TYPE *CCharacter::GetMotionParam(int motion_no, int *out_index, int *out_start, int *out_end, int *out_set) {
    int             index;
    int             i;
    tagMOTION_TYPE *found;

    motion_no = mods::ResolveTownMotion(this, motion_no);
    index = -1;
    found = NULL;

    // The motion numbers of the sets run one after another, so the set that
    // holds the number is the one whose range covers it.
    for (i = 0; i < CHARA_MOTION_MAX; i++) {
        if (this->motion[i] != NULL) {
            if (this->motion_start[i] > motion_no) {
                continue;
            }

            if (!(motion_no < this->motion_end[i])) {
                continue;
            }

            index = motion_no - this->motion_start[i];
            found = this->motion[i];

            if (out_start != NULL) {
                *out_start = this->motion_start[i];
            }

            if (out_end != NULL) {
                *out_end = this->motion_end[i];
            }

            if (out_set != NULL) {
                *out_set = i;
            }

            break;
        }
    }

    if (index < 0) {
        return NULL;
    }

    *out_index = index;
    return found;
}

PORT_OVERRIDE void CCharacter::ShadowStep() {
    int             index;
    int             i;
    tagMOTION_TYPE *motion;
    tagMOTION_TYPE *shadow;
    int             motion_no;
    int             start;
    float           speed;

    motion_no = this->motion_no;
    motion_no = mods::ResolveTownMotion(this, motion_no);

    if (motion_no < 0) {
        return;
    }

    index = -1;
    shadow = NULL;

    for (i = 0; i < CHARA_MOTION_MAX; i++) {
        if (this->motion[i] == NULL) {
            continue;
        }

        start = this->motion_start[i];

        if (start > motion_no) {
            continue;
        }

        if (motion_no >= this->motion_end[i]) {
            continue;
        }

        index = motion_no - start;
        shadow = this->shadow_motion[i];
        motion = this->motion[i];
    }

    if (shadow == NULL) {
        return;
    }

    if (index < 0) {
        return;
    }

    if (motion->motion_info == NULL || shadow->motion_info == NULL) {
        return;
    }

    speed = motion->motion_info[index].speed;

    if ((this->motion_flags & 1) || MotionStopFlag) {
        shadow->motion_info[index].speed = 0.0f;
    }

    if (this->motion_flags & 2) {
        // The shadow stops on the last frame rather than running past it.
        if (!(shadow->state.time + speed + 0.001f < (float) shadow->motion_info[index].end)) {
            shadow->motion_info[index].speed = 0.0f;
        }
    }

    if (this->shadow_frame == NULL) {
        return;
    }

    // The shadow plays the motion of the model, at the frame the model stands on.
    shadow->state.time = motion->state.time;
    shadow->state.next_frame = motion->state.next_frame;
    shadow->state.motion_no = motion->state.motion_no;
    shadow->state.blend = motion->state.blend;
    shadow->state.blend_step = motion->state.blend_step;
    shadow->state.next_frame = motion->state.next_frame;
    shadow->state.playing_no = motion->state.playing_no;
    shadow->state.blending = motion->state.blending;
    SetMotionEX(this->shadow_frame, shadow, shadow->motion_info, &shadow->state, shadow->frame_info);
    shadow->motion_info[index].speed = speed;
}
