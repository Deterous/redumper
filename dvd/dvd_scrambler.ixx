module;
#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

export module dvd.scrambler;

import cd.cdrom;
import dvd.edc;
import dvd.raw;
import utils.endian;
import utils.hex_bin;
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

        // determine XOR table offset
        uint32_t offset = (psn >> 4 & 0xF) * FORM1_DATA_SIZE;

        // custom XOR table offset for user data area
        if(ngd_id.has_value() && psn >= 0x030000 && psn <= 0x0DE0B0)
        {
            if(psn >= 0x030010)
            {
                uint32_t shift = ngd_id.value() ^ (psn >> 4 & 0xF);
                offset = (shift + 7.5) * FORM1_DATA_SIZE + (shift > 8);
            }
            else
                offset += 7.5 * FORM1_DATA_SIZE;
        }

        // unscramble sector
        process(sector, sector, offset, size);

        if(endian_swap(frame->edc) == DVD_EDC().update(sector, offsetof(DataFrame, edc)).final())
            unscrambled = true;

        // if EDC does not match, scramble sector back
        if(!unscrambled)
            process(sector, sector, offset, size);

        return unscrambled;
    }


    static void process(uint8_t *output, const uint8_t *data, uint32_t offset, uint32_t size)
    {
        uint32_t main_data_offset = offsetof(DataFrame, main_data);
        uint32_t end_byte = size < offsetof(DataFrame, edc) ? size : offsetof(DataFrame, edc);
        for(uint32_t i = main_data_offset; i < end_byte; ++i)
            output[i] = data[i] ^ _TABLE[offset + i - main_data_offset];
    }

private:
    static constexpr auto _TABLE = []()
    {
        std::array<uint8_t, FORM1_DATA_SIZE * ECC_FRAMES + FORM1_DATA_SIZE> table{};

        // ECMA-267

        std::array<uint16_t, ECC_FRAMES> iv = { 0x0001, 0x5500, 0x0002, 0x2A00, 0x0004, 0x5400, 0x0008, 0x2800, 0x0010, 0x5000, 0x0020, 0x2001, 0x0040, 0x4002, 0x0080, 0x0005 };
        for(uint8_t group = 0; group < ECC_FRAMES; ++group)
        {
            uint16_t shift_register = iv[group];

            table[group * FORM1_DATA_SIZE] = (uint8_t)shift_register;

            // extend table to account for custom offsets
            uint16_t group_length = group == ECC_FRAMES - 1 ? 2 * FORM1_DATA_SIZE : FORM1_DATA_SIZE;

            for(uint16_t i = 1; i < group_length; ++i)
            {
                for(uint8_t b = 0; b < CHAR_BIT; ++b)
                {
                    // new LSB = b14 XOR b10
                    bool lsb = (shift_register >> 14 & 1) ^ (shift_register >> 10 & 1);
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
