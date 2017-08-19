#ifndef __SLAVE_PROTOCOL_H__
#define __SLAVE_PROTOCOL_H__

// Typedefs:

    typedef enum {
        SlaveCommand_GetKeyStates,
        SlaveCommand_SetTestLed,
        SlaveCommand_SetLedPwmBrightness,
    } slave_command_t;

#endif
