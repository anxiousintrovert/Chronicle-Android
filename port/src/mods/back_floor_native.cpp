#include "back_floor.hpp"
#include "btsysscript.hpp"
#include "dun/gameloop.hpp"
#include "runscript.hpp"
#include "shot_freefuncs.hpp"
#include "userstatus.hpp"

// Native semantics unless a framework-started transport requests its access check.
// No inventory item or saved event flag is changed by this adapter.
PORT_OVERRIDE int _CHECK_EVENT_FLG(RS_STACKDATA *stack, int argument_count) {
    int flag_no = stack->type == RS_FLOAT ? (int) stack->f : stack->i;
    ++stack;
    int flag = mods::BackFloorTransportAccess(flag_no) ? 1 : UserStatus->ChkEventFlag(flag_no);
    if (stack->type == RS_PTR) {
        stack->p->i = flag;
    }
    return 1;
}
