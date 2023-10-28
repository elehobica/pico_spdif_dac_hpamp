/*-----------------------------------------------------------/
/ RotaryEncoder.cpp
/------------------------------------------------------------/
/ Copyright (c) 2023, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/

#include "RotaryEncoder.h"

namespace gpio
{

std::map<const uint, RotaryEncoder* const> RotaryEncoder::_pinARef;
std::map<const uint, RotaryEncoder* const> RotaryEncoder::_pinBRef;

void rotaryEncoderCallback(uint gpio, uint32_t events)
{
    if (RotaryEncoder::_pinARef.count(gpio) > 0) {
        int32_t inc;
        RotaryEncoder* const re = RotaryEncoder::_pinARef[gpio];
        if (events == GPIO_IRQ_EDGE_RISE) {
            inc = (gpio_get(re->_pinB)) ? -1 : 1;
        } else if (events == GPIO_IRQ_EDGE_FALL) {
            inc = (gpio_get(re->_pinB)) ? 1 : -1;
        }
        re->_add(inc);
    } else if (RotaryEncoder::_pinBRef.count(gpio) > 0) {
        int32_t inc;
        RotaryEncoder* const re = RotaryEncoder::_pinBRef[gpio];
        if (events == GPIO_IRQ_EDGE_RISE) {
            inc = (gpio_get(re->_pinA)) ? 1 : -1;
        } else if (events == GPIO_IRQ_EDGE_FALL) {
            inc = (gpio_get(re->_pinA)) ? -1 : 1;
        }
        re->_add(inc);
    }
}

RotaryEncoder::RotaryEncoder(const uint pinA, const uint pinB, const uint32_t step, const int32_t initValue, const int32_t min, const int32_t max) :
    _pinA(pinA), _pinB(pinB), _step(step), _min(min), _max(max)
{
    _rawValue = initValue * _step;
    _pinARef.emplace(_pinA, this);
    _pinBRef.emplace(_pinB, this);
    gpio_set_irq_enabled_with_callback(_pinA, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, rotaryEncoderCallback);
    gpio_set_irq_enabled_with_callback(_pinB, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, rotaryEncoderCallback);
}

RotaryEncoder::~RotaryEncoder()
{
    gpio_set_irq_enabled_with_callback(_pinA, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, nullptr);
    gpio_set_irq_enabled_with_callback(_pinB, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, nullptr);
    _pinARef.erase(_pinA);
    _pinBRef.erase(_pinB);
}

int32_t RotaryEncoder::value() const
{
    return _rawValue / _step;
}

void RotaryEncoder::_add(const int32_t inc)
{
    _rawValue += inc;
    if (_rawValue < (int32_t) (_min * _step)) {
        _rawValue = _min * _step;
    } else if (_rawValue > (int32_t) (_max * _step)) {
        _rawValue = _max * _step;
    }
}

}  // namespace gpio