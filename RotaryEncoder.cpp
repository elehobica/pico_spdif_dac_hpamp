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
        RotaryEncoder* const re = RotaryEncoder::_pinARef[gpio];
        if (events == GPIO_IRQ_EDGE_RISE) {
            re->_inc(!gpio_get(re->_pinB));
        } else if (events == GPIO_IRQ_EDGE_FALL) {
            re->_inc(gpio_get(re->_pinB));
        }
    } else if (RotaryEncoder::_pinBRef.count(gpio) > 0) {
        RotaryEncoder* const re = RotaryEncoder::_pinBRef[gpio];
        if (events == GPIO_IRQ_EDGE_RISE) {
            re->_inc(gpio_get(re->_pinA));
        } else if (events == GPIO_IRQ_EDGE_FALL) {
            re->_inc(!gpio_get(re->_pinA));
        }
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

void RotaryEncoder::set(int32_t value)
{
    _setRaw(value * _step);
}

int32_t RotaryEncoder::get() const
{
    return _rawValue / _step;
}

void RotaryEncoder::_setRaw(int32_t rawValue)
{
    if (rawValue < (int32_t) (_min * _step)) {
        rawValue = _min * _step;
    } else if (rawValue > (int32_t) (_max * _step)) {
        rawValue = _max * _step;
    }
    _rawValue = rawValue;
}

void RotaryEncoder::_inc(const bool up)
{
    if (up) {
        _setRaw(_rawValue + 1);
    } else {
        _setRaw(_rawValue - 1);
    }
}

}  // namespace gpio