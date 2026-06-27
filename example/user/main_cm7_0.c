/*********************************************************************************************************************
* CYT4BB CM7_0 test program
*
* Stage 1 division:
*   CM7_0: reserved for motor/control work. It must not initialize the camera or DEBUG UART in this test.
*   CM7_1: owns MT9V03X camera acquisition and Seekfree Assistant UART transmission.
********************************************************************************************************************/

#include "zf_common_headfile.h"

int main(void)
{
    clock_init(SYSTEM_CLOCK_250M);      // System clock initialization. Keep this call.
    debug_info_init();                  // Information only; does not claim DEBUG UART hardware.

    while(true)
    {
        // Reserved for later motor, encoder and control code.
        // Do not call mt9v03x_init(), debug_init() or seekfree_assistant_camera_send() here.
    }
}
