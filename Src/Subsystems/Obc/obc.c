#include "obc.h"
#include "obc_internal.h"

// mirar si xObcEventGroup deberia ser global variable

ObcState_t Nominal(void) {
    // to do:
    //  frequency tratment for state

    xObcEventGroup
}

void ObcTask() {
    // OBC state machine starts in NOMINAL state
    ObcState_t ObcState = NOMINAL; // have to change right?? NOMINAL for now

    for (;;) {
        switch (ObcState) {
            case NOMINAL:
                ObcState = Nominal(); break;
        }
    }
}