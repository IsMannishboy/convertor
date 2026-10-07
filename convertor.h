#pragma once

#include <Arduino.h>

#ifndef IN_BAUD
    #define IN_BAUD 115200
#endif
#ifndef OUT_BAUD
    #define OUT_BAUD 115200
#endif
#ifndef IN_INV
    #define IN_INV true
#endif
#ifndef OUT_INV
    #define OUT_INV true
#endif
#ifndef PIN_IN_RX
    #define PIN_IN_RX 20
#endif
#ifndef PIN_OUT_TX
    #define PIN_OUT_TX 10
#endif

#ifndef PIN_BUTTON
    #define PIN_BUTTON 6   // на C3 devkit это кнопка BOOT
#endif
