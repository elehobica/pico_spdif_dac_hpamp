/*-----------------------------------------------------------/
/ RotaryEncoder.h
/------------------------------------------------------------/
/ Copyright (c) 2023, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/

#pragma once

#include <map>
#include "pico/stdlib.h"

namespace gpio
{
class RotaryEncoder
{
public:
    RotaryEncoder(const uint pinA, const uint pinB, const uint32_t step = 1, const int32_t initValue = 0, const int32_t min = 0, const int32_t max = 100);
    virtual ~RotaryEncoder();
    int32_t value() const;
private:
    static std::map<const uint, RotaryEncoder* const> _pinARef;
    static std::map<const uint, RotaryEncoder* const> _pinBRef;
    const uint _pinA;
    const uint _pinB;
    const uint32_t _step;
    const int32_t _min;
    const int32_t _max;
    int32_t _rawValue;

    void _add(const int32_t inc);

    friend void rotaryEncoderCallback(uint gpio, uint32_t events);
};
}  // namespace gpio