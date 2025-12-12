module;
#include <cstdint>

export module dvd.raw;

import cd.cdrom;



namespace gpsxre
{

export const uint32_t PSN_START = 0x30000;
export const uint32_t DATA_FRAME_SIZE = 2064;
export const uint32_t RECORDING_FRAME_SIZE = 2366;


export struct DataFrame
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


export struct NintendoDataFrame
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


export struct RecordingFrame
{
    struct Row
    {
        uint8_t user_data[172];
        uint8_t parity_inner[10];
    };

    Row row[12];
    uint8_t parity_outer[182];
};


export struct CacheFrame2384
{
    RecordingFrame recording_frame;
    uint8_t unknown[18];
};

}
