// Copyright (c) 2013 Park Jeongmin (pjm0616@gmail.com)
// See LICENSE for details.

#include <string>
#include <list>
#include <map>
#include <vector>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <zlib.h>

#include "mabipack.h"
#include "mt19937ar.h"

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error This program only works under little endian cpus.
#endif


uint64_t unix_ts_to_filetime(time_t unix_ts, int utc_offset=MABIPACK_DEFAULT_TIMEZONE)
{
	return (unix_ts + utc_offset + 11644473600) * 10000000;
}


MabiPack::MabiPack()
	: fd_(-1)
{
}

MabiPack::~MabiPack()
{
	closepack();
}

int MabiPack::openpack(const std::string &path)
{
	assert(fd_ < 0);

	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0) {
		return -1;
	}

	int nread = ::read(fd, &header_, sizeof (header_));
	if (nread != sizeof (header_)) {
		::close(fd);
		return -2;
	}
	if (std::memcmp(header_.magic, "PACK", 4)) {
		::close(fd);
		return -3;
	}
	if (std::memcmp(header_.pack_revision, "\2\1\0\0", 4)) {
		::close(fd);
		return -4;
	}
	header_.mountpoint[sizeof(header_.mountpoint) - 1] = '\0';
	fd_ = fd;

	for (unsigned int i = 0; i < header_.filecnt; i++) {
		filelist_t::value_type entry = read_fileinfo(fd);
		if (entry.first.empty()) {
			closepack();
			return -5;
		}
		files_.insert(entry);
	}

	return 0;
}

MabiPack::filelist_t::value_type MabiPack::read_fileinfo(int fd)
{
	assert(fd_ >= 0);

	int nread;

	// read filename length
	char nametype;
	uint32_t namelen;
	nread = ::read(fd, &nametype, 1);
	if (nread != 1) {
		return filelist_t::value_type();
	} else if (nametype < 4) {
		namelen = (0x10 * (nametype + 1)) - 1;
	} else if (nametype == 4) {
		namelen = 0x60 - 1;
	} else if (nametype == 5) {
		nread = ::read(fd, &namelen, 4);
		if (nread != 4) {
			return filelist_t::value_type();
		}
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
	if (fd_ >= 0) {
		::close(fd_);
		fd_ = -1;
		files_.clear();
	}
	return 0;
}

char *MabiPack::decode_file_contents(const file_info &entry, char *compressed)
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

char *MabiPack::readfile(const file_info &entry)
{
	assert(fd_ >= 0);

	if (entry.size_compressed == 0) {
		return nullptr;
	}
	if (!entry.is_compressed) {
		// we do not support uncompressed files.
		return nullptr;
	}

	uint32_t data_section_off = sizeof (header_) + header_.fileinfo_size;
	int ret = ::lseek(fd_, data_section_off + entry.offset, SEEK_SET);
	if (ret < 0) {
		return nullptr;
	}
	char *compressed = new char[entry.size_compressed];
	int nread = ::read(fd_, compressed, entry.size_compressed);
	if (nread != (int)entry.size_compressed) {
		delete[] compressed;
		return nullptr;
	}

	char *data = decode_file_contents(entry, compressed);
	delete[] compressed;
	return data;
}

char *MabiPack::readfile(const std::string &path)
{
	return readfile(files_[path]);
}


MabiPackWriter::MabiPackWriter()
	: fd_(-1)
{
}

MabiPackWriter::~MabiPackWriter()
{
	discard();
}

int MabiPackWriter::open(const std::string &path, uint32_t version, int filecnt, const char *mountpoint)
{
	assert(fd_ < 0);

	if (::strlen(mountpoint) >= sizeof(header_.mountpoint) - 1) {
		return -2;
	}

	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT, 0644);
	if (fd < 0) {
		return -1;
	}

	next_idx_ = 0;
	files_.resize(filecnt);
	creation_filetime_ = unix_ts_to_filetime(time(nullptr));

	::memset(&header_, 0, sizeof(header_));
	::memcpy(header_.magic, "PACK", 4);
	::memcpy(header_.pack_revision, "\2\1\0\0", 4);
	header_.version = version;
	header_.filecnt0 = header_.filecnt = filecnt;
	header_.time1 = header_.time2 = creation_filetime_;
	::snprintf(header_.mountpoint, sizeof(header_.mountpoint), "%s", mountpoint);
	// Allocate maximum possible space for filename.
	int fileinfo_pure_size = (MABIPACK_MAX_FILENAME_STORAGE + sizeof(file_info)) * filecnt;
	header_.padding_size = 1024 - (fileinfo_pure_size % 1024);
	header_.fileinfo_size = fileinfo_pure_size + header_.padding_size;

	int ret = ::lseek(fd, sizeof(package_header) + header_.fileinfo_size, SEEK_SET);
	if (ret < 0) {
		close(fd);
		return -3;
	}

	fd_ = fd;
	return 0;
}

