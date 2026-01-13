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
    bool descramble(uint8_t *sector, uint32_t psn, uint32_t size = DATA_FRAME_SIZE, std::optional<std::uint32_t> ngd_index = std::nullopt) const
    {
        bool unscrambled = false;

        // zeroed or not enough data to analyze
        if(is_zeroed(sector, size) || size < sizeof(DataFrame::id) + sizeof(DataFrame::ied))
            return unscrambled;

        auto frame = (DataFrame *)sector;

        // validate sector header
        if(frame->id.psn() != psn || !validate_id(sector))
            return unscrambled;

        // unscramble sector
        uint32_t group = ngd_index.value_or(psn / 16);
        process(sector, sector, group, 0, size, ngd_index.has_value());

        if(frame->edc == DVD_EDC().update(sector, offsetof(DataFrame, edc)).final())
            unscrambled = true;

        // if EDC does not match, scramble sector back
        if(!unscrambled)
            process(sector, sector, group, 0, size, ngd_index.has_value());

        return unscrambled;
    }


    static void process(uint8_t *output, const uint8_t *data, uint32_t group, uint32_t offset, uint32_t size, bool ngd = false)
    {
        if(ngd)
        {
            for(uint32_t i = 0; i < size; ++i)
                output[i] = data[i] ^ _NGD_TABLE[group][offset + i];
        }
        else
        {
            for(uint32_t i = 0; i < size; ++i)
                output[i] = data[i] ^ _DVD_TABLE[group][offset + i];
        }
    }

private:
    static constexpr auto make_table(std::array<uint16_t, 16> iv)
    {
        std::array<std::array<uint8_t, DATA_FRAME_SIZE>, 16> table{};

        // ECMA-267

        for(uint8_t group = 0; group < 16; ++group)
        {
            uint16_t shift_register = iv[group];

            table[group][MAIN_DATA_OFFSET] = (uint8_t)shift_register;

            for(uint16_t i = MAIN_DATA_OFFSET + 1; i < DATA_FRAME_SIZE - sizeof(DataFrame::edc); ++i)
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
    }

    static constexpr std::array<uint16_t, 16> _DVD_IV = { 0x0001, 0x5500, 0x0002, 0x2A00, 0x0004, 0x5400, 0x0008, 0x2800, 0x0010, 0x5000, 0x0020, 0x2001, 0x0040, 0x4002, 0x0080, 0x0005 };
    static constexpr std::array<uint16_t, 16> _NGD_IV = { 0x0003, 0x0030, 0x7F00, 0x7001, 0x0006, 0x0045, 0x7E00, 0x6003, 0x000C, 0x00C0, 0x7C00, 0x4007, 0x0018, 0x0180, 0x7800, 0x000F };
    static constexpr auto _DVD_TABLE = make_table(_DVD_IV);
    static constexpr auto _NGD_TABLE = make_table(_NGD_IV);
};

}
