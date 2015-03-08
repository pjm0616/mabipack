// Copyright (c) 2013 Park Jeongmin (pjm0616@gmail.com)
// See LICENSE for details.
#pragma once


// Since Mabinogi is being developed in Korea we assume timestamps are in KST(UTC+9).
static const int MABIPACK_DEFAULT_TIMEZONE = 32400; // UTC+9
static const int MABIPACK_MAX_FILENAME_STORAGE = 256;
// Subtract the size for filename_encoding_method(\x05), filename_length and null_terminator.
static const int MABIPACK_MAX_FILENAME = MABIPACK_MAX_FILENAME_STORAGE - (1 + 4 + 1);

struct package_header
{
	char magic[4];
	char pack_revision[4];
	uint32_t version;
	uint32_t filecnt0;
	uint64_t time1, time2;
	char mountpoint[480];
	uint32_t filecnt;
	uint32_t fileinfo_size;
	uint32_t padding_size;
	uint32_t data_section_size;
	char padding[16];
};

struct file_info
{
	uint32_t seed;
	uint32_t zero;
	uint32_t offset;
	uint32_t size_compressed;
	uint32_t size_orig;
	uint32_t is_compressed;
	uint64_t time1, time2, time3, time4, time5;
};

class MabiPack
{
public:
	typedef std::map<std::string, file_info> filelist_t;

public:
	MabiPack();
	~MabiPack();

	int openpack(const std::string &path);
	int closepack();
	char *readfile(const std::string &path);
	char *readfile(const file_info &entry);

	const package_header &header() const { return header_; }

	filelist_t::iterator begin() { return files_.begin(); }
	filelist_t::iterator end() { return files_.end(); }
	filelist_t::const_iterator begin() const { return files_.begin(); }
	filelist_t::const_iterator end() const { return files_.end(); }

private:
	filelist_t::value_type read_fileinfo(int fd);
	char *decode_file_contents(const file_info &entry, char *compressed);

private:
	int fd_;
	package_header header_;
	filelist_t files_;
};

class MabiPackWriter
{
public:
	MabiPackWriter();
	~MabiPackWriter();

	int open(const std::string &path, uint32_t version, int filecnt, const char *mountpoint="data\\");
	// Returns <0 on error and errno is set appropriately.
	int addfile(const std::string &path);
	// Returns <0 on error and errno is set appropriately.
	int commit();
	void discard();

private:
	int write_filename(const char *name);

private:
	int fd_;
	int next_idx_;
	package_header header_;
	std::vector<std::pair<std::string, file_info>> files_;
	uint64_t creation_filetime_;
};

// Utility class. todo: move this to somewhere else.
class PreserveErrno
{
public:
	PreserveErrno()
		: orig_errno_(errno)
	{
	}
	~PreserveErrno()
	{
		errno = orig_errno_;
	}

private:
	int orig_errno_;
};

