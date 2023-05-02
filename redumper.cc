#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include "cmd.hh"
#include "crc16_gsm.hh"
#include "crc32.hh"
#include "dump.hh"
#include "dump_cd.hh"
#include "dump_dvd.hh"
#include "file_io.hh"
#include "logger.hh"
#include "scrambler.hh"
#include "signal.hh"
#include "split.hh"
#include "subcode.hh"
#include "redumper.hh"

import version;



namespace gpsxre
{

std::map<std::string, void (*)(Options &)> COMMAND_HANDLERS =
{
	{"cd", redumper_cd},
	{"dump", redumper_dump},
	{"refine", redumper_refine},
	{"protection", redumper_protection},
	{"split", redumper_split},
	{"info", redumper_info},
	{"subchannel", redumper_subchannel},
	{"debug", redumper_debug}
};


void redumper(Options &options)
{
	normalize_options(options);

	Logger::Get().Reset((std::filesystem::path(options.image_path) / options.image_name).string() + ".log");

	LOG("{}\n", redumper_version());
	LOG("command line: {}\n", options.command_line);

	for(auto const &p : options.commands)
	{
		LOG("*** COMMAND: {}", p);

		auto it = COMMAND_HANDLERS.find(p);
		if(it == COMMAND_HANDLERS.end())
		{
			LOG("warning: unknown command, skipping ({})", p);
			continue;
		}

		it->second(options);
	}
}


std::string redumper_version()
{
	return std::format("redumper v{}.{}.{} build_{} [{}]", XSTRINGIFY(REDUMPER_VERSION_MAJOR), XSTRINGIFY(REDUMPER_VERSION_MINOR),
			XSTRINGIFY(REDUMPER_VERSION_PATCH), XSTRINGIFY(REDUMPER_VERSION_BUILD), version::build());
}


void normalize_options(Options &options)
{
	// default command
	if(options.commands.empty())
		options.commands.push_back("cd");

	bool drive_required = false;
	bool generate_name = false;
	for(auto const &p : options.commands)
	{
		if(p == "cd" || p == "dump" || p == "refine")
			drive_required = true;

		if(p == "cd" || p == "dump")
			generate_name = true;
	}

	// autodetect drive
	if(drive_required && options.drive.empty())
	{
		options.drive = first_ready_drive();

		if(options.drive.empty())
			throw_line("no ready drives detected on the system");
	}

	// add drive colon if unspecified
#ifdef _WIN32
	if(!options.drive.empty())
	{
		if(options.drive.back() != ':')
			options.drive += ':';
	}
#endif

	// autogenerate image name
	if(generate_name && options.image_name.empty())
	{
		auto drive = options.drive;
		drive.erase(remove(drive.begin(), drive.end(), ':'), drive.end());
		drive.erase(remove(drive.begin(), drive.end(), '/'), drive.end());
		options.image_name = std::format("dump_{}_{}", system_date_time("%y%m%d_%H%M%S"), drive);
	}
}


std::string first_ready_drive()
{
	std::string drive;

	auto drives = SPTD::ListDrives();
	for(const auto &d : drives)
	{
		try
		{
			SPTD sptd(d);

			auto status = cmd_drive_ready(sptd);
			if(!status.status_code)
			{
				drive = d;
				break;
			}
		}
		// drive busy
		catch(const std::exception &)
		{
			;
		}
	}

	return drive;
}


DiscType query_disc_type(std::string drive)
{
	auto disc_type = DiscType::CD;

	SPTD sptd(drive);

	// test unit ready
	SPTD::Status status = cmd_drive_ready(sptd);
	if(status.status_code)
		throw_line(std::format("drive not ready, SCSI ({})", SPTD::StatusMessage(status)));

	GET_CONFIGURATION_FeatureCode_ProfileList current_profile = GET_CONFIGURATION_FeatureCode_ProfileList::RESERVED;
	status = cmd_get_configuration_current_profile(sptd, current_profile);
	if(status.status_code)
		throw_line(std::format("failed to query disc type, SCSI ({})", SPTD::StatusMessage(status)));

	switch(current_profile)
	{
	case GET_CONFIGURATION_FeatureCode_ProfileList::CD_ROM:
	case GET_CONFIGURATION_FeatureCode_ProfileList::CD_R:
	case GET_CONFIGURATION_FeatureCode_ProfileList::CD_RW:
		disc_type = DiscType::CD;
		break;

	case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_ROM:
	case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R:
	case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RAM:
	case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW_RO:
	case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW:
	case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW:
		disc_type = DiscType::DVD;
		break;

	case GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM:
	case GET_CONFIGURATION_FeatureCode_ProfileList::BD_R:
	case GET_CONFIGURATION_FeatureCode_ProfileList::BD_R_RRM:
	case GET_CONFIGURATION_FeatureCode_ProfileList::BD_RE:
		disc_type = DiscType::BLURAY;
		break;

	default:
		throw_line(std::format("unsupported disc type (profile: {})", (uint16_t)current_profile));
	}

	return disc_type;
}


void redumper_cd(Options &options)
{
	auto disc_type = query_disc_type(options.drive);

	if(disc_type == DiscType::CD)
	{
		bool refine = redumper_dump_cd(options, false);
		if(refine)
			redumper_dump_cd(options, true);
		redumper_protection(options);
		redumper_split(options);
		redumper_info(options);
	}
	else
	{
		bool refine = dump_dvd(options, false);
		if(refine)
			dump_dvd(options, true);
	}
}


void redumper_dump(Options &options)
{
	auto disc_type = query_disc_type(options.drive);

	if(disc_type == DiscType::CD)
		redumper_dump_cd(options, false);
	else
		dump_dvd(options, false);
}


void redumper_refine(Options &options)
{
	auto disc_type = query_disc_type(options.drive);

	if(disc_type == DiscType::CD)
		redumper_dump_cd(options, true);
	else
		dump_dvd(options, true);
}


void redumper_protection(Options &options)
{
	redumper_protection_cd(options);
}


void redumper_split(Options &options)
{
	redumper_split_cd(options);
}


void redumper_info(Options &options)
{
	redumper_info_cd(options);
}


void redumper_subchannel(Options &options)
{
	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	std::filesystem::path sub_path(image_prefix + ".subcode");

	uint32_t sectors_count = check_file(sub_path, CD_SUBCODE_SIZE);
	std::fstream sub_fs(sub_path, std::fstream::in | std::fstream::binary);
	if(!sub_fs.is_open())
		throw_line(std::format("unable to open file ({})", sub_path.filename().string()));

	ChannelQ q_empty;
	memset(&q_empty, 0, sizeof(q_empty));

	bool empty = false;
	std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		read_entry(sub_fs, sub_buffer.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);

		ChannelQ Q;
		subcode_extract_channel((uint8_t *)&Q, sub_buffer.data(), Subchannel::Q);

		// Q is available
		if(memcmp(&Q, &q_empty, sizeof(q_empty)))
		{
			int32_t lbaq = BCDMSF_to_LBA(Q.mode1.a_msf);

			LOGC("[LBA: {:6}, LBAQ: {:6}] {}", LBA_START + (int32_t)lba_index, lbaq, Q.Decode());
			empty = false;
		}
		else if(!empty)
		{
			LOG("...");
			empty = true;
		}
	}
}


