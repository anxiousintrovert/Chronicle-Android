// Native body loader with an ownership retirement notification only.
#include "character.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "editloop.hpp"
#include "frame.hpp"
#include "textureanime.hpp"
#include "town.hpp"

PORT_OVERRIDE void EdLoadMainChara(char *pack_path, char *info_name, CDataAlloc2<1> *arena) {
    int i;

    if (arena == NULL) {
        arena = &CharaBuffer;
        CharaBuffer.used = 0;
    }

    LoadFile(pack_path, read_buffer, NULL);
    MainChara.Initialize();

    for (i = 0; i < 128; i++) {
        CharaTexAnimeData[i].Initialize();
    }

    MainChara.InitializeTexAnime(CharaTexAnimeData, 128);
    MainChara.LoadPackData2(read_buffer, info_name, arena, 8, arena, 0);
    CFrameAttr attr;
    attr.clip_enable = false;
    attr.fog_enable = true;
    MainChara.frame->SetAttr(attr, 1, 4);
#ifdef PAL
    MainChara.SetPosition(0.0f, 0.0f, 0.0f);
#else
    float origin = 0.0f;
    MainChara.SetPosition(origin, origin, origin);
#endif
    Chara = &MainChara;
    mods::TownBodyLoaded();
}
