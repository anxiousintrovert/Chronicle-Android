#include "dungeonmap.hpp"
#include <libvu0.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "camera.hpp"
#include "character.hpp"
#include "dataset.hpp"
#include "dun/gameloop.hpp"
#include "dungeonparts.hpp"
#include "frame.hpp"
#include "frameattr.hpp"
#include "framevu1.hpp"
#include "gfx/gfx.hpp"
#include "mathutil.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "screen.hpp"
#include "snd.hpp"
#include "texture.hpp"
#include "textureanime.hpp"
#include "userstatus.hpp"

// Retail's, with the cell picked for a character's key door addressed on the whole pointer.

PORT_OVERRIDE void CDungeonMap::BuildCharaSpecialParts() {
    int          list[128];
    int          roll;
    int          num;
    int          floor;
    int          pick;
    MAP_CELL    *cell;
    std::uintptr_t address;

    if (selectMapNo < DUNGEON_DEMON_SHAFT) {
        int zone = UserStatus->res_limit_zone_current;

        // A resurrection zone puts that character's door on the floor and
        // nothing else.
        if (zone >= 0 && zone < 6) {
            if (zone == RES_LIMIT_ZONE_XIAO) {
                if ((int) ((100.0f * (float) rand()) / 2147483648.0f) >= 50) {
                    this->SetCharaDoor(CHARA_TOAN);
                } else {
                    this->SetCharaDoor(CHARA_TOAN);
                }

                return;
            }

            this->SetCharaDoor(zone);
            return;
        }

        // The first floors of the first dungeon never take a special part.
        roll = (int) ((100.0f * (float) rand()) / 2147483648.0f);

        if (selectMapNo == DUNGEON_DIVINE_BEAST_CAVE && UserStatus->cur_floor < 8) {
            roll = 0;
        }

        if (UserStatus->party_size >= 2 && roll > 0x32) {
            num = this->CreatPartsList(list, 0x40, 0, -1);

            if (num > 0) {
                pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                this->cells[list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride].parts_no = MAP_PARTS_KEY_XIAO;
            }
        }

        // Which special part a floor takes depends on the dungeon, how far the
        // party has come and a roll of the dice.
        roll = (int) ((100.0f * (float) rand()) / 2147483648.0f);
        floor = UserStatus->cur_floor;

        switch (selectMapNo) {
            case DUNGEON_WISE_OWL_FOREST:
                if (UserStatus->party_size >= 3 && roll > 0xA && floor >= 9) {
                    num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                        address += (std::uintptr_t) this;
                        cell = &((CDungeonMap *) address)->cells[0];
                        cell->parts_no += MAP_PARTS_KEY_DOOR_GORO_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
                        return;
                    }
                }

                break;
            case DUNGEON_SHIPWRECK:
                if (UserStatus->party_size >= 3 && roll < 0x28) {
                    num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                        address += (std::uintptr_t) this;
                        cell = &((CDungeonMap *) address)->cells[0];
                        cell->parts_no += MAP_PARTS_KEY_DOOR_GORO_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
                        return;
                    }
                }

                if (UserStatus->party_size >= 4 && roll >= 0x28 && floor >= 9) {
                    num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                        address += (std::uintptr_t) this;
                        cell = &((CDungeonMap *) address)->cells[0];
                        cell->parts_no += MAP_PARTS_KEY_DOOR_RUBY_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
                        return;
                    }
                }

                break;
            case DUNGEON_SUN_MOON_TEMPLE:
                if (UserStatus->party_size >= 3 && roll < 0x14) {
                    num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                        address += (std::uintptr_t) this;
                        cell = &((CDungeonMap *) address)->cells[0];
                        cell->parts_no += MAP_PARTS_KEY_DOOR_GORO_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
                        return;
                    }
                }

                if (UserStatus->party_size >= 4 && roll < 0x28) {
                    num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                        address += (std::uintptr_t) this;
                        cell = &((CDungeonMap *) address)->cells[0];
                        cell->parts_no += MAP_PARTS_KEY_DOOR_RUBY_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
                        return;
                    }
                }

                if (UserStatus->party_size >= 5 && roll >= 0x28 && floor >= 9) {
                    num = this->CreatPartsList(list, 0x40, 0, -1);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        this->cells[list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride].parts_no = MAP_PARTS_KEY_UNGAGA;
                        return;
                    }
                }

                break;
            case DUNGEON_MOON_SEA:
                if (UserStatus->party_size >= 3 && roll < 0xA) {
                    num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                        address += (std::uintptr_t) this;
                        cell = &((CDungeonMap *) address)->cells[0];
                        cell->parts_no += MAP_PARTS_KEY_DOOR_GORO_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
                        return;
                    }
                }

                if (UserStatus->party_size >= 4 && roll < 0x14) {
                    num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                        address += (std::uintptr_t) this;
                        cell = &((CDungeonMap *) address)->cells[0];
                        cell->parts_no += MAP_PARTS_KEY_DOOR_RUBY_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
                        return;
                    }
                }

                if (UserStatus->party_size >= 5 && roll < 0x1E) {
                    num = this->CreatPartsList(list, 0x40, 0, -1);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        this->cells[list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride].parts_no = MAP_PARTS_KEY_UNGAGA;
                        return;
                    }
                }

                if (UserStatus->party_size >= 6 && roll > 0x28 && floor >= 8) {
                    num = this->CreatPartsList(list, 0x40, 0, -1);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        this->cells[list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride].parts_no = MAP_PARTS_KEY_OSMOND;
                        return;
                    }
                }

                break;
            case DUNGEON_GALLERY_OF_TIME:
                if (UserStatus->party_size >= 3 && roll < 0xA) {
                    num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                        address += (std::uintptr_t) this;
                        cell = &((CDungeonMap *) address)->cells[0];
                        cell->parts_no += MAP_PARTS_KEY_DOOR_GORO_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
                        return;
                    }
                }

                if (UserStatus->party_size >= 4 && roll < 0x14) {
                    num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                        address += (std::uintptr_t) this;
                        cell = &((CDungeonMap *) address)->cells[0];
                        cell->parts_no += MAP_PARTS_KEY_DOOR_RUBY_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
                        return;
                    }
                }

                if (UserStatus->party_size >= 5 && roll < 0x1E) {
                    num = this->CreatPartsList(list, 0x40, 0, -1);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        this->cells[list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride].parts_no = MAP_PARTS_KEY_UNGAGA;
                        return;
                    }
                }

                if (UserStatus->party_size >= 6 && roll > 0x28) {
                    num = this->CreatPartsList(list, 0x40, 0, -1);

                    if (num > 0) {
                        pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                        this->cells[list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride].parts_no = MAP_PARTS_KEY_OSMOND;
                    }
                }

                break;
        }
    }
}

