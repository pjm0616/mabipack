// Copyright (c) 2013 Park Jeongmin (pjm0616@gmail.com)
// See LICENSE for details.

#include <string>
#include <list>
#include <vector>
#include <map>
#include <sstream>

#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#include "mabipack.h"
#include "wildcard.h"


// utilities
time_t filetime_to_unix_ts(uint64_t filetime, int utc_offset=MABIPACK_DEFAULT_TIMEZONE)
{
	return filetime / 10000000 - 11644473600 - utc_offset;
}

const char *format_filetime(uint64_t filetime)
{
	int utcoff = MABIPACK_DEFAULT_TIMEZONE;
	time_t ts = filetime_to_unix_ts(filetime, utcoff);
	static char buf[512];
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z (Assuming KST)", localtime(&ts));
	return buf;
}

static bool check_patterns(const std::vector<const char *> &patterns, const std::string &name)
{
	if (patterns.empty()) {
		return true;
	}
	for (const char *pattern : patterns) {
		if (wc_match_nocase(pattern, name.c_str())) {
			return true;
		}
	}
	return false;
}

static void str_split(std::vector<std::string> &out, const std::string &str, char delim)
{
	std::stringstream ss(str);
	std::string buf;
	while (std::getline(ss, buf, delim)) {
		out.push_back(buf);
	}
}

static int mkdir_recursive(const std::string &path, bool ignore_last_elem=false)
{
	std::vector<std::string> elems;
	str_split(elems, path, '/');
	std::string buf("./");
	for (auto it = elems.begin(); it != elems.end(); ++it) {
		std::string &elem = *it;
		if (elem == "..") {
			return -1;
		}
		if (ignore_last_elem && (it + 1 == elems.end())) {
			break;
		}
		buf += elem + "/";
		int ret = mkdir(buf.c_str(), 0755);
		if (ret != 0 && errno != EEXIST) {
			perror("mkdir");
			return -1;
		}
	}
	return 0;
}

static int extract_file(MabiPack &pack, const std::string &name, const file_info &entry)
{
	int ret = mkdir_recursive(name, true);
	if (ret < 0) {
		// failed to make directory or there were invalid characters in filename.
		return -1;
	}

	int fd = open(name.c_str(), O_CREAT | O_WRONLY, 0644);
	if (fd < 0) {
		perror("open");
		return -1;
	}

	char *data = pack.readfile(entry);
	if (data == nullptr) {
		close(fd);
		unlink(name.c_str());
		fprintf(stderr, "Cannot extract file: %s\n", name.c_str());
		return -1;
	}
	int nwrite = write(fd, data, entry.size_orig);
	if (nwrite != (int)entry.size_orig) {
		if (nwrite < 0) {
			perror("write");
		} else {
			fprintf(stderr, "short write: %d, %d\n", nwrite, entry.size_orig);
		}
		close(fd);
		unlink(name.c_str());
		delete[] data;
		return -1;
	}

	close(fd);
	delete[] data;
	return 0;
}

// verbs
typedef int (*mabipack_verb_t)(MabiPack &pack, const std::vector<const char *> &patterns);

static int do_extract(MabiPack &pack, const std::vector<const char *> &patterns)
{
	for (auto &entry : pack) {
		if (check_patterns(patterns, entry.first)) {
			printf("%s\n", entry.first.c_str());
			int ret = extract_file(pack, entry.first, entry.second);
			if (ret < 0) {
				fprintf(stderr, "Error extracting the package. aborting...\n");
				return -1;
			}
		}
	}
	return 0;
}

static int do_list(MabiPack &pack, const std::vector<const char *> &patterns)
{
	const package_header &hdr = pack.header();
	printf("Version number: %d\n", hdr.version);
	printf("Creation date: %s\n", format_filetime(hdr.time1));
	printf("Mountpoint: %s\n", hdr.mountpoint);
	printf("====================\n");
	uint32_t cnt = 0;
	uint64_t total_size = 0;
	for (auto &entry : pack) {
		if (check_patterns(patterns, entry.first)) {
			printf("%.2f KiB\t%s\n", entry.second.size_orig / 1024.0f, entry.first.c_str());
			cnt += 1;
			total_size += entry.second.size_orig;
		}
	}
	printf("Total %d file(s), %.2f MiB\n", cnt, total_size / 1048576.0f);
	return 0;
}

static int do_usage(char *argv[])
{
	fprintf(stderr, "Usage: %s <options> <packfile> [patterns...]\n", argv[0]);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-h - help message\n");
	fprintf(stderr, "\t-l - list files in the package\n");
	fprintf(stderr, "\t-e - extract files in the package (default)\n");
	fprintf(stderr, "\t-d - set output directory\n");
	return 0;
}

// main
int main(int argc, char *argv[])
{
	// parse args
	mabipack_verb_t func = do_extract;
	int opt;
	const char *extract_dir = "./";
	while ((opt = getopt(argc, argv, "hled:")) != -1) {
		switch (opt) {
		case 'h':
			do_usage(argv);
			exit(EXIT_SUCCESS);

		case 'l':
			func = do_list;
			break;

		case 'e':
			func = do_extract;
			break;

		case 'd':
			extract_dir = optarg;
			break;
		}
	}
	if (optind >= argc) {
		fprintf(stderr, "Error: Expected packfile argument after options.\n");
		do_usage(argv);
		exit(EXIT_FAILURE);
	}

	const char *packfile = argv[optind++];
	std::vector<const char *> patterns;
	for (int i = optind; i < argc; i++) {
		patterns.push_back(argv[i]);
	}

	MabiPack pack;
	int ret = pack.openpack(packfile);
	if (ret != 0) {
		fprintf(stderr, "ERROR: Cannot open packfile: %d\n", ret);
		exit(EXIT_FAILURE);
	}

	if (func == do_extract) {
		ret = chdir(extract_dir);
		if (ret != 0) {
			ret = mkdir(extract_dir, 0755);
			if (ret != 0) {
				perror("mkdir");
				exit(EXIT_FAILURE);
			}
			ret = chdir(extract_dir);
			if (ret != 0) {
				perror("chdir");
				exit(EXIT_FAILURE);
			}
		}
	}

	ret = func(pack, patterns);
	return ret;
}

