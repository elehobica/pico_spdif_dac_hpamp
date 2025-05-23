/*------------------------------------------------------/
/ Copyright (c) 2023, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/audio_i2s.h"
#include "spdif_rx.h"
#include "RotaryEncoder.h"
#include "ConfigParam.h"

static constexpr uint PIN_LED = PICO_DEFAULT_LED_PIN;

static constexpr uint8_t PIN_DCDC_PSM_CTRL = 23;
static constexpr uint8_t PIN_PICO_SPDIF_RX_DATA = 15;
static constexpr uint8_t PIN_ROTARY_ENCODER_A = 26;
static constexpr uint8_t PIN_ROTARY_ENCODER_B = 27;
static constexpr uint8_t PIN_P5V_EN = 28;
static constexpr uint8_t PIN_CHARGER_KEY_EN = 22;

static constexpr uint32_t NO_SYNC_TIMEOUT_P5V_OFF_SEC = 60;
static constexpr uint32_t NO_SIGNAL_TIMEOUT_P5V_OFF_SEC = 180;
static constexpr uint32_t NO_SIGNAL_LEVEL = 4;  // level to detect blank supposing 16bit data

static constexpr int SAMPLES_PER_BUFFER = PICO_AUDIO_I2S_BUFFER_SAMPLE_LENGTH; // Samples / channel
static constexpr int32_t DAC_ZERO = 1;
static int16_t buf_s16[SAMPLES_PER_BUFFER*2]; // 16bit 2ch data before applying volume
static audio_buffer_pool_t* ap = nullptr;
static bool decode_flg = false;
volatile static bool i2s_setup_flg = false;
volatile static bool i2s_cancel_flg = false;

static gpio::RotaryEncoder* rotaryEncoder;
static constexpr int32_t ROTARY_ENCODER_STEP = 2;
static constexpr int32_t ROTARY_ENCODER_OUTPUT_MIN = 0;
static constexpr int32_t ROTARY_ENCODER_OUTPUT_MAX = 100;
static constexpr int32_t ROTARY_ENCODER_OUTPUT_DEFAULT = 50;

static volatile uint32_t last_signal_time;

const uint32_t vol_table[101] = {
    0, 4, 8, 12, 16, 20, 24, 27, 29, 31,
    34, 37, 40, 44, 48, 52, 57, 61, 67, 73,
    79, 86, 94, 102, 111, 120, 131, 142, 155, 168,
    183, 199, 217, 236, 256, 279, 303, 330, 359, 390, // vol_table[34] = 256
    424, 462, 502, 546, 594, 646, 703, 764, 831, 904,
    983, 1069, 1163, 1265, 1376, 1496, 1627, 1770, 1925, 2094,
    2277, 2476, 2693, 2929, 3186, 3465, 3769, 4099, 4458, 4849,
    5274, 5736, 6239, 6785, 7380, 8026, 8730, 9495, 10327, 11232,
    12216, 13286, 14450, 15716, 17093, 18591, 20220, 21992, 23919, 26015,
    28294, 30773, 33470, 36403, 39592, 43061, 46835, 50938, 55402, 60256,
    65536
};
static uint32_t volume_mul;

#define audio_pio __CONCAT(pio, PICO_AUDIO_I2S_PIO)

static audio_format_t audio_format = {
    .sample_freq = 44100,
    .pcm_format = AUDIO_PCM_FORMAT_S32,
    .channel_count = AUDIO_CHANNEL_STEREO
};

static audio_buffer_format_t producer_format = {
    .format = &audio_format,
    .sample_stride = 8
};

static audio_i2s_config_t i2s_config = {
    .data_pin = PICO_AUDIO_I2S_DATA_PIN,
    .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
    .dma_channel0 = 0,
    .dma_channel1 = 1,
    .pio_sm = 0
};

static io_rw_32* reg_clkdiv;
static io_rw_32 clkdiv_tbl[3];
typedef enum _clkdiv_speed_t {
    CLKDIV_FAST = 0,
    CLKDIV_NORM = 1,
    CLKDIV_SLOW = 2
} clkdiv_speed_t;

ConfigParam& cfgParam = ConfigParam::instance();

static inline uint32_t _millis()
{
    return to_ms_since_boot(get_absolute_time());
}

static void save_center_clkdiv(PIO pio, uint sm)
{
    reg_clkdiv = &(pio->sm[sm].clkdiv);
    clkdiv_tbl[CLKDIV_NORM] = *reg_clkdiv;
    clkdiv_tbl[CLKDIV_FAST] = clkdiv_tbl[CLKDIV_NORM] - (1 << PIO_SM0_CLKDIV_FRAC_LSB);
    clkdiv_tbl[CLKDIV_SLOW] = clkdiv_tbl[CLKDIV_NORM] + (1 << PIO_SM0_CLKDIV_FRAC_LSB);
}

static void set_offset_clkdiv(clkdiv_speed_t speed)
{
    *reg_clkdiv = clkdiv_tbl[speed];
}

static void i2s_audio_deinit()
{
    decode_flg = false;

    audio_i2s_set_enabled(false);
    audio_i2s_end();

    audio_buffer_t* ab;
    ab = take_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = take_audio_buffer(ap, false);
    }
    ab = get_free_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_free_audio_buffer(ap, false);
    }
    ab = get_full_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_full_audio_buffer(ap, false);
    }
    free(ap);
    ap = nullptr;
}

static audio_buffer_pool_t *i2s_audio_init(uint32_t sample_freq)
{
    audio_format.sample_freq = sample_freq;

    audio_buffer_pool_t *producer_pool = audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);
    ap = producer_pool;

    bool __unused ok;
    const audio_format_t *output_format;

    output_format = audio_i2s_setup(&audio_format, &audio_format, &i2s_config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    { // initial buffer data
        audio_buffer_t *ab = take_audio_buffer(producer_pool, true);
        int32_t *samples = (int32_t *) ab->buffer->bytes;
        for (uint i = 0; i < ab->max_sample_count; i++) {
            samples[i*2+0] = DAC_ZERO;
            samples[i*2+1] = DAC_ZERO;
        }
        ab->sample_count = ab->max_sample_count;
        give_audio_buffer(producer_pool, ab);
    }
    save_center_clkdiv(audio_pio, i2s_config.pio_sm);
    audio_i2s_set_enabled(true);

    decode_flg = true;
    return producer_pool;
}

static bool decode()
{
    static bool mute_flag = true;

    if (ap == nullptr) { return false; }
    audio_buffer_t *ab;
    if ((ab = take_audio_buffer(ap, false)) == nullptr) { return false; }

    #ifdef DEBUG_PLAYAUDIO
    {
        uint32_t time = to_ms_since_boot(get_absolute_time());
        printf("AUDIO::decode start at %d ms\n", time);
    }
    #endif // DEBUG_PLAYAUDIO

    ab->sample_count = ab->max_sample_count;
    int32_t *samples = (int32_t *) ab->buffer->bytes;

    uint32_t fifo_count = spdif_rx_get_fifo_count();
    if (spdif_rx_get_state() == SPDIF_RX_STATE_STABLE) {
        if (mute_flag && fifo_count >= SPDIF_RX_FIFO_SIZE / 2) {
            mute_flag = false;
        }
    } else {
        mute_flag = true;
    }

    uint32_t data_accum = 0;
    uint32_t ave_level;
    if (mute_flag) {
        for (int i = 0; i < ab->sample_count; i++) {
            samples[i*2+0] = DAC_ZERO;
            samples[i*2+1] = DAC_ZERO;
        }
        ave_level = 0;
    } else {
        // I2S frequency adjustment (feedback from SPDIF_RX fffo_count)
        // note that this scheme could increase I2S clock jitter
        // (using pio_sm_set_clkdiv should have already include jitter unless dividing by integer)
        if (fifo_count <= SPDIF_RX_FIFO_SIZE / 2 - SPDIF_BLOCK_SIZE) {
            set_offset_clkdiv(CLKDIV_SLOW);
            //printf("<");
        } else if (fifo_count <= SPDIF_RX_FIFO_SIZE / 2 + SPDIF_BLOCK_SIZE) {
            set_offset_clkdiv(CLKDIV_NORM);
            //printf("-");
        } else {
            set_offset_clkdiv(CLKDIV_FAST);
            //printf(">");
        }
        //printf("%d,", fifo_count);
        if (ab->sample_count > fifo_count / 2) {
            ab->sample_count = fifo_count / 2;
        }
        uint32_t total_count = ab->sample_count * 2;
        int i = 0;
        uint32_t read_count = 0;
        uint32_t* buff;
        uint32_t volume_mul_target = vol_table[rotaryEncoder->get()];
        // volume slow transition to avoid noise
        if (volume_mul < volume_mul_target) {
            volume_mul += (volume_mul_target - volume_mul + 64) / 64;
        } else if (volume_mul > volume_mul_target) {
            volume_mul -= (volume_mul - volume_mul_target + 64) / 64;
        }
        while (read_count < total_count) {
            uint32_t get_count = spdif_rx_read_fifo(&buff, total_count - read_count);
            for (int j = 0; j < get_count / 2; j++) {
                if (volume_mul >= 256) {
                    // keep 24bit if 32bit DAC
                    samples[i*2+0] = (int32_t) ((buff[j*2+0] & 0x0ffffff0) << 4) / 256 * (volume_mul / 256) + DAC_ZERO;
                    samples[i*2+1] = (int32_t) ((buff[j*2+1] & 0x0ffffff0) << 4) / 256 * (volume_mul / 256) + DAC_ZERO;
                } else if (volume_mul >= 16) {
                    // keep 20bit if 32bit DAC
                    samples[i*2+0] = (int32_t) ((buff[j*2+0] & 0x0ffffff0) << 4) / 4096 * (volume_mul / 16) + DAC_ZERO;
                    samples[i*2+1] = (int32_t) ((buff[j*2+1] & 0x0ffffff0) << 4) / 4096 * (volume_mul / 16) + DAC_ZERO;
                } else {
                    // keep 16bit if 32bit DAC
                    samples[i*2+0] = (int32_t) ((buff[j*2+0] & 0x0ffffff0) << 4) / 65536 * volume_mul + DAC_ZERO;
                    samples[i*2+1] = (int32_t) ((buff[j*2+1] & 0x0ffffff0) << 4) / 65536 * volume_mul + DAC_ZERO;
                }
                data_accum += std::abs(static_cast<int16_t>(((buff[i*2+0] >> 12) & 0xffff)));
                data_accum += std::abs(static_cast<int16_t>(((buff[i*2+1] >> 12) & 0xffff)));
                i++;
            }
            read_count += get_count;
        }
        ave_level = data_accum / total_count;
    }
    give_audio_buffer(ap, ab);

    #ifdef DEBUG_PLAYAUDIO
    {
        uint32_t time = to_ms_since_boot(get_absolute_time());
        printf("AUDIO::decode end   at %d ms\n", time);
    }
    #endif // DEBUG_PLAYAUDIO

    return ave_level > NO_SIGNAL_LEVEL;
}

extern "C" {
void i2s_callback_func();
}
// callback from:
//   void __isr __time_critical_func(audio_i2s_dma_irq_handler)()
//   defined at my_pico_audio_i2s/audio_i2s.c
//   where i2s_callback_func() is declared with __attribute__((weak))
void i2s_callback_func()
{
    uint32_t now = _millis();
    if (decode_flg) {
        if (decode()) {
            last_signal_time = now;
        }
    }
}

static void i2s_setup(spdif_rx_samp_freq_t samp_freq)
{
    float samp_freq_actual = spdif_rx_get_samp_freq_actual();
    uint32_t c_bits;
    spdif_rx_get_c_bits(&c_bits, sizeof(c_bits), 0);
    printf("Samp Freq = %d Hz (%6.4f KHz)\n", (int) samp_freq, samp_freq_actual / 1e3);
    printf("SPDIF C bits = %08x\n", c_bits);
    if (ap != nullptr) {
        i2s_audio_deinit(); // less gap noise if deinit() is done when input is stable
    }
    // need to care the case when lost during setup to avoid noise
    if (i2s_cancel_flg) { return; }

    i2s_audio_init(samp_freq);

    // need to care the case when lost during setup to avoid noise
    if (i2s_cancel_flg) {
        if (ap != nullptr) {
            i2s_audio_deinit();
        }
    }
}

static void on_stable_func(spdif_rx_samp_freq_t samp_freq)
{
    // callback function should be returned as quick as possible
    i2s_setup_flg = true;
    i2s_cancel_flg = false;
}

static void on_lost_stable_func()
{
    // callback function should be returned as quick as possible
    i2s_cancel_flg = true;
}

static void load_from_flash()
{
    rotaryEncoder->set(cfgParam.P_CFG_VOLUME.get());
    volume_mul = vol_table[rotaryEncoder->get()];
}

static void store_to_flash()
{
    // note that there is no care about core1 if running (if core1 runs, finalize() will fail)
    // also it breaks spdif_rx data due to the pause of interrupts,
    //   therefore store_to_flash() should be done after audio power off
    cfgParam.P_CFG_VOLUME.set(static_cast<uint32_t>(rotaryEncoder->get()));
    cfgParam.finalize();
}

int main()
{
    stdio_init_all();

    // LED
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, false);

    // DCDC PSM control
    // 0: PFM mode (best efficiency)
    // 1: PWM mode (improved ripple)
    gpio_init(PIN_DCDC_PSM_CTRL);
    gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, true); // PWM mode for less Audio noise

    // KEY enable for charger
    gpio_init(PIN_CHARGER_KEY_EN);
    gpio_set_dir(PIN_CHARGER_KEY_EN, GPIO_OUT);
    gpio_put(PIN_CHARGER_KEY_EN, false);

    // 5V Power for DAC & Amp
    gpio_init(PIN_P5V_EN);
    gpio_set_dir(PIN_P5V_EN, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, false);

    // Rotary Encoder
    gpio_init(PIN_ROTARY_ENCODER_A);
    gpio_set_dir(PIN_ROTARY_ENCODER_A, GPIO_IN);
    gpio_pull_up(PIN_ROTARY_ENCODER_A);

    gpio_init(PIN_ROTARY_ENCODER_B);
    gpio_set_dir(PIN_ROTARY_ENCODER_B, GPIO_IN);
    gpio_pull_up(PIN_ROTARY_ENCODER_B);
    rotaryEncoder = new gpio::RotaryEncoder(PIN_ROTARY_ENCODER_A, PIN_ROTARY_ENCODER_B, ROTARY_ENCODER_STEP, ROTARY_ENCODER_OUTPUT_DEFAULT);

    // Load from Flash
    load_from_flash();

    spdif_rx_config_t config = {
        .data_pin = PIN_PICO_SPDIF_RX_DATA,
        .pio_sm = 0,
        .dma_channel0 = 2,
        .dma_channel1 = 3,
        .alarm_pool = alarm_pool_get_default(),
        .flags = SPDIF_RX_FLAGS_ALL
    };

    spdif_rx_start(&config);
    spdif_rx_set_callback_on_stable(on_stable_func);
    spdif_rx_set_callback_on_lost_stable(on_lost_stable_func);

    uint32_t now = _millis();
    uint32_t last_sync_time = 0;
    bool prev_power = true;
    while (true) {
        now = _millis();
        if (i2s_setup_flg) {
            i2s_setup_flg = false;
            i2s_setup(spdif_rx_get_samp_freq());
        }
        if (spdif_rx_get_state() == SPDIF_RX_STATE_STABLE) {
            last_sync_time = now;
        }
        if (spdif_rx_get_state() == SPDIF_RX_STATE_STABLE && (now - last_signal_time < NO_SIGNAL_TIMEOUT_P5V_OFF_SEC/2 * 1000)) {
            gpio_put(PIN_P5V_EN, true);
            gpio_put(PIN_LED, true);
            prev_power = true;
        } else if (spdif_rx_get_state() == SPDIF_RX_STATE_STABLE && (now - last_signal_time < NO_SIGNAL_TIMEOUT_P5V_OFF_SEC * 1000)) {
            gpio_put(PIN_P5V_EN, true);
            gpio_put(PIN_LED, ((now / 10) % 100 < 16));  // blink 1 Hz / duty 16 %
            prev_power = true;
        } else if (spdif_rx_get_state() != SPDIF_RX_STATE_STABLE && (now - last_sync_time < NO_SYNC_TIMEOUT_P5V_OFF_SEC * 1000)) {
            gpio_put(PIN_P5V_EN, true);
            gpio_put(PIN_LED, ((now / 10) % 100 < 4));  // blink 1 Hz / duty 4 %
            prev_power = true;
        } else {
            gpio_put(PIN_P5V_EN, false); // terminate 5V power for DAC and Amp
            gpio_put(PIN_LED, false);
            if (prev_power) {
                store_to_flash();  // at this timming the power from battery is still alive and shutdown soon
            } else {
                // terminate main 5V power from charger
                gpio_put(PIN_CHARGER_KEY_EN, true);
            }
            prev_power = false;
        }
        tight_loop_contents();
        sleep_ms(10);
    }

    return 0;
}