PORT_OVERRIDE int CDungeonMap::SetCharaDoor(int chara_no) {
    int          list[128];
    int          num;
    int          pick;
    MAP_CELL    *cell;
    std::uintptr_t address;

    num = 0;

    // Each character's door stands on a map part that suits it.
    switch (chara_no) {
        case CHARA_TOAN:
            if (UserStatus->party_size >= 2) {
                num = this->CreatPartsList(list, 0x40, 0, -1);

                if (num > 0) {
                    pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                    this->cells[list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride].parts_no = MAP_PARTS_KEY_XIAO;
                }
            }

            break;
        case CHARA_XIAO:
            num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

            if (num > 0) {
                pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                address += (std::uintptr_t) this;
                cell = &((CDungeonMap *) address)->cells[0];
                cell->parts_no += MAP_PARTS_KEY_DOOR_XIAO_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
            }

            break;
        case CHARA_GORO:
            num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

            if (num > 0) {
                pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                address += (std::uintptr_t) this;
                cell = &((CDungeonMap *) address)->cells[0];
                cell->parts_no += MAP_PARTS_KEY_DOOR_GORO_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
            }

            break;
        case CHARA_RUBY:
            num = this->CreatPartsList(list, 0x40, MAP_PARTS_ROOM_DOOR_NORTH, MAP_PARTS_ROOM_DOOR_WEST);

            if (num > 0) {
                pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                address = (list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride) * sizeof(MAP_CELL);
                address += (std::uintptr_t) this;
                cell = &((CDungeonMap *) address)->cells[0];
                cell->parts_no += MAP_PARTS_KEY_DOOR_RUBY_NORTH - MAP_PARTS_ROOM_DOOR_NORTH;
            }

            break;
        case CHARA_UNGAGA:
            num = this->CreatPartsList(list, 0x40, 0, -1);

            if (num > 0) {
                pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                this->cells[list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride].parts_no = MAP_PARTS_KEY_UNGAGA;
            }

            break;
        case CHARA_OSMOND:
            num = this->CreatPartsList(list, 0x40, 0, -1);

            if (num > 0) {
                pick = (int) (((float) num * (float) rand()) / 2147483648.0f);
                this->cells[list[pick * 2] + list[pick * 2 + 1] * chronicle::dungeon::GridStride].parts_no = MAP_PARTS_KEY_OSMOND;
            }

            break;
    }

    return num;
}

