module;
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>
#include "throw_line.hh"

export module dvd.raw;

import cd.cdrom;
import common;
import drive.mediatek;
import options;
import scsi.sptd;
import utils.file_io;
import utils.galois;



namespace gpsxre
{


// .raw LBA 0 starts at 0x1BBA0000 byte offset
constexpr uint32_t DVD_LBA_START = -0x30000;
constexpr uint32_t DATA_FRAME_SIZE = 2064;
constexpr uint32_t RECORDING_FRAME_SIZE = 2366;
constexpr uint32_t MEDIATEK_CACHE_SIZE = 2384;



struct IdentificationData
{
    struct
    {
        uint8_t layer_number       :1;
        uint8_t data_type          :1;
        uint8_t zone_type          :2;
        uint8_t reserved           :1;
        uint8_t reflectivity       :1;
        uint8_t tracking_method    :1;
        uint8_t sector_format_type :1;
    } sector_info;

    uint8_t sector_number[3];

    int32_t lba() const
    {
        return (int32_t)((uint32_t(sector_number[2]) << 16) | (uint32_t(sector_number[1]) << 8) | uint32_t(sector_number[0]));
    }
};

struct DataFrame
{
    IdentificationData id;
    uint16_t ied;
    uint8_t cpr_mai[6];
    uint8_t user_data[FORM1_DATA_SIZE];
    uint32_t edc;
};


struct NintendoDataFrame
{
    IdentificationData id;
    uint16_t ied;
    uint8_t user_data[FORM1_DATA_SIZE];
    uint8_t cpr_mai[6];
    uint32_t edc;
};


struct RecordingFrame
{
    struct Row
    {
        uint8_t user_data[172];
        uint8_t parity_inner[10];
    };

    Row row[12];
    uint8_t parity_outer[182];
};


struct MediatekCacheFrame
{
    RecordingFrame recording_frame;
    uint8_t unknown[18];
};


bool validate_id(const uint8_t id[6])
{
    // primitive polynomial x^8 + x^4 + x^3 + x^2 + 1
    static GF256 gf(0x11D); // 100011101

    // generator G(x) = x^2 + g1*x + g2
    uint8_t g1 = gf.add(1, gf.exp[1]); // alpha0 + alpha1
    uint8_t g2 = gf.exp[1];            // alpha0 * alpha1

    // initialize coefficients
    uint8_t poly[6] = { 0 };
    for(uint8_t i = 0; i < 4; ++i)
        poly[i] = id[i];

    // polynomial long division
    for(uint8_t i = 0; i <= 3; ++i)
    {
        uint8_t coef = poly[i];
        if(coef != 0)
        {
            poly[i + 0] = 0;
            poly[i + 1] = gf.add(poly[i + 1], gf.mul(coef, g1));
            poly[i + 2] = gf.add(poly[i + 2], gf.mul(coef, g2));
        }
    }

    return (poly[4] == id[4]) && (poly[5] == id[5]);
}


int32_t mediatek_dvd_cache_extract(const std::vector<uint8_t> &cache, std::vector<uint8_t> &frames, int32_t expected_lba)
{
    int32_t first_lba = DVD_LBA_START - 1;
    int32_t next_lba = DVD_LBA_START;

    if(cache.size() < MEDIATEK_CACHE_SIZE)
        return first_lba;

    uint32_t num_frames = cache.size() / MEDIATEK_CACHE_SIZE;
    for(uint32_t i = 0; i < num_frames; i++)
    {
        auto cache_frame = cache.data() + i * MEDIATEK_CACHE_SIZE;

        if(!validate_id(cache_frame))
        {
            // finish reading from cache if read sectors include expected LBA
            if(first_lba <= expected_lba && next_lba > expected_lba)
                return first_lba;

            continue;
        }

        auto id = (IdentificationData *)cache_frame;
        int32_t lba = id->lba();

        if(next_lba == DVD_LBA_START)
        {
            if(lba < expected_lba - 100 || lba > expected_lba)
                continue;

            first_lba = lba;
            next_lba = lba + 1;
        }
        else if(lba != next_lba)
        {
            if(lba <= expected_lba)
            {
                first_lba = lba;
                next_lba = lba + 1;
                frames.clear();
            }
            else
                return first_lba;
        }
        else
            next_lba++;

        frames.insert(frames.end(), cache_frame, cache_frame + RECORDING_FRAME_SIZE);
    }

    return first_lba;
}


bool mediatek_dvd_cache(Context &ctx, std::fstream &fs_raw, std::fstream &fs_state, const Options &options, int32_t expected_lba)
{
    std::vector<uint8_t> cache;
    std::vector<uint8_t> frames; // TODO: reserve() expected number of frames

    auto status = mediatek_cache_read(*ctx.sptd, cache, 1024 * 1024 * mediatek_get_config(ctx.drive_config.type).size_mb);
    if(status.status_code)
        throw_line("read cache failed, SCSI ({})", SPTD::StatusMessage(status));

    auto first_lba = mediatek_dvd_cache_extract(cache, frames, expected_lba);
    if(first_lba < DVD_LBA_START)
        return false;

    int sectors_read = frames.size() / RECORDING_FRAME_SIZE;
    write_entry(fs_raw, frames.data(), RECORDING_FRAME_SIZE, first_lba - DVD_LBA_START, sectors_read, 0);

    return true;
}


export bool read_raw_dvd(Context &ctx, std::fstream &fs_raw, std::fstream &fs_state, const Options &options, int32_t lba, uint32_t sectors_to_read)
{
    int32_t expected_lba = lba + sectors_to_read - 1;
    if(drive_is_mediatek(ctx.drive_config))
        return mediatek_dvd_cache(ctx, fs_raw, fs_state, options, expected_lba);
}

}
