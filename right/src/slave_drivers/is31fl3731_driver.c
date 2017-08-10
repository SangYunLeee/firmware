#include "config.h"
#include "slave_drivers/is31fl3731_driver.h"
#include "slave_scheduler.h"
#include "led_display.h"

led_driver_state_t LedDriverStates[LED_DRIVER_MAX_COUNT] = {
    {
        .i2cAddress = I2C_ADDRESS_LED_DRIVER_RIGHT,
        .setupLedControlRegistersCommand = {
            FRAME_REGISTER_LED_CONTROL_FIRST,
            0b01111111, // key row 1
            0b00000000, // no display
            0b01111111, // keys row 2
            0b00000000, // no display
            0b01111111, // keys row 3
            0b00000000, // no display
            0b01111111, // keys row 4
            0b00000000, // no display
            0b01111010, // keys row 5
            0b00000000, // no display
            0b00000000, // keys row 6
            0b00000000, // no display
            0b00000000, // keys row 7
            0b00000000, // no display
            0b00000000, // keys row 8
            0b00000000, // no display
            0b00000000, // keys row 9
            0b00000000, // no display
        }
    },
    {
        .i2cAddress = I2C_ADDRESS_LED_DRIVER_LEFT,
        .setupLedControlRegistersCommand = {
            FRAME_REGISTER_LED_CONTROL_FIRST,
            0b01111111, // key row 1
            0b00111111, // display row 1
            0b01011111, // keys row 2
            0b00111111, // display row 2
            0b01011111, // keys row 3
            0b00111111, // display row 3
            0b01111101, // keys row 4
            0b00011111, // display row 4
            0b00101111, // keys row 5
            0b00011111, // display row 5
            0b00000000, // keys row 6
            0b00011111, // display row 6
            0b00000000, // keys row 7
            0b00011111, // display row 7
            0b00000000, // keys row 8
            0b00011111, // display row 8
            0b00000000, // keys row 9
            0b00011111, // display row 9
        }
    },
};

uint8_t setFunctionFrameBuffer[] = {LED_DRIVER_REGISTER_FRAME, LED_DRIVER_FRAME_FUNCTION};
uint8_t setShutdownModeNormalBuffer[] = {LED_DRIVER_REGISTER_SHUTDOWN, SHUTDOWN_MODE_NORMAL};
uint8_t setFrame1Buffer[] = {LED_DRIVER_REGISTER_FRAME, LED_DRIVER_FRAME_1};
uint8_t updatePwmRegistersBuffer[PWM_REGISTER_BUFFER_LENGTH];

void LedSlaveDriver_Init(uint8_t ledDriverId) {
    led_driver_state_t *currentLedDriverState = LedDriverStates + ledDriverId;
    currentLedDriverState->phase = LedDriverPhase_SetFunctionFrame;
    currentLedDriverState->ledIndex = 0;
    LedDriverStates[LedDriverId_Left].setupLedControlRegistersCommand[7] |= 0b00000010; // Enable the LED of the ISO key.
    memset(currentLedDriverState->targetLedValues, 0x00, LED_DRIVER_LED_COUNT);
    SetLeds(0xff);
    LedDisplay_SetText(3, "ABC");
}