PORT_OVERRIDE void CDungeonMap::DrawMap(CCameraFollow *camera, CFrameVu1 *player) {
    float           cam_pos[4];
    float           cam_ref[4];
    float           view_delta[4];
    float           view[4];
    float           sound_pos[4];
    float           cell_delta[4];
    float           ambient[4];
    float           old_ambient[4];
    ITEM_FREE_AREA *free_area;
    float           volume;
    float           pan;
    float           dist;
    float           delta_x;
    float           nearest;
    float           world_x;
    float           world_z;
    float           delta_z;
    int             sound_no;
    int             row;
    int             col;
    int             cell_no;
    int             npc_no;
    int             area_no;
    int             rect_no;

    ((CCamera *) camera)->GetPos(cam_pos);
    ((CCamera *) camera)->GetRef(cam_ref);
    view_delta[0] = cam_ref[0] - cam_pos[0];
    view_delta[1] = cam_ref[1] - cam_pos[1];
    view_delta[2] = cam_ref[2] - cam_pos[2];
    view_delta[3] = 0.0f;
    sceVu0Normalize(view, view_delta);
    free_area = ItemFreeAreaAll[selectMapNo];

    this->ClearNPC_Cash();
    nearest = 10000.0f;

    for (row = 0; row < this->GridExtent(); row++) {
        for (col = 0; col < this->GridExtent(); col++) {
            world_x = 160.0f * col;
            delta_x = world_x - cam_pos[0];
            world_z = 160.0f * row;
            delta_z = world_z - cam_pos[2];

            cell_delta[0] = delta_x;
            cell_delta[1] = 0.0f;
            cell_delta[2] = delta_z;
            cell_delta[3] = 0.0f;
            dist = DistVector(cell_delta);
            cell_no = col + row * chronicle::dungeon::GridStride;
            this->cells[cell_no].camera_dist = dist;

            if (this->cells[cell_no].parts_no == MAP_PARTS_NONE) {
                this->cells[cell_no].visible = false;
                continue;
            }

            if (this->cells[cell_no].parts_no == MAP_PARTS_KEY_XIAO) {
                if (UserStatus->cur_georama == 2 && dist < nearest) {
                    nearest = dist;
                    sound_pos[0] = world_x;
                    sound_pos[1] = 0.0f;
                    sound_pos[2] = world_z;
                    sound_pos[3] = 1.0f;
                    sound_no = 65;
                }
            }

            if (this->cells[cell_no].parts_no == 75 && dist < nearest) {
                nearest = dist;
                sound_pos[0] = world_x;
                sound_pos[1] = 0.0f;
                sound_pos[2] = world_z;
                sound_pos[3] = 1.0f;
                sound_no = 75;
            }

            bool draw = true;
            if (selectMapNo != DUNGEON_MOON_SEA) {
                if (!(dist < 160.0f * this->draw_dist_scale)) {
                    draw = false;
                } else {
                    cam_ref[0] = delta_x - view_delta[0];
                    cam_ref[1] = 0.0f;
                    cam_ref[2] = delta_z - view_delta[2];
                    cam_ref[3] = 1.0f;
                    sceVu0Normalize(cam_ref, cam_ref);
                    float facing = sceVu0InnerProduct(view, cam_ref);
                    // Fill keeps the game's vertical FOV and reveals more world at the sides.
                    // Treat a cell as in front through the angle from the frame edge to the
                    // visible viewport edge, rather than using the 4:3 center-plane cutoff.
                    float horizontal_margin = 0.0f;
                    gfx::FrameLayout layout = gfx::CurrentFrameLayout();
                    if (layout.aspect == gfx::AspectMode::Fill) {
                        gfx::LogicalRect visible = gfx::VisibleLogicalRect(gfx::kMainTarget);
                        horizontal_margin = std::max(0.0f, visible.w - kScreenWidth) /
                                            kScreenWidth;
                    }
                    float facing_limit = -horizontal_margin / std::hypot(1.0f, horizontal_margin);
                    int georama = UserStatus->cur_georama;
                    if (georama == 5 && this->cells[cell_no].parts_no == MAP_PARTS_URA_ROAD) {
                        facing = 1.0f;
                    }
                    if (facing <= facing_limit && !(dist < 160.0f)) {
                        draw = false;
                    }
                    if (this->cells[cell_no].parts_no >= MAP_PARTS_URA_ENTRANCE_NORTH &&
                        this->cells[cell_no].parts_no <= MAP_PARTS_URA_ENTRANCE_WEST && georama == 5) {
                        draw = true;
                    }
                }
            }
            this->cells[cell_no].visible = draw;
            if (!draw) {
                continue;
            }

            int            direction = this->cells[cell_no].direction;
            CDungeonParts *direction_part = &this->parts[this->cells[cell_no].parts_no];
            direction_part->direction = direction;
            {
                CDungeonParts *position_part = &this->parts[this->cells[cell_no].parts_no];
                position_part->pos[0] = world_x;
                position_part->pos[1] = 0.0f;
                position_part->pos[2] = world_z;
                position_part->pos[3] = 1.0f;
            }

            for (npc_no = 0; npc_no < 4; npc_no++) {
                if (this->npc[npc_no].parts_no == this->cells[cell_no].parts_no &&
                    this->npc[npc_no].draw_num < 16 &&
                    (dist < 160.0f || view[0] * (delta_x - view_delta[0]) +
                                            view[2] * (delta_z - view_delta[2]) > 0.0f)) {
                    this->ReservNPC_Draw(npc_no, world_x, 0.0f, world_z, this->cells[cell_no].direction);
                }
            }

            if (selectMapNo != DUNGEON_MOON_SEA && UserStatus->cur_georama == 4) {
                MGGetAmbient(old_ambient);
                MGGetAmbient(ambient);

                if (dist <= 480.0f) {
                    ambient[3] = 128.0f;
                } else {
                    ambient[3] = 128.0f - (dist - 480.0f);

                    if (ambient[3] < 0.0f) {
                        ambient[3] = 0.0f;
                    }
                }

                MGSetAmbient(ambient);
            }

            this->parts[this->cells[cell_no].parts_no].Draw();

            if (selectMapNo != DUNGEON_MOON_SEA && UserStatus->cur_georama == 4) {
                MGSetAmbient(old_ambient);
            }

            if (DebugStatus[6] != 0) {
                for (area_no = 0; free_area[area_no].parts_no != MAP_PARTS_NONE; area_no++) {
                    if (free_area[area_no].parts_no != this->cells[cell_no].parts_no) {
                        continue;
                    }

                    for (rect_no = 0; rect_no < free_area[area_no].rect_num; rect_no++) {
                        float corner[4][4];
                        int   screen[4][4];
                        int   all_visible;
                        int   corner_no;
                        int   rotation;

                        rotation = (int) (float) free_area[area_no].direction;
                        rotation = rotation + this->cells[cell_no].direction;

                        if (rotation > 3) {
                            rotation -= 4;
                        }

                        all_visible = 1;
                        float radians = (PI * ((4 - rotation) * 90)) / 180.0f;
                        float x[4];
                        float z[4];
                        float height;
                        float left = free_area[area_no].rect[rect_no].x0;
                        x[0] = 10.0f * left;
                        height = 10.0f * free_area[area_no].rect[rect_no].y0;
                        z[0] = 10.0f * free_area[area_no].rect[rect_no].z0;
                        x[3] = 10.0f * free_area[area_no].rect[rect_no].x1;
                        z[3] = 10.0f * free_area[area_no].rect[rect_no].z1;
                        x[1] = x[3];
                        z[1] = z[0];
                        x[2] = x[0];
                        z[2] = z[3];

                        for (corner_no = 0; corner_no < 4; corner_no++) {
                            corner[corner_no][0] = -z[corner_no] * sinf(radians) - x[corner_no] * cosf(radians);
                            corner[corner_no][2] = -x[corner_no] * sinf(radians) + z[corner_no] * cosf(radians);
                            corner[corner_no][0] *= -1.0f;
                            corner[corner_no][0] += world_x;
                            corner[corner_no][1] = 2.0f + height;
                            corner[corner_no][2] += world_z;
                            corner[corner_no][3] = 1.0f;

                            if (MGRotTransPers(screen[corner_no], corner[corner_no], 0) == 0) {
                                all_visible = 0;
                            }
                        }

                        if (all_visible != 0) {
                            setColSprite(Vif1Packet, screen[0], screen[1], screen[2], screen[3], 0x80, 0, 0, 0x40);
                        }
                    }
                }
            }
        }
    }

    if (nearest < 10000.0f) {
        SndGetVolPan(&volume, &pan, sound_pos, 10.0f, 500.0f);
        SndSetSeVolf(sound_no, volume, 0);
        SndSetSePanf(sound_no, pan, 0);
    }
}

