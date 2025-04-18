module;

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <list>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "throw_line.hh"

export module skeleton;

import cd.cd;
import cd.cdrom;
import cd.common;
import common;
import filesystem.iso9660;
import options;
import readers.sector_reader;
import readers.image_bin_form1_reader;
import readers.image_iso_form1_reader;
import utils.animation;
import utils.logger;
import utils.misc;



namespace gpsxre
{

const uint8_t EXO_MAGIC[] = { '.', 'E', 'X', 'O' };
const uint32_t EXO_VER = 0;

typedef std::tuple<std::string, uint32_t, uint32_t, uint32_t> ContentEntry;


void progress_output(std::string name, uint64_t value, uint64_t value_count)
{
    char animation = value == value_count ? '*' : spinner_animation();

    LOGC_RF("{} [{:3}%] {}", animation, value * 100 / value_count, name);
}


bool inside_contents(const std::vector<ContentEntry> &contents, uint32_t value)
{
    for(auto const &c : contents)
        if(value >= std::get<1>(c) && value < std::get<1>(c) + std::get<2>(c))
            return true;

    return false;
}


void erase_sector(uint8_t *s, bool iso)
{
    if(iso)
        memset(s, 0x00, FORM1_DATA_SIZE);
    else
    {
        auto sector = (Sector *)s;

        if(sector->header.mode == 1)
            memset(sector->mode1.user_data, 0x00, FORM1_DATA_SIZE);
        else if(sector->header.mode == 2)
        {
            if(sector->mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
                memset(sector->mode2.xa.form2.user_data, 0x00, FORM2_DATA_SIZE);
            else
                memset(sector->mode2.xa.form1.user_data, 0x00, FORM1_DATA_SIZE);
        }
        else
            memset(sector->mode2.user_data, 0x00, MODE0_DATA_SIZE);
    }
}


void write_sector(std::fstream &fs, std::vector<uint8_t> &data, bool iso)
{
    if(iso)
        fs.write((char *)data.data(), FORM1_DATA_SIZE);
    else
    {
        auto sector = (Sector *)data.data();

        if(sector->header.mode == 1)
            fs.write((char *)sector->mode1.user_data, FORM1_DATA_SIZE);
        else if(sector->header.mode == 2)
        {
            if(sector->mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
                fs.write((char *)sector->mode2.xa.form2.user_data, FORM2_DATA_SIZE);
            else
                fs.write((char *)sector->mode2.xa.form1.user_data, FORM1_DATA_SIZE);
        }
    }
}


void write_exo(std::fstream &fs, std::vector<uint8_t> &data, uint32_t sector_num, TrackType track_type)
{
    auto sector = (Sector *)data.data();

    bool bad_sector = false;

    if(std::memcmp(sector->sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
    {
        if(!bad_sector)
        {
            bad_sector = true;
            fs.write((char *)&sector_num, sizeof(sector_num));
        }
        fs.put(0x01);
        fs.write((char *)sector->sync, sizeof(sector->sync));
    }

    MSF msf = LBA_to_BCDMSF(sector_num);
    if(std::memcmp(sector->header.address.raw, msf.raw, sizeof(sector->header.address.raw)))
    {
        if(!bad_sector)
        {
            bad_sector = true;
            fs.write((char *)&sector_num, sizeof(sector_num));
        }
        fs.put(0x02);
        fs.write((char *)sector->header.address.raw, sizeof(sector->header.address.raw));
    }

    uint8_t mode_byte = track_type == TrackType::MODE1_2352 ? 0x01 : track_type == TrackType::MODE2_2352 ? 0x02 : 0x00;
    if(std::memcmp(&sector->header.mode, &mode_byte, sizeof(mode_byte)))
    {
        if(!bad_sector)
        {
            bad_sector = true;
            fs.write((char *)&sector_num, sizeof(sector_num));
        }
        fs.put(0x03);
        fs.write((char *)&sector->header.mode, sizeof(sector->header.mode));
    }

    if(sector->header.mode == 1)
    {
        // todo: calculate EDC
        if(std::memcmp(sector->mode1.intermediate, CD_DATA_INTERMEDIATE, sizeof(CD_DATA_INTERMEDIATE)))
        {
            if(!bad_sector)
            {
                bad_sector = true;
                fs.write((char *)&sector_num, sizeof(sector_num));
            }
            fs.put(0x05);
            fs.write((char *)sector->mode1.intermediate, sizeof(sector->mode1.intermediate));
        }
        // todo: calculate ECC
    }
    else if(sector->header.mode == 2)
    {
        // todo: calculate subheader
        if(sector->mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
        {
            // todo: calculate edc
        }
        else
        {
            // todo: calculate edc
            // todo: calculate ecc
        }
    }
}


void skeleton(const std::string &image_prefix, const std::string &image_path, bool iso, TrackType track_type, Options &options)
{
    std::filesystem::path skeleton_path(image_prefix + ".skeleton");
    std::filesystem::path hash_path(image_prefix + ".hash");
    std::filesystem::path exo_path(image_prefix + ".exo");

    if(!options.overwrite && (std::filesystem::exists(skeleton_path) || std::filesystem::exists(hash_path) || std::filesystem::exists(exo_path)))
        throw_line("skeleton/hash/exo file already exists");

    std::unique_ptr<SectorReader> sector_reader;
    if(iso)
        sector_reader = std::make_unique<Image_ISO_Reader>(image_path);
    else
        sector_reader = std::make_unique<Image_BIN_Form1Reader>(image_path);

    uint32_t sectors_count = std::filesystem::file_size(image_path) / (iso ? FORM1_DATA_SIZE : CD_DATA_SIZE);

    auto area_map = iso9660::area_map(sector_reader.get(), 0, sectors_count);
    if(area_map.empty())
        return;

    if(options.debug)
    {
        LOG("ISO9660 map: ");
        std::for_each(area_map.cbegin(), area_map.cend(),
            [](const iso9660::Area &area)
            {
                auto count = scale_up(area.size, FORM1_DATA_SIZE);
                LOG("LBA: [{:6} .. {:6}], count: {:6}, type: {}{}", area.offset, area.offset + count - 1, count, iso9660::area_type_to_string(area.type),
                    area.name.empty() ? "" : std::format(", name: {}", area.name));
            });
    }

    std::vector<ContentEntry> contents;
    for(uint32_t i = 0; i + 1 < area_map.size(); ++i)
    {
        auto const &a = area_map[i];

        std::string name(a.name.empty() ? iso9660::area_type_to_string(a.type) : a.name);

        if(a.type == iso9660::Area::Type::SYSTEM_AREA || a.type == iso9660::Area::Type::FILE_EXTENT)
            contents.emplace_back(name, a.offset, scale_up(a.size, sector_reader->sectorSize()), a.size);

        uint32_t gap_start = a.offset + scale_up(a.size, sector_reader->sectorSize());
        if(gap_start < area_map[i + 1].offset)
        {
            uint32_t gap_size = area_map[i + 1].offset - gap_start;

            // 5% or more in relation to the total filesystem size
            if((uint64_t)gap_size * 100 / sectors_count > 5)
                contents.emplace_back(std::format("GAP_{:07}", gap_start), gap_start, gap_size, gap_size * sector_reader->sectorSize());
        }
    }

    uint64_t contents_sectors_count = 0;
    for(auto const &c : contents)
        contents_sectors_count += std::get<2>(c);

    std::fstream hash_fs(hash_path, std::fstream::out);
    if(!hash_fs.is_open())
        throw_line("unable to create file ({})", hash_path.filename().string());

    uint32_t contents_sectors_processed = 0;
    for(auto const &c : contents)
    {
        progress_output(std::format("hashing {}", std::get<0>(c)), contents_sectors_processed, contents_sectors_count);

        bool xa = false;
        hash_fs << std::format("{} {}", sector_reader->calculateSHA1(std::get<1>(c), std::get<2>(c), std::get<3>(c), false, &xa), std::get<0>(c)) << std::endl;

        if(xa)
            hash_fs << std::format("{} {}.XA", sector_reader->calculateSHA1(std::get<1>(c), std::get<2>(c), std::get<3>(c), true), std::get<0>(c)) << std::endl;

        contents_sectors_processed += std::get<2>(c);
    }
    progress_output("hashing complete", contents_sectors_processed, contents_sectors_count);
    LOGC("");

    std::fstream image_fs(image_path, std::fstream::in | std::fstream::binary);
    if(!image_fs.is_open())
        throw_line("unable to open file ({})", image_path);

    std::fstream skeleton_fs(skeleton_path, std::fstream::out | std::fstream::binary);
    if(!skeleton_fs.is_open())
        throw_line("unable to create file ({})", skeleton_path.filename().string());

    std::vector<uint8_t> sector(iso ? FORM1_DATA_SIZE : CD_DATA_SIZE);
    std::fstream exo_fs;
    if(!iso)
    {
        exo_fs.open(exo_path, std::fstream::out | std::fstream::binary);
        if(!exo_fs.is_open())
            throw_line("unable to create file ({})", exo_path.filename().string());

        exo_fs.write((char *)EXO_MAGIC, sizeof(EXO_MAGIC));
        if(exo_fs.fail())
            throw_line("write failed ({})", exo_path.filename().string());
        exo_fs.write((char *)(&EXO_VER), sizeof(EXO_VER));
        if(exo_fs.fail())
            throw_line("write failed ({})", exo_path.filename().string());
        exo_fs.write((char *)(&sectors_count), sizeof(sectors_count));
        if(exo_fs.fail())
            throw_line("write failed ({})", exo_path.filename().string());
        exo_fs.write((char *)(&track_type), sizeof(track_type));
        if(exo_fs.fail())
            throw_line("write failed ({})", exo_path.filename().string());
    }
    for(uint32_t s = 0; s < sectors_count; ++s)
    {
        progress_output(iso ? "creating skeleton" : "creating exo/skeleton", s, sectors_count);

        image_fs.read((char *)sector.data(), sector.size());
        if(image_fs.fail())
            throw_line("read failed ({})", image_path);

        if(inside_contents(contents, s))
            erase_sector(sector.data(), iso);

        write_sector(skeleton_fs, sector, iso);
        if(skeleton_fs.fail())
            throw_line("write failed ({})", skeleton_path.filename().string());

        if(!iso)
        {
            write_exo(exo_fs, sector, s, track_type);
            if(exo_fs.fail())
                throw_line("write failed ({})", exo_path.filename().string());
        }
    }
    progress_output(iso ? "creating skeleton" : "creating exo/skeleton", sectors_count, sectors_count);

    LOGC("");
}


export int redumper_skeleton(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    if(std::filesystem::exists(image_prefix + ".cue"))
    {
        for(auto const &t : cue_get_entries(image_prefix + ".cue"))
        {
            // supported track types only
            if(t.second == TrackType::MODE1_2352 || t.second == TrackType::MODE2_2352)
            {
                auto track_prefix = (std::filesystem::path(options.image_path) / std::filesystem::path(t.first).stem()).string();

                skeleton(track_prefix, (std::filesystem::path(options.image_path) / t.first).string(), false, t.second, options);
            }
        }
    }
    else if(std::filesystem::exists(image_prefix + ".iso"))
    {
        skeleton(image_prefix, image_prefix + ".iso", true, TrackType::ISO, options);
    }
    else
        throw_line("image file not found");

    return exit_code;
}

}
