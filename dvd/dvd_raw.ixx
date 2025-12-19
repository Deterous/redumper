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


struct DataFrame
{
    struct
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
    } id;

    uint16_t ied;
    uint8_t cpr_mai[6];
    uint8_t user_data[FORM1_DATA_SIZE];
    uint32_t edc;
};


struct NintendoDataFrame
{
    struct
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
    } id;

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


uint16_t compute_ied(const uint8_t id[4])
{
    // primitive polynomial x^8 + x^4 + x^3 + x^2 + 1
    static GF256 gf(0x11D); // 100011101

    // generator G(x) = x^2 + g1*x + g2
    uint8_t g1 = gf.add(1, gf.exp[1]); // alpha0 + alpha1
    uint8_t g2 = gf.exp[1];            // alpha0 * alpha1

    // initialize coefficients
    uint8_t poly[6] = {0};
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

    return ((uint16_t)poly[4] << 8) | (uint16_t)poly[5];
}


int32_t mediatek_dvd_cache_extract(const std::vector<uint8_t> &cache, const std::vector<uint8_t> &frames, int32_t lba)
{
    // look for sectors with ID near lba
    // detect and return unique frame by ID and validate it using IED
    // check first 4 bytes of each MEDIATEK_CACHE_SIZE frame
    // if there are two frames with validated IDs, check prior/next frame is valid and adjacent number

    return 0;
}


void mediatek_dvd_cache(Context &ctx, std::fstream &fs_raw, std::fstream &fs_state, const Options &options, int32_t lba)
{
    std::vector<uint8_t> cache;
    std::vector<uint8_t> frames;

    auto status = asus_cache_read(*ctx.sptd, cache, 1024 * 1024 * asus_get_config(ctx.drive_config.type).size_mb);
    if(status.status_code)
        throw_line("read cache failed, SCSI ({})", SPTD::StatusMessage(status));

    auto first_lba = mediatek_dvd_cache_extract(cache, frames, lba);
    int sectors_read = cache.size() / RECORDING_FRAME_SIZE;

    write_entry(fs_raw, file_data.data(), RECORDING_FRAME_SIZE, first_lba - DVD_LBA_START, sectors_read, 0);

    // TODO: How to store raw state in state file?
    // write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), first_lba - DVD_LBA_START, sectors_read, 0);
}


export void read_raw_dvd(Context &ctx, std::fstream &fs_raw, std::fstream &fs_state, const Options &options, int32_t lba)
{
    if(drive_is_asus(ctx.drive_config))
        mediatek_dvd_cache(ctx, fs_raw, fs_state, options, lba);
}

}
