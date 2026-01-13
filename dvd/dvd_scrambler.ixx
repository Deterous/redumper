module;
#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

export module dvd.scrambler;

import cd.cdrom;
import dvd.edc;
import dvd.raw;
import utils.misc;



namespace gpsxre
{

export class DVD_Scrambler
{
public:
    bool descramble(uint8_t *sector, uint32_t lba, uint32_t size = DATA_FRAME_SIZE) const
    {
        bool unscrambled = false;

        // zeroed or not enough data to analyze
        if(is_zeroed(sector, size) || size < DATA_FRAME_SIZE)
            return unscrambled;

        auto frame = (DataFrame *)sector;

        // validate sector header
        if(frame->id.lba() != lba || !validate_id(sector))
            return unscrambled;

        // unscramble sector
        process(sector, sector, lba / 16, 0, size);

        if(frame->edc == DVD_EDC().update(sector, offsetof(DataFrame, edc)).final())
            unscrambled = true;

        // if EDC does not match, scramble sector back
        if(!unscrambled)
            process(sector, sector, lba / 16, 0, size);

        return unscrambled;
    }


    static void process(uint8_t *output, const uint8_t *data, uint32_t group, uint32_t offset, uint32_t size)
    {
        for(uint32_t i = 0; i < size; ++i)
            output[i] = data[i] ^ _TABLE[group][offset + i];
    }

private:
    static constexpr auto _TABLE = []()
    {
        std::array<uint16_t, 16> INITIAL_VALUES = { 0x0001, 0x5500, 0x0002, 0x2A00, 0x0004, 0x5400, 0x0008, 0x2800, 0x0010, 0x5000, 0x0020, 0x2001, 0x0040, 0x4002, 0x0080, 0x0005 };
        std::array<std::array<uint8_t, DATA_FRAME_SIZE>, 16> table{};

        // ECMA-267

        for(uint8_t group = 0; group < 16; ++group)
        {
            uint16_t shift_register = INITIAL_VALUES[group];

            table[group][offsetof(DataFrame, main_data)] = (uint8_t)shift_register;

            for(uint16_t i = offsetof(DataFrame, main_data) + 1; i < offsetof(DataFrame, edc); ++i)
            {
                for(uint8_t b = 0; b < CHAR_BIT; ++b)
                {
                    // new LSB = b14 XOR b10
                    bool lsb = (shift_register >> 14) ^ (shift_register >> 10);
                    // 15-bit register requires masking MSB
                    shift_register = ((shift_register << 1) & 0x7FFF) | lsb;
                }

                table[group][i] = (uint8_t)shift_register;
            }
        }

        return table;
    }();
};

export class Nintendo_Scrambler
{
public:
    bool descramble(uint8_t *sector, uint32_t lba, uint32_t index, uint32_t size = DATA_FRAME_SIZE) const
    {
        bool unscrambled = false;

        // zeroed or not enough data to analyze
        if(is_zeroed(sector, size) || size < DATA_FRAME_SIZE)
            return unscrambled;

        auto frame = (NintendoDataFrame *)sector;

        // validate sector header
        if(frame->id.lba() != lba || !validate_id(sector))
            return unscrambled;

        // function to determine index:
        // uint32_t sum = 0;
        // for(uint8_t i = 0; i < 8; ++i)
        // ....sum += discID[i];
        // uint32_t index = ((sum >> 4) + sum) & 0x0F

        // unscramble sector
        process(sector, sector, index, 0, size);

        if(frame->edc == DVD_EDC().update(sector, offsetof(DataFrame, edc)).final())
            unscrambled = true;

        // if EDC does not match, scramble sector back
        if(!unscrambled)
            process(sector, sector, index, 0, size);

        return unscrambled;
    }


    static void process(uint8_t *output, const uint8_t *data, uint32_t index, uint32_t offset, uint32_t size)
    {
        for(uint32_t i = 0; i < size; ++i)
            output[i] = data[i] ^ _TABLE[index][offset + i];
    }

private:
    static constexpr auto _TABLE = []()
    {
        std::array<uint16_t, 16> INITIAL_VALUES = { 0x0003, 0x0030, 0x7F00, 0x7001, 0x0006, 0x0045, 0x7E00, 0x6003, 0x000C, 0x00C0, 0x7C00, 0x4007, 0x0018, 0x0180, 0x7800, 0x000F };
        std::array<std::array<uint8_t, DATA_FRAME_SIZE>, 16> table{};

        // "Nintendo Game Disk"

        for(uint8_t index = 0; index < 16; ++index)
        {
            uint16_t shift_register = INITIAL_VALUES[index];

            table[index][offsetof(DataFrame, main_data)] = (uint8_t)shift_register;

            for(uint16_t i = offsetof(DataFrame, main_data) + 1; i < offsetof(DataFrame, edc); ++i)
            {
                for(uint8_t b = 0; b < CHAR_BIT; ++b)
                {
                    // new LSB = b14 XOR b10
                    bool lsb = (shift_register >> 14) ^ (shift_register >> 10);
                    // 15-bit register requires masking MSB
                    shift_register = ((shift_register << 1) & 0x7FFF) | lsb;
                }

                table[index][i] = (uint8_t)shift_register;
            }
        }

        return table;
    }();
};

}
