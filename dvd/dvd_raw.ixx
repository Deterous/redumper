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


std::vector<uint8_t> mediatek_dvd_cache_extract(const std::vector<uint8_t> &cache, int32_t lba)
{
    std::vector<uint8_t> data;

    // detect and return unique frame by ID and validate it using IED
    // check first 4 bytes of each MEDIATEK_CACHE_SIZE frame
    // if there are two frames with validated IDs, check prior/next frame is valid and adjacent number

    return data;
}


void mediatek_dvd_cache(Context &ctx, std::fstream &fs_raw, std::fstream &fs_state, Options &options)
{
    std::vector<uint8_t> cache;

    auto status = asus_cache_read(*ctx.sptd, cache, 1024 * 1024 * asus_get_config(ctx.drive_config.type).size_mb);
    if(status.status_code)
        throw_line("read cache failed, SCSI ({})", SPTD::StatusMessage(status));

    cache = mediatek_dvd_cache_extract(cache, 0);

    // write_entry(fs_raw, file_data.data(), RECORDING_FRAME_SIZE, lba - DVD_LBA_START, sectors_read, 0);
    // write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), lba - DVD_LBA_START, sectors_read, 0);
}

void read_raw_dvd(Context &ctx, std::fstream &fs_raw, std::fstream &fs_state, Options &options)
{
    if(drive_is_asus(ctx.drive_config))
        mediatek_dvd_cache(ctx, fs_raw, fs_state, options);
}

}
