/*-----------------------------------------------------------/
/ ConfigParam.h
/------------------------------------------------------------/
/ Copyright (c) 2024, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/

#pragma once

#include "FlashParam.h"

typedef enum {
    CFG_ID_VOLUME = FlashParamNs::CFG_ID_BASE,
} ParamId_t;

//=================================
// Interface of ConfigParam class
//=================================
struct ConfigParam : FlashParamNs::FlashParam {
    static ConfigParam& instance() {  // Singleton
        static ConfigParam instance;
        return instance;
    }
    // Parameter<T>                   inst          id             name          default size
    FlashParamNs::Parameter<uint32_t> P_CFG_VOLUME {CFG_ID_VOLUME, "CFG_VOLUME", 50};
};