int MabiPackWriter::commit()
{
	assert(fd_ >= 0);

	int ret;

	off_t size = ::lseek(fd_, 0, SEEK_CUR);
	if (size < 0) {
		return -1;
	}

	// Write file metadata
	ret = ::lseek(fd_, sizeof(package_header), SEEK_SET);
	if (ret < 0) {
		return -2;
	}
	for (const std::pair<std::string, file_info> &entry : files_) {
		ret = write_filename(entry.first.c_str());
		if (ret != 0) {
			return -3;
		}
		ret = ::write(fd_, &entry.second, sizeof(entry.second));
		if (ret != sizeof(entry.second)) {
			return -4;
		}
	}
	ret = ::lseek(fd_, 0, SEEK_CUR);
	if (ret < 0 || (unsigned int)ret >= sizeof(package_header) + header_.fileinfo_size) {
		return -5;
	}

	ret = ::lseek(fd_, 0, SEEK_SET);
	header_.data_section_size = size - sizeof(package_header) + header_.fileinfo_size;
	ret = ::write(fd_, &header_, sizeof(header_));
	if (ret != sizeof(header_)) {
		return -6;
	}

	::close(fd_);
	fd_ = -1;

	return 0;
}

void MabiPackWriter::discard()
{
	if (fd_ >= 0) {
		::close(fd_);
		fd_ = -1;
	}
}

int MabiPackWriter::addfile(const std::string &path)
{
	assert(fd_ >= 0);

	int seed = 0;
	int ret;

	if (path.size() > MABIPACK_MAX_FILENAME) {
		errno = EINVAL;
		return -7;
	}

	off_t offset = ::lseek(fd_, 0, SEEK_CUR);
	if (offset < 0) {
		return -6;
	}

	int filefd = ::open(path.c_str(), O_RDONLY);
	if (filefd < 0) {
		return -1;
	}

	struct stat sb;
	ret = ::fstat(filefd, &sb);
	if (ret < 0) {
		return -8;
	}

	off_t filesize = ::lseek(filefd, 0, SEEK_END);
	ret = ::lseek(filefd, 0, SEEK_SET);
	if (filesize < 0 || ret < 0) {
		{
			PreserveErrno pe;
			close(filefd);
		}
		return -2;
	}

	char *buf = new char[filesize];
	ret = ::read(filefd, buf, filesize);
	if (ret != filesize) {
		{
			PreserveErrno pe;
			::close(filefd);
			delete[] buf;
		}
		return -3;
	}
	::close(filefd);

	uLongf complen = compressBound(filesize);
	char *compbuf = new char[complen];
	// TODO: Use streaming compression.
	ret = compress2((Bytef *)compbuf, &complen, (const Bytef *)buf, filesize, 9);
	delete[] buf;
	if (ret != Z_OK) {
		delete[] compbuf;
		errno = EIO;
		return -4;
	}

	mt19937ar mt((seed << 7) ^ 0xa9c36de1);
	for (unsigned int i = 0; i < complen; i++) {
		compbuf[i] ^= mt.genrand_int32();
	}

	ret = ::write(fd_, compbuf, complen);
	delete[] compbuf;
	if (ret != (int)complen) {
		return -5;
	}

	auto &entry = files_[next_idx_++];
	entry.first = path;
	entry.second.seed = 0;
	entry.second.zero = 0;
	entry.second.offset = offset - sizeof (header_) - header_.fileinfo_size;
	entry.second.size_orig = filesize;
	entry.second.size_compressed = complen;
	entry.second.is_compressed = 1;
	entry.second.time1 = entry.second.time2 = entry.second.time4 = entry.second.time5
		= creation_filetime_;
	entry.second.time3 = unix_ts_to_filetime(sb.st_mtime);

	return 0;
}

int MabiPackWriter::write_filename(const char *name)
{
	assert(fd_ >= 0);

	int len = strlen(name);
	char *buf = new char[5 + len + 1];
	buf[0] = '\x05';
	*(uint32_t *)&buf[1] = len;
	memcpy(&buf[5], name, len + 1);

	// convert unix style path separators to windows style
	for (char *p = &buf[5]; p < buf + len; p++) {
		if (*p == '/') {
			*p = '\\';
		}
	}

	int ret = ::write(fd_, buf, 5 + len);
	if (ret != 5 + len) {
		return -1;
	}
	return 0;
}

