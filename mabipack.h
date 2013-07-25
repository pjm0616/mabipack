// Copyright (c) 2013 Park Jeongmin (pjm0616@gmail.com)
// See LICENSE for details.
#pragma once

class MabiPack
{
public:
	struct package_header
	{
		char magic[4];
		char pack_revision[4];
		uint32_t version;
		uint32_t sum;
		uint64_t time1, time2;
		char path[480];
		uint32_t filecnt;
		uint32_t fileinfo_size;
		uint32_t blank_size;
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

	typedef std::map<std::string, file_info> filelist_t;

public:
	MabiPack();
	~MabiPack();

	int openpack(const std::string &path);
	int closepack();
	char *readfile(const std::string &path);
	char *readfile(const file_info &entry);

	filelist_t::iterator begin() { return this->_files.begin(); }
	filelist_t::iterator end() { return this->_files.end(); }
	filelist_t::const_iterator begin() const { return this->_files.begin(); }
	filelist_t::const_iterator end() const { return this->_files.end(); }

private:
	filelist_t::value_type _read_fileinfo(int fd);
	char *_decode_file_contents(const file_info &entry, char *compressed);

private:
	int _fd;
	package_header _header;
	filelist_t _files;
};

