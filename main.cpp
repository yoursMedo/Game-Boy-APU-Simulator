#include <print>
#include <array>
#include <vector>
#include <fstream>

constexpr std::uint64_t Clock_Hz{4'194'304};
constexpr std::uint64_t Cycles_Per_Step{200`};
constexpr std::uint64_t Frame_Sequencer_Period{8'192};
constexpr std::uint64_t Sample_Rate{48'000};

namespace {

class SquareChannel {
public:
    void write_sweep_reg(std::uint8_t v) {
        sweep_period    = (v >> 4) & 0x07;
        sweep_direction = (v & 0x08) ? -1 : 1;
        sweep_shift     = v & 0x07;
    }

    void write_duty_length(std::uint8_t v) {
        duty_cycle = (v >> 6) & 0x03;
        channel_countdown = 64 - (v & 0x3F);
    }


    void write_envelope_reg(std::uint8_t v) {
        current_volume     = (v >> 4) & 0x0F;
        envelope_direction = (v & 0x08) ? 1 : -1;
        envelope_period    = v & 0x07;
    }

    void write_freq_low(std::uint8_t v) {
        frequency = (frequency & 0x700) | v;
    }


    void write_freq_high_control(std::uint8_t v) {
        frequency = (frequency & 0x0FF) | ((v & 0x07) << 8);
        countdown_enabled = (v & 0x40) != 0;
        if (v & 0x80) {
            trigger_now();
        }
    }

    void trigger_now() {
        enabled = true;
        duty_position = 0;
        frequency_timer_counter = (2'048 - frequency) * 4;
        envelope_countdown = envelope_period;
        sweep_countdown = sweep_period;
    }

    void tick_length() {
        if (countdown_enabled) {
            if (channel_countdown > 0) {
                channel_countdown -= 1;
                if (channel_countdown == 0) enabled = false;
            }
        }
    }

    void tick_frequency(std::uint64_t cycles) {
        frequency_timer_counter += cycles;
        while (frequency_timer_counter >= (2'048 - frequency) * 4) {
            frequency_timer_counter -= (2'048 - frequency) * 4;
            duty_position = (duty_position + 1) % 8;
        }
    }

    void tick_envelope() {
        if (envelope_period == 0) return;

        if (envelope_countdown > 0) {
            envelope_countdown -= 1;
            if (envelope_countdown == 0) {
                envelope_countdown = envelope_period;
                if (envelope_direction > 0 && current_volume < 15) {
                    current_volume += 1;
                } else if (envelope_direction < 0 && current_volume > 0) {
                    current_volume -= 1;
                }
            }
        }
    }

    void tick_sweep() {
        if (sweep_period == 0) return;
        if (sweep_countdown > 0) {
            sweep_countdown -= 1;
            if (sweep_countdown == 0) {
                sweep_countdown = sweep_period;
                std::uint16_t change = frequency >> sweep_shift;
                std::uint16_t new_freq = (sweep_direction > 0) ? (frequency + change)
                                                               : (frequency - change);
                if (new_freq > 2'047) {
                    enabled = false;
                } else {
                    frequency = new_freq;
                }
            }
        }
    }

    std::int16_t output() const {
        if (!enabled) { return 0; }
        std::uint8_t bit = duty_table[duty_cycle][duty_position];
        std::int16_t level = (bit == 1) ? current_volume : -static_cast<std::int16_t>(current_volume);
        return level;
    }

    void step(std::uint64_t cycles) {
        tick_frequency(cycles);
    }

private:
    bool enabled{false};
    std::uint8_t channel_countdown{64};
    bool countdown_enabled{false};
    std::uint8_t duty_position{};
    std::uint8_t duty_cycle{};
    std::uint16_t frequency{};
    std::uint32_t frequency_timer_counter{0};
    static constexpr std::uint8_t duty_table[4][8] = {
        {0, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 1, 1, 1},
        {0, 1, 1, 1, 1, 1, 1, 0},
    };
    std::uint8_t current_volume{};
    std::int8_t  envelope_direction{};
    std::uint8_t envelope_period{};
    std::uint8_t envelope_countdown{};
    std::uint8_t sweep_period{};
    std::int8_t  sweep_direction{};
    std::uint8_t sweep_shift{};
    std::uint8_t sweep_countdown{};
};

class WaveChannel {
public:

    void write_wave_ram_byte(std::uint8_t index, std::uint8_t value) {
        wave_ram[index] = value;
    }

    void write_dac(std::uint8_t v) {
        dac_enabled = (v & 0x80) != 0;
        if (!dac_enabled) {
            enabled = false;
        }
    }

    void write_length(std::uint8_t v) {
        channel_countdown = 256 - v;
    }

    void write_volume(std::uint8_t v) {
        std::uint8_t code = (v >> 5) & 0x03;
        static constexpr std::uint8_t shift_table[4] = {4, 0, 1, 2};
        volume_shift = shift_table[code];
    }

    void write_freq_low(std::uint8_t v) {
        frequency = (frequency & 0x700) | v;
    }

    void write_freq_high_control(std::uint8_t v) {
        frequency = (frequency & 0x0FF) | ((v & 0x07) << 8);
        countdown_enabled = (v & 0x40) != 0;
        if (v & 0x80) {
            trigger_now();
        }
    }

    void trigger_now() {
        enabled = true;
        sample_index = 0;
        frequency_timer_counter = (2'048 - frequency) * 2;
    }

    void tick_length() {
        if (countdown_enabled) {
            if (channel_countdown > 0) {
                channel_countdown -= 1;
                if (channel_countdown == 0) enabled = false;
            }
        }
    }

    std::uint8_t current_sample() const {
        std::uint8_t byte = wave_ram[sample_index / 2];
        if (sample_index % 2 == 0) {
            return byte >> 4;
        } else {
            return byte & 0x0F;
        }
    }

    void step(std::uint64_t cycles) {
        frequency_timer_counter += cycles;
        while (frequency_timer_counter >= (2'048 - frequency) * 2) {
            frequency_timer_counter -= (2'048 - frequency) * 2;
            sample_index = (sample_index + 1) % 32;
        }
    }

    std::int16_t output() const {
        if (!enabled || !dac_enabled) { return 0; }
        std::uint8_t shifted = current_sample() >> volume_shift;
        return static_cast<std::int16_t>(shifted) - (8 >> volume_shift);
    }


private:
    bool enabled{false};
    bool countdown_enabled{false};
    std::uint16_t channel_countdown{256};
    std::array<std::uint8_t, 16> wave_ram{};
    std::uint8_t sample_index{0};
    std::uint8_t volume_shift{};
    bool dac_enabled{false};
    std::uint16_t frequency{};
    std::uint32_t frequency_timer_counter{0};

};

class NoiseChannel {
public:
    void write_length(std::uint8_t v) {
        channel_countdown = 64 - (v & 0x3F);
    }

    void write_envelope_reg(std::uint8_t v) {
        current_volume     = (v >> 4) & 0x0F;
        envelope_direction = (v & 0x08) ? 1 : -1;
        envelope_period    = v & 0x07;
    }

    void write_poly_reg(std::uint8_t v) {
        shift   = (v >> 4) & 0x0F;
        lfsr_15 = (v & 0x08) == 0;
        divisor = v & 0x07;
    }

    void write_control(std::uint8_t v) {
        countdown_enabled = (v & 0x40) != 0;
        if (v & 0x80) {
            trigger_now();
        }
    }

    void trigger_now() {
        enabled = true;
        envelope_countdown = envelope_period;
        lfsr = 0x7FFF;
        frequency_timer_counter = 0;
    }

    void tick_length() {
        if (countdown_enabled) {
            if (channel_countdown > 0) {
                channel_countdown -= 1;
                if (channel_countdown == 0) enabled = false;
            }
        }
    }

    void tick_envelope() {
        if (envelope_period == 0) return;
        if (envelope_countdown > 0) {
            envelope_countdown -= 1;
            if (envelope_countdown == 0) {
                envelope_countdown = envelope_period;
                if (envelope_direction > 0 && current_volume < 15) {
                    current_volume += 1;
                } else if (envelope_direction < 0 && current_volume > 0) {
                    current_volume -= 1;
                }
            }
        }
    }

    std::uint32_t noise_period() const {
        std::uint32_t divisor_value = (divisor == 0) ? 8 : (divisor * 16);
        return divisor_value << shift;
    }

    void step(std::uint64_t cycles) {
        frequency_timer_counter += cycles;
        while (frequency_timer_counter >= noise_period()) {
            frequency_timer_counter -= noise_period();
            std::uint16_t feedback = (lfsr & 1) ^ ((lfsr >> 1) & 1);
            lfsr >>= 1;
            lfsr |= (feedback << 14);
            if (!lfsr_15) {
                lfsr &= ~(1 << 6);
                lfsr |= (feedback << 6);
            }
        }
    }

    std::int16_t output() const {
        if (!enabled) { return 0; }
        std::uint8_t bit = (~lfsr) & 1;
        std::int16_t level = (bit == 1) ? current_volume : -current_volume;
        return level;
    }


private:
    bool enabled{false};
    bool countdown_enabled{false};
    std::uint8_t channel_countdown{64};
    std::uint8_t current_volume{};
    std::int8_t  envelope_direction{};
    std::uint8_t envelope_period{};
    std::uint8_t envelope_countdown{};
    std::uint16_t lfsr{};
    bool          lfsr_15{true};
    std::uint8_t divisor{};
    std::uint8_t shift{};
    std::uint32_t frequency_timer_counter{};
};

class APU {
public:

    void step(const std::uint64_t cycles) {
        total_cycles += cycles;

        square_channel_1.step(cycles);
        square_channel_2.step(cycles);
        wave_channel.step(cycles);
        noise_channel.step(cycles);
        frame_sequencer_counter += cycles;

        while (frame_sequencer_counter >= Frame_Sequencer_Period) {
            frame_sequencer_counter -= Frame_Sequencer_Period;
            tick_frame_sequencer();
        }
        sample_accumulator += Sample_Rate * cycles;
        while (sample_accumulator >= Clock_Hz) {
            sample_accumulator -= Clock_Hz;
            capture_sample();
        }
    }

    void write_wav(const std::string& path) const {
        std::ofstream file(path, std::ios::binary);

        const std::uint32_t sample_count   = output_samples.size();
        const std::uint32_t data_bytes     = sample_count * 2;
        const std::uint16_t channels       = 1;
        const std::uint16_t bits_per_sample = 16;
        const std::uint32_t byte_rate      = Sample_Rate * channels * (bits_per_sample / 8);
        const std::uint16_t block_align    = channels * (bits_per_sample / 8);

        auto write_u32 = [&](std::uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
        auto write_u16 = [&](std::uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };

        file.write("RIFF", 4);
        write_u32(36 + data_bytes);
        file.write("WAVE", 4);

        file.write("fmt ", 4);
        write_u32(16);
        write_u16(1);
        write_u16(channels);
        write_u32(Sample_Rate);
        write_u32(byte_rate);
        write_u16(block_align);
        write_u16(bits_per_sample);

        file.write("data", 4);
        write_u32(data_bytes);
        file.write(reinterpret_cast<const char*>(output_samples.data()), data_bytes);
    }

    void write_register(std::uint16_t address, std::uint8_t value) {

        if (address >= 0xFF30 && address <= 0xFF3F) {
            wave_channel.write_wave_ram_byte(address - 0xFF30, value);
            return;
        }

        switch (address) {
            case 0xFF10: square_channel_1.write_sweep_reg(value);         break;
            case 0xFF11: square_channel_1.write_duty_length(value);       break;
            case 0xFF12: square_channel_1.write_envelope_reg(value);      break;
            case 0xFF13: square_channel_1.write_freq_low(value);          break;
            case 0xFF14: square_channel_1.write_freq_high_control(value); break;

            case 0xFF16: square_channel_2.write_duty_length(value);       break;
            case 0xFF17: square_channel_2.write_envelope_reg(value);      break;
            case 0xFF18: square_channel_2.write_freq_low(value);          break;
            case 0xFF19: square_channel_2.write_freq_high_control(value); break;

            case 0xFF20: noise_channel.write_length(value);               break;
            case 0xFF21: noise_channel.write_envelope_reg(value);         break;
            case 0xFF22: noise_channel.write_poly_reg(value);             break;
            case 0xFF23: noise_channel.write_control(value);              break;

            case 0xFF1A: wave_channel.write_dac(value);                   break;
            case 0xFF1B: wave_channel.write_length(value);                break;
            case 0xFF1C: wave_channel.write_volume(value);                break;
            case 0xFF1D: wave_channel.write_freq_low(value);              break;
            case 0xFF1E: wave_channel.write_freq_high_control(value);     break;

            default: break;
        }
    }


private:
    void tick_frame_sequencer() {
        if (frame_sequencer_step % 2 == 0) {
            square_channel_1.tick_length();
            square_channel_2.tick_length();
            wave_channel.tick_length();
            noise_channel.tick_length();
        }
        if (frame_sequencer_step % 4 == 2) {
            square_channel_1.tick_sweep();
        }
        if (frame_sequencer_step == 7) {
            square_channel_1.tick_envelope();
            square_channel_2.tick_envelope();
            noise_channel.tick_envelope();
        }
        frame_sequencer_step = (frame_sequencer_step + 1) % 8;
    }

    void capture_sample() {
        std::int16_t level_ch_1 = square_channel_1.output();
        std::int16_t level_ch_2 = square_channel_2.output();
        std::int16_t level_ch_3 = wave_channel.output();
        std::int16_t level_ch_4 = noise_channel.output();
        std::int16_t sample = (level_ch_1 + level_ch_2 + level_ch_3 + level_ch_4) * 500;
        output_samples.push_back(sample);
    }

    std::uint64_t total_cycles{0};
    std::uint64_t frame_sequencer_counter{0};
    std::uint8_t frame_sequencer_step{0};
    SquareChannel square_channel_1{};
    SquareChannel square_channel_2{};
    WaveChannel   wave_channel{};
    NoiseChannel  noise_channel{};
    std::uint64_t sample_accumulator{0};
    std::vector<std::int16_t> output_samples{};
};

void play_vgm(APU& apu, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    std::println("Loaded {} bytes from {}", data.size(), path);

    if (data.size() < 0x40) {
        std::println("File too small / not a VGM");
        return;
    }

    auto read_u32 = [&](std::size_t off) -> std::uint32_t {
        return data[off] | (data[off+1] << 8) | (data[off+2] << 16) | (data[off+3] << 24);
    };

    std::uint32_t data_offset = read_u32(0x34);
    std::size_t p = (data_offset == 0) ? 0x40 : (0x34 + data_offset);

    auto do_wait = [&](std::uint32_t samples) {
        std::uint64_t cycles = static_cast<std::uint64_t>(samples) * Clock_Hz / 44'100;
        std::uint64_t done = 0;
        while (done < cycles) {
            apu.step(Cycles_Per_Step);
            done += Cycles_Per_Step;
        }
    };

    while (p < data.size()) {
        std::uint8_t cmd = data[p];

        if (cmd == 0xB3) {
            std::uint8_t reg = data[p+1];
            std::uint8_t val = data[p+2];
            apu.write_register(0xFF10 + reg, val);
            p += 3;
        } else if (cmd == 0x61) {
            std::uint16_t n = data[p+1] | (data[p+2] << 8);
            do_wait(n);
            p += 3;
        } else if (cmd == 0x62) {
            do_wait(735);
            p += 1;
        } else if (cmd == 0x63) {
            do_wait(882);
            p += 1;
        } else if (cmd >= 0x70 && cmd <= 0x7F) {
            do_wait((cmd & 0x0F) + 1);
            p += 1;
        } else if (cmd == 0x66) {
            break;
        } else {
            p += 1;
        }
    }
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::println("usage: {} <file.vgm>", argv[0]);
        return 1;
    }

    APU apu;
    play_vgm(apu, argv[1]);
    apu.write_wav("output (1).wav");
    std::println("Wrote output.wav");
    return 0;
}