void redumper_debug(Options &options)
{
	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
	std::filesystem::path state_path(image_prefix + ".state");
	std::filesystem::path cache_path(image_prefix + ".asus");
	std::filesystem::path toc_path(image_prefix + ".toc");
	std::filesystem::path cdtext_path(image_prefix + ".cdtext");
	std::filesystem::path cue_path(image_prefix + ".cue");

	/*
		// popcnt test
		if(1)
		{
			for(uint32_t i = 0; i < 0xffffffff; ++i)
			{
				uint32_t test = __popcnt(i);
				uint32_t test2 = bits_count(i);

				if(test != test2)
					LOG("{} <=> {}", test, test2);
			}
		}
	*/
	// CD-TEXT debug
	if(0)
	{
		std::vector<uint8_t> toc_buffer = read_vector(toc_path);
		TOC toc(toc_buffer, false);

		std::vector<uint8_t> cdtext_buffer = read_vector(cdtext_path);
		toc.updateCDTEXT(cdtext_buffer);

		std::fstream fs(cue_path, std::fstream::out);
		if(!fs.is_open())
			throw_line(std::format("unable to create file ({})", cue_path.string()));
		toc.printCUE(fs, options.image_name, 0);

		LOG("");
	}

	// SBI stats
	if(0)
	{
		std::vector<std::filesystem::path> sbi_files;
		for(auto const &f : std::filesystem::directory_iterator("sbi"))
			sbi_files.push_back(f);
		std::sort(sbi_files.begin(), sbi_files.end());

		std::map<uint32_t, uint32_t> sbi_stats;
		for(auto const &f : sbi_files)
		{
			LOG("{}", f.string());

			auto buffer = read_vector(f);

			ChannelQ Q;
			constexpr uint32_t sbi_magic_size = 4;
			constexpr uint32_t sbi_entry_size = 14;

			uint32_t sectors_count = (buffer.size() - sbi_magic_size) / sbi_entry_size;
			for(uint32_t i = 0; i < sectors_count; ++i)
			{
				auto *b = &buffer[sbi_magic_size + i * sbi_entry_size];

				MSF msf = *(MSF *)b;
				auto lba = BCDMSF_to_LBA(msf);
				++sbi_stats[lba];
				std::cout << std::format("{} ", lba + 150);
				Q = *(ChannelQ *)(b + 4);
				Q.crc = 0;
				//				for(uint32_t j = 0; j < 14; ++j)
				//					std::cout << std::format("{:02X} ", b[j]);
				std::cout << std::format("{}", Q.Decode()) << std::endl;
			}

			LOG("");
		}

		for(auto const &ss : sbi_stats)
		{
			std::cout << std::format("{}, ", ss.first);
		}
		std::cout << std::endl;

		LOG("");
	}

	// LG/ASUS cache read
	if(0)
	{
		SPTD sptd(options.drive);
		auto drive_config = drive_init(sptd, options);

		auto cache = asus_cache_read(sptd, drive_config.type);
	}

	// LG/ASUS cache dump extract
	if(1)
	{
		auto drive_type = DriveConfig::Type::LG_ASU3;
		std::vector<uint8_t> cache = read_vector(cache_path);

		asus_cache_print_subq(cache, drive_type);

		//		auto asd = asus_cache_unroll(cache);
		//		auto asd = asus_cache_extract(cache, 128224, 0);
		auto asus_leadout_buffer = asus_cache_extract(cache, 292353, 100, drive_type);
		uint32_t entries_count = (uint32_t)asus_leadout_buffer.size() / CD_RAW_DATA_SIZE;

		LOG("entries count: {}", entries_count);

		std::ofstream ofs_data(image_prefix + ".asus.data", std::ofstream::binary);
		std::ofstream ofs_c2(image_prefix + ".asus.c2", std::ofstream::binary);
		std::ofstream ofs_sub(image_prefix + ".asus.sub", std::ofstream::binary);
		for(uint32_t i = 0; i < entries_count; ++i)
		{
			uint8_t *entry = &asus_leadout_buffer[CD_RAW_DATA_SIZE * i];

			ofs_data.write((char *)entry, CD_DATA_SIZE);
			ofs_c2.write((char *)entry + CD_DATA_SIZE, CD_C2_SIZE);
			ofs_sub.write((char *)entry + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);
		}
	}


	// convert old state file to new state file
	if(0)
	{
		std::fstream fs_state(state_path, std::fstream::out | std::fstream::in | std::fstream::binary);
		uint64_t states_count = std::filesystem::file_size(state_path) / sizeof(State);
		std::vector<State> states((std::vector<State>::size_type)states_count);
		fs_state.read((char *)states.data(), states.size() * sizeof(State));
		for(auto &s : states)
		{
			uint8_t value = (uint8_t)s;
			if(value == 0)
				s = (State)4;
			else if(value == 1)
				s = (State)3;
			else if(value == 3)
				s = (State)1;
			else if(value == 4)
				s = (State)0;
		}

		fs_state.seekp(0);
		fs_state.write((char *)states.data(), states.size() * sizeof(State));
	}

	LOG("");
}

}