void LedSlaveDriver_Update(uint8_t ledDriverId) {
    led_driver_state_t *currentLedDriverState = LedDriverStates + ledDriverId;
    uint8_t *ledDriverPhase = &currentLedDriverState->phase;
    uint8_t ledDriverAddress = currentLedDriverState->i2cAddress;
    uint8_t *ledIndex = &currentLedDriverState->ledIndex;

    switch (*ledDriverPhase) {
        case LedDriverPhase_SetFunctionFrame:
            if (ledDriverId == LedDriverId_Left && !Slaves[SlaveId_LeftKeyboardHalf].isConnected) {
                break;
            }
            I2cAsyncWrite(ledDriverAddress, setFunctionFrameBuffer, sizeof(setFunctionFrameBuffer));
            *ledDriverPhase = LedDriverPhase_SetShutdownModeNormal;
            break;
        case LedDriverPhase_SetShutdownModeNormal:
            I2cAsyncWrite(ledDriverAddress, setShutdownModeNormalBuffer, sizeof(setShutdownModeNormalBuffer));
            *ledDriverPhase = LedDriverPhase_SetFrame1;
            break;
        case LedDriverPhase_SetFrame1:
            I2cAsyncWrite(ledDriverAddress, setFrame1Buffer, sizeof(setFrame1Buffer));
            *ledDriverPhase = LedDriverPhase_InitLedControlRegisters;
            break;
        case LedDriverPhase_InitLedControlRegisters:
            I2cAsyncWrite(ledDriverAddress, currentLedDriverState->setupLedControlRegistersCommand, LED_CONTROL_REGISTERS_COMMAND_LENGTH);
            *ledDriverPhase = LedDriverPhase_InitLedValues;
            break;
        case LedDriverPhase_InitLedValues:
            updatePwmRegistersBuffer[0] = FRAME_REGISTER_PWM_FIRST + *ledIndex;
            memcpy(updatePwmRegistersBuffer+1, currentLedDriverState->sourceLedValues + *ledIndex, PMW_REGISTER_UPDATE_CHUNK_SIZE);
            I2cAsyncWrite(ledDriverAddress, updatePwmRegistersBuffer, PWM_REGISTER_BUFFER_LENGTH);
            *ledIndex += PMW_REGISTER_UPDATE_CHUNK_SIZE;
            if (*ledIndex >= LED_DRIVER_LED_COUNT) {
                *ledIndex = 0;
                *ledDriverPhase = LedDriverPhase_Initialized;
            }
            break;
        case LedDriverPhase_Initialized:
        {
            uint8_t *sourceLedValues = currentLedDriverState->sourceLedValues;
            uint8_t *targetLedValues = currentLedDriverState->targetLedValues;

            uint8_t lastLedChunkStartIndex = LED_DRIVER_LED_COUNT - PMW_REGISTER_UPDATE_CHUNK_SIZE;
            uint8_t startLedIndex = *ledIndex > lastLedChunkStartIndex ? lastLedChunkStartIndex : *ledIndex;

            uint8_t count;
            for (count=0; count<LED_DRIVER_LED_COUNT; count++) {
                if (sourceLedValues[startLedIndex] != targetLedValues[startLedIndex]) {
                    break;
                }

                if (++startLedIndex >= LED_DRIVER_LED_COUNT) {
                    startLedIndex = 0;
                }
            }

            bool foundStartIndex = count < LED_DRIVER_LED_COUNT;
            if (!foundStartIndex) {
                *ledIndex = 0;
                return;
            }

            uint8_t maxChunkSize = MIN(LED_DRIVER_LED_COUNT - startLedIndex, PMW_REGISTER_UPDATE_CHUNK_SIZE);
            uint8_t maxEndLedIndex = startLedIndex + maxChunkSize - 1;
            uint8_t endLedIndex = startLedIndex;
            for (uint8_t index=startLedIndex; index<=maxEndLedIndex; index++) {
                if (sourceLedValues[index] != targetLedValues[index]) {
                    endLedIndex = index;
                }
            }

            updatePwmRegistersBuffer[0] = FRAME_REGISTER_PWM_FIRST + startLedIndex;
            uint8_t length = endLedIndex - startLedIndex + 1;
            memcpy(updatePwmRegistersBuffer+1, currentLedDriverState->sourceLedValues + startLedIndex, length);
            memcpy(currentLedDriverState->targetLedValues + startLedIndex, currentLedDriverState->sourceLedValues + startLedIndex, length);
            I2cAsyncWrite(ledDriverAddress, updatePwmRegistersBuffer, length+1);
            *ledIndex += length;
            if (*ledIndex >= LED_DRIVER_LED_COUNT) {
                *ledIndex = 0;
            }
            break;
        }
    }
}

void SetLeds(uint8_t ledBrightness)
{
    for (uint8_t i=0; i<LED_DRIVER_MAX_COUNT; i++) {
        memset(&LedDriverStates[i].sourceLedValues, ledBrightness, LED_DRIVER_LED_COUNT);
    }
}
