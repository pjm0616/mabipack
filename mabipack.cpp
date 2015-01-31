// Copyright (c) 2013 Park Jeongmin (pjm0616@gmail.com)
// See LICENSE for details.

#include <string>
#include <list>
#include <map>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>

#include <zlib.h>

#include "mabipack.h"
#include "mt19937ar.h"


MabiPack::MabiPack()
{
}

MabiPack::~MabiPack()
{
	this->closepack();
}

int MabiPack::openpack(const std::string &path)
{
	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0) {
		return -1;
	}

	int nread = ::read(fd, &this->header_, sizeof (this->header_));
	if (nread != sizeof (this->header_)) {
		::close(fd);
		return -2;
	}
	if (std::memcmp(this->header_.magic, "PACK", 4)) {
		::close(fd);
		return -3;
	}
	if (std::memcmp(this->header_.pack_revision, "\2\1\0\0", 4)) {
		::close(fd);
		return -4;
	}
	this->header_.mountpoint[sizeof(this->header_.mountpoint) - 1] = '\0';
	this->fd_ = fd;

	for (unsigned int i = 0; i < this->header_.filecnt; i++) {
		filelist_t::value_type entry = this->read_fileinfo(fd);
		if (entry.first.empty()) {
			this->closepack();
			return -5;
		}
		this->files_.insert(entry);
	}

	return 0;
}

MabiPack::filelist_t::value_type MabiPack::read_fileinfo(int fd)
{
	int nread;

	// read filename length
	char nametype;
	uint32_t namelen;
	nread = ::read(fd, &nametype, 1);
	if (nametype < 4) {
		namelen = (0x10 * (nametype + 1)) - 1;
	} else if (nametype == 4) {
		namelen = 0x60 - 1;
	} else if (nametype == 5) {
		::read(fd, &namelen, 4);
	} else {
		return filelist_t::value_type();
	}

	// read filename
	char filename[512];
	if (namelen >= sizeof (filename) - 1) {
		return filelist_t::value_type();
	}
	nread = ::read(fd, filename, namelen);
	if (nread != (int)namelen) {
		return filelist_t::value_type();
	}
	filename[namelen] = 0;

	// convert windows style path separators to unix style
	for (char *p = filename; p < filename + namelen; p++) {
		if (*p == '\\') {
			*p = '/';
		}
	}

	// read fileinfo
	file_info entry;
	nread = ::read(fd, &entry, sizeof (entry));
	if (nread != sizeof (entry)) {
		return filelist_t::value_type();
	}

	return std::make_pair(std::string(filename), entry);
}

int MabiPack::closepack()
{
	::close(this->fd_);
	this->fd_ = -1;
	this->files_.clear();
	return 0;
}

char *MabiPack::decode_file_contents(const MabiPack::file_info &entry, char *compressed)
{
	uint32_t seed = (entry.seed << 7) ^ 0xa9c36de1;
	mt19937ar mt(seed);
	for (unsigned int i = 0; i < entry.size_compressed; i++) {
		compressed[i] ^= mt.genrand_int32();
	}

	uLongf outlen = entry.size_orig;
	char *data = new char[entry.size_orig];
	int ret = uncompress((Bytef *)data, &outlen, (Bytef *)compressed, entry.size_compressed);
	if (ret != Z_OK || outlen != entry.size_orig) {
		fprintf(stderr, "uncompress: %d\n", ret);
		delete[] data;
		return nullptr;
	}

	return data;
}

char *MabiPack::readfile(const MabiPack::file_info &entry)
{
	if (entry.size_compressed == 0) {
		return nullptr;
	}
	if (!entry.is_compressed) {
		// we do not support uncompressed files.
		return nullptr;
	}

	uint32_t data_section_off = sizeof (this->header_) + this->header_.fileinfo_size;
	::lseek(this->fd_, data_section_off + entry.offset, SEEK_SET);
	char *compressed = new char[entry.size_compressed];
	int nread = ::read(this->fd_, compressed, entry.size_compressed);
	if (nread != (int)entry.size_compressed) {
		delete[] compressed;
		return nullptr;
	}

	char *data = this->decode_file_contents(entry, compressed);
	delete[] compressed;
	return data;
}

char *MabiPack::readfile(const std::string &path)
{
	return this->readfile(this->files_[path]);
}

