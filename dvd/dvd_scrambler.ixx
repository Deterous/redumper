module;
#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

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
    bool descramble(uint8_t *sector, uint32_t psn, uint32_t size = DATA_FRAME_SIZE, std::optional<std::uint32_t> ngd_id = std::nullopt) const
    {
        bool unscrambled = false;

        // zeroed or not enough data to analyze
        if(is_zeroed(sector, size) || size < sizeof(DataFrame::id) + sizeof(DataFrame::ied))
            return unscrambled;

        auto frame = (DataFrame *)sector;

        // validate sector header
        if(frame->id.psn() != psn || !validate_id(sector))
            return unscrambled;

        // determine initial table offset
        uint32_t offset;
        if(psn >= 0x030010 && ngd_id.has_value())
            offset += ((psn >> 4 & 0xF) ^ ngd_id.value()) * FORM1_DATA_SIZE + 0x3C00;
        else
            offset = (psn >> 4 & 0xF) * FORM1_DATA_SIZE;

        // unscramble sector
        process(sector, sector, offset, size);

        if(frame->edc == DVD_EDC().update(sector, offsetof(DataFrame, edc)).final())
            unscrambled = true;

        // if EDC does not match, scramble sector back
        if(!unscrambled)
            process(sector, sector, offset, size);

        return unscrambled;
    }


    static void process(uint8_t *output, const uint8_t *data, uint32_t offset, uint32_t size)
    {
        for(uint32_t i = offsetof(DataFrame, main_data); i < offsetof(DataFrame, edc) && i < size; ++i)
            output[i] = data[i] ^ _TABLE[offset + i - offsetof(DataFrame, main_data)];
    }

private:
    static constexpr auto _TABLE = []()
    {
        static constexpr std::array<uint16_t, 16> iv = { 0x0001, 0x5500, 0x0002, 0x2A00, 0x0004, 0x5400, 0x0008, 0x2800, 0x0010, 0x5000, 0x0020, 0x2001, 0x0040, 0x4002, 0x0080, 0x0005 };

        std::array<uint8_t, FORM1_DATA_SIZE * 16> table{};

        // ECMA-267

        for(uint8_t group = 0; group < 16; ++group)
        {
            uint16_t shift_register = iv[group];

            table[group * FORM1_DATA_SIZE + MAIN_DATA_OFFSET] = (uint8_t)shift_register;

            for(uint16_t i = 0; i < FORM1_DATA_SIZE; ++i)
            {
                for(uint8_t b = 0; b < CHAR_BIT; ++b)
                {
                    // new LSB = b14 XOR b10
                    bool lsb = (shift_register >> 14) ^ (shift_register >> 10);
                    // 15-bit register requires masking MSB
                    shift_register = ((shift_register << 1) & 0x7FFF) | lsb;
                }

                table[group * FORM1_DATA_SIZE + i] = (uint8_t)shift_register;
            }
        }

        return table;
    }();
};

}