PORT_OVERRIDE void CDungeonMap::DrawDummyModel(CCamera *camera) {
    float pos[4];

    if (this->dummy_num == 0) {
        return;
    }

    // A dummy model only draws while the camera is near enough to it.
    camera->GetPos(pos);

    for (int i = 0; i < this->dummy_num; i++) {
        if (this->dummy_frame[this->dummy_model[i]] != NULL) {
            if (DistVector(this->dummy_pos[i], pos) < 160.0f * (1.0f + this->draw_dist_scale)) {
                this->dummy_frame[this->dummy_model[i]]->SetPosition(this->dummy_pos[i]);
                MGDraw(this->dummy_frame[this->dummy_model[i]]);
            }
        }
    }
}

// Moon Sea places Atla across an open field, so distance from the player does not hide them.
PORT_OVERRIDE void CDungeonMap::DrawAtraBoll(float *pos) {
    float draw_pos[4];

    if (this->atra_model == NULL) {
        return;
    }

    for (int i = 0; i < this->atra_num; i++) {
        if (this->atra[i].used != 0 &&
            (selectMapNo == DUNGEON_MOON_SEA ||
             DistVector(this->atra[i].pos, pos) <= 160.0f * this->draw_dist_scale)) {
            sceVu0CopyVector(draw_pos, this->atra[i].pos);
            draw_pos[1] += sinf(this->atra[i].phase);
            this->atra_model->SetPosition(draw_pos);
            MGDraw(this->atra_model);

            if (this->atra[i].phase <= 360.0f) {
                this->atra[i].phase += 0.1f;
            } else {
                this->atra[i].phase = 0.0f;
            }
        }
    }
}

