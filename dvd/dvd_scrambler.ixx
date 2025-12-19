module;
#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

export module dvd.scrambler;

import cd.cdrom;
import dvd.raw;
import utils.misc;



namespace gpsxre
{

export class DVD_Scrambler
{
public:
    bool descramble(uint8_t *sector, int32_t *lba, uint32_t size = DATA_FRAME_SIZE) const
    {
        bool unscrambled = false;

        // zeroed or not enough data to analyze
        if(is_zeroed(sector, size) || size < DATA_FRAME_SIZE)
            return unscrambled;

        // unscramble sector
        process(sector, sector, 0, size);

        auto frame = (DataFrame *)sector;

        // EDC matches
        if(frame->edc == DVD_EDC().update((uint8_t *)sector.data(), sector.length() - 4).final())
            unscrambled = true;

        // if unsuccessful, scramble sector back
        if(!unscrambled)
            process(sector, sector, 0, size);

        return unscrambled;
    }


    static void process(uint8_t *output, const uint8_t *data, uint32_t offset, uint32_t size)
    {
        for(uint32_t i = 0; i < size; ++i)
            output[i] = data[i] ^ _TABLE[offset + i];
    }

private:
    static constexpr auto _TABLE = []()
    {
        std::array<uint8_t, DATA_FRAME_SIZE> table{};

        // ECMA-268

        uint16_t shift_register = 0x0001;

        for(uint16_t i = offsetof(DataFrame, main_data); i < FORM1_DATA_SIZE; ++i)
        {
            table[i] = (uint8_t)shift_register;

            for(uint8_t b = 0; b < CHAR_BIT; ++b)
            {
                // each bit in the input stream of the scrambler is added modulo 2 to the least significant bit of a maximum length register
                bool carry = (shift_register & 1) ^ (shift_register >> 1 & 1);
                // the 15-bit register is of the parallel block synchronized type, and fed back according to polynomial x15 + x + 1
                shift_register = ((uint16_t)carry << 15 | shift_register) >> 1;
            }
        }

        return table;
    }();
};

}