#include "mods/player_tuning_native.hpp"
#include "dungeon_minimap.hpp"

static void DrawExpandedMiniMap(const CDungeonMap &map, float *pos, float angle) {
    const int status = UserStatus->minimap_status;
    if (status < 0 || status >= 3) return;
    int reveal_map = BtEquipMap, reveal_items = BtEquipMasuisyou;
    if (DebugStatus[3]) reveal_map = reveal_items = 1;
    mods::ApplyPlayerMapReveal(reveal_map, reveal_items);
    auto sprites = ExpandedDungeonMiniMap(map, pos, status, reveal_map != 0, reveal_items == 1);
    char tile_name[] = "minimap1", symbol_name[] = "itempack";
    auto *tiles = TexManager.GetTexture(tile_name, -1);
    auto *symbols = TexManager.GetTexture(symbol_name, -1);
    for (const auto &sprite : sprites) {
        if (sprite.kind == DungeonMiniMapKind::Player) {
            set2DSprite(Vif1Packet, symbols, sprite.screen, sprite.atlas, 7, 7, angle);
        } else if (sprite.kind == DungeonMiniMapKind::Tile) {
            set2DSprite(Vif1Packet, tiles, sprite.screen, sprite.atlas,
                        sprite.tone, sprite.tone, sprite.tone, sprite.alpha);
        } else {
            set2DSprite(Vif1Packet, symbols, sprite.screen, sprite.atlas, sprite.alpha);
        }
    }
    pos[0] *= 0.1f;
    pos[2] *= 0.1f;
}

PORT_OVERRIDE void CDungeonMap::DrawMiniMap(float *pos, float angle) {
    if (this->GridExtent() == chronicle::dungeon::GridStride) {
        DrawExpandedMiniMap(*this, pos, angle);
        return;
    }
    int           status;
    unsigned char red;
    int           saved_map;
    int           saved_crystal;
    unsigned char green;
    unsigned char blue;
    int           row;
    int           col;
    int           direction;
    int           map_x;
    int           map_y;
    int           cellx;
    int           visible;
    int           show;
    float         player_screen_x;
    CTexture     *map_texture;
    CTexture     *symbol_texture;
    unsigned char alpha;
    int           masked;
    int           cell_no;
    CRect_i_      map_dst;
    CRect_i_      map_src;
    CRect_i_      player_dst;
    CRect_i_      player_src;
    CRect_i_      trap_dst;
    CRect_i_      trap_src;
    CRect_i_      box_dst;
    CRect_i_      box_src;
    CRect_i_      atra_dst;
    CRect_i_      atra_src;
    status = UserStatus->minimap_status;

    if (status == 3) {
        return;
    }

    unsigned char red_table[3] = {0x20, 0x40, 0x60};
    unsigned char green_table[3] = {0x60, 0x80, 0xA2};
    unsigned char blue_table[3] = {0x40, 0x60, 0xA2};

    red = red_table[status];
    green = green_table[status];
    blue = blue_table[status];
    saved_map = BtEquipMap;
    saved_crystal = BtEquipMasuisyou;

    if (DebugStatus[3] != 0) {
        BtEquipMap = 1;
        BtEquipMasuisyou = 1;
    }

    mods::ApplyPlayerMapReveal(BtEquipMap, BtEquipMasuisyou);
    visible = BtEquipMap ? 0 : 1;
    map_texture = TexManager.GetTexture("minimap1", -1);
    symbol_texture = TexManager.GetTexture("itempack", -1);

    for (row = 0; row < 20; row++) {
        for (col = 0; col < 20; col++) {
            cell_no = col + row * chronicle::dungeon::GridStride;
            masked = this->mask[cell_no];
            show = masked;

            if (visible == 0 && masked == 0) {
                show = 1;
                alpha = red;
            } else {
                alpha = green;
            }

            if (this->cells[cell_no].parts_no != MAP_PARTS_NONE && show == 1) {
                direction = this->cells[cell_no].direction + this->parts[this->cells[cell_no].parts_no].direction_offset;
                map_src.x = (direction % 8) * 16;
                map_src.y = (direction / 8) * 16;
                map_src.width = 16;
                map_src.height = 16;
                map_dst.x = col * 16 + 380;
                map_dst.y = row * 16 + 64;
                map_dst.width = 16;
                map_dst.height = 16;
                set2DSprite(Vif1Packet, map_texture, map_dst, map_src, alpha, alpha, alpha, blue);
            }
        }
    }

    pos[0] *= 0.1f;
    pos[2] *= 0.1f;
    player_src.x = 88;
    player_src.y = 96;
    player_src.width = 12;
    player_src.height = 12;
    map_x = (int) (pos[2] + 64.0f + 8.0f);
    player_screen_x = pos[0] + 380.0f + 8.0f;
    map_y = (int) player_screen_x;
    player_dst.x = (int) player_screen_x;
    player_dst.y = map_x;
    player_dst.width = 12;
    player_dst.height = 12;
    set2DSprite(Vif1Packet, symbol_texture, player_dst, player_src, 7, 7, angle);

    for (int i = 0; i < 3; i++) {
        int cell;

        if (this->trap_circle[i].state == 0) {
            continue;
        }

        map_x = (int) (this->trap_circle[i].pos[0] * 0.1f);
        map_y = (int) (this->trap_circle[i].pos[2] * 0.1f);
        cellx = (int) ((80.0f + this->trap_circle[i].pos[0]) / 160.0f);
        cell = cellx + (int) ((80.0f + this->trap_circle[i].pos[2]) / 160.0f) * chronicle::dungeon::GridStride;
        show = this->mask[cell];

        if (BtEquipMasuisyou == 1) {
            show = 1;
        }

        if (this->cells[cell].parts_no != MAP_PARTS_NONE && show == 1) {
            trap_src.x = 88;
            trap_src.y = 112;
            trap_src.width = 8;
            trap_src.height = 8;
            trap_dst.x = map_x + 384;
            trap_dst.y = map_y + 68;
            trap_dst.width = 8;
            trap_dst.height = 8;
            set2DSprite(Vif1Packet, symbol_texture, trap_dst, trap_src, blue);
        }
    }

    for (int i = 0; i < 24; i++) {
        int cell;

        if (this->boxes[i].used == 0 || this->boxes[i].closed == 0) {
            continue;
        }

        map_x = (int) (this->boxes[i].pos[0] * 0.1f);
        map_y = (int) (this->boxes[i].pos[2] * 0.1f);
        cellx = (int) ((80.0f + this->boxes[i].pos[0]) / 160.0f);
        cell = cellx + (int) ((80.0f + this->boxes[i].pos[2]) / 160.0f) * chronicle::dungeon::GridStride;
        show = this->mask[cell];

        if (BtEquipMasuisyou == 1) {
            show = 1;
        }

        if (this->cells[cell].parts_no != MAP_PARTS_NONE && show == 1) {
            box_src.x = 80;
            box_src.y = 96;
            box_src.width = 8;
            box_src.height = 8;
            box_dst.x = map_x + 384;
            box_dst.y = map_y + 68;
            box_dst.width = 8;
            box_dst.height = 8;
            set2DSprite(Vif1Packet, symbol_texture, box_dst, box_src, blue);
        }
    }

    for (int i = 0; i < 8; i++) {
        int cell;

        if (this->atra[i].used == 0) {
            continue;
        }

        map_x = (int) (this->atra[i].pos[0] * 0.1f);
        map_y = (int) (this->atra[i].pos[2] * 0.1f);
        cellx = (int) ((80.0f + this->atra[i].pos[0]) / 160.0f);
        cell = cellx + (int) ((80.0f + this->atra[i].pos[2]) / 160.0f) * chronicle::dungeon::GridStride;
        show = this->mask[cell];

        if (BtEquipMasuisyou == 1) {
            show = 1;
        }

        if (this->cells[cell].parts_no != MAP_PARTS_NONE && show == 1) {
            atra_src.x = 72;
            atra_src.y = 104;
            atra_src.width = 8;
            atra_src.height = 8;
            atra_dst.x = map_x + 384;
            atra_dst.y = map_y + 68;
            atra_dst.width = 8;
            atra_dst.height = 8;
            set2DSprite(Vif1Packet, symbol_texture, atra_dst, atra_src, blue);
        }
    }

    BtEquipMap = saved_map;
    BtEquipMasuisyou = saved_crystal;
}
