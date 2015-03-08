// Copyright (c) 2013 Park Jeongmin (pjm0616@gmail.com)
// See LICENSE for details.

#include <string>
#include <list>
#include <vector>
#include <map>
#include <set>
#include <sstream>

#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>

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

// Program options
static const char *g_program_name;
static const char *g_packfile;
static std::vector<const char *> g_arglist;
// extract only
static const char *g_extract_dir = "./";
// create only
static int g_pack_version = 0;
static const char *g_pack_mountpoint = "data\\";

// verbs
typedef int (*mabipack_verb_t)();

static int do_extract()
{
	MabiPack pack;
	int ret = pack.openpack(g_packfile);
	if (ret != 0) {
		fprintf(stderr, "ERROR: Cannot open packfile: %d\n", ret);
		return EXIT_FAILURE;
	}

	ret = chdir(g_extract_dir);
	if (ret != 0) {
		ret = mkdir(g_extract_dir, 0755);
		if (ret != 0) {
			perror("mkdir");
			return EXIT_FAILURE;
		}

		ret = chdir(g_extract_dir);
		if (ret != 0) {
			perror("chdir");
			return EXIT_FAILURE;
		}
	}

	for (auto &entry : pack) {
		if (check_patterns(g_arglist, entry.first)) {
			printf("%s\n", entry.first.c_str());
			int ret = extract_file(pack, entry.first, entry.second);
			if (ret < 0) {
				fprintf(stderr, "Error extracting the package. aborting...\n");
				return EXIT_FAILURE;
			}
		}
	}

	return EXIT_SUCCESS;
}

static int do_list()
{
	MabiPack pack;
	int ret = pack.openpack(g_packfile);
	if (ret != 0) {
		fprintf(stderr, "ERROR: Cannot open packfile: %d\n", ret);
		return EXIT_FAILURE;
	}

	const package_header &hdr = pack.header();
	printf("Version number: %d\n", hdr.version);
	printf("Creation date: %s\n", format_filetime(hdr.time1));
	printf("Mountpoint: %s\n", hdr.mountpoint);
	printf("====================\n");
	uint32_t cnt = 0;
	uint64_t total_size = 0;
	for (auto &entry : pack) {
		if (check_patterns(g_arglist, entry.first)) {
			printf("%.2f KiB\t%s\n", entry.second.size_orig / 1024.0f, entry.first.c_str());
			cnt += 1;
			total_size += entry.second.size_orig;
		}
	}
	printf("Total %d file(s), %.2f MiB\n", cnt, total_size / 1048576.0f);

	return EXIT_SUCCESS;
}

// Note: trailing slashes must not be present in path argument.
static int collect_files(std::list<std::string> &result, std::set<std::string> &result_set, const std::string &path)
{
	struct stat sb;
	int ret = ::stat(path.c_str(), &sb);
	if (ret < 0) {
		fprintf(stderr, "ERROR: Cannot stat %s: %s\n", path.c_str(), strerror(errno));
		return -1;
	}

	if (S_ISREG(sb.st_mode)) {
		if (result_set.count(path)) {
			return 0;
		}
		result.push_back(path);
		result_set.insert(path);
	} else if (S_ISDIR(sb.st_mode)) {
		DIR *dp = opendir(path.c_str());
		if (!dp) {
			return -2;
		}

		struct dirent *entry;
		errno = 0;
		while ((entry = readdir(dp)) != NULL) {
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
				continue;
			}

			ret = collect_files(result, result_set, path + "/" + entry->d_name);
			if (ret < 0) {
				{
					PreserveErrno ep;
					closedir(dp);
				}
				return -3;
			}

			errno = 0;
		}
		{
			PreserveErrno ep;
			closedir(dp);
		}
		if (errno != 0) {
			return -4;
		}
	} else {
		errno = ESOCKTNOSUPPORT;
		return -5;
	}

	return 0;
}
static int do_create()
{
	std::list<std::string> files;
	std::set<std::string> files_set;
	for (const char *name : g_arglist) {
		// Get the length of `name' without trailing slashes.
		int i = strlen(name) - 1;
		while (i >= 0 && name[i] == '/') {
			i--;
		}
		i++;
		if (i == 0) {
			fprintf(stderr, "ERROR: Empty filename in argument list\n");
			return EXIT_FAILURE;
		}
		std::string sname(name, i);
		int ret = collect_files(files, files_set, sname);
		if (ret < 0) {
			fprintf(stderr, "ERROR: Failed to collect filelist(%d): %s: %s\n", ret, sname.c_str(), strerror(errno));
			return EXIT_FAILURE;
		}
	}

	fprintf(stdout, "Creating package %s\n", g_packfile);
	fprintf(stdout, "Pack version: %d\n", g_pack_version);
	fprintf(stdout, "Mountpoint: %s\n", g_pack_mountpoint);
	fprintf(stdout, "Number of files: %lu\n", files.size());

	MabiPackWriter pack_writer;
	int ret = pack_writer.open(g_packfile, g_pack_version, files.size(), g_pack_mountpoint);
	if (ret != 0) {
		fprintf(stderr, "ERROR: Cannot open packfile: %d\n", ret);
		return EXIT_FAILURE;
	}

	for (const std::string &path : files) {
		fprintf(stdout, "Adding file %s\n", path.c_str());
		ret = pack_writer.addfile(path);
		if (ret < 0) {
			fprintf(stderr, "ERROR: Cannot add file(%d): %s: %s\n", ret, path.c_str(), strerror(errno));
			pack_writer.discard();
			return EXIT_FAILURE;
		}
	}

	ret = pack_writer.commit();
	if (ret < 0) {
		fprintf(stderr, "ERROR: Cannot write package header(%d): %s\n", ret, strerror(errno));
		pack_writer.discard();
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int do_usage()
{
	fprintf(stderr, "Usage: %s <options> <packfile> [patterns...]\n", g_program_name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-h - help message\n");
	fprintf(stderr, "\t-l - list files in the package\n");
	fprintf(stderr, "\t-e - extract files in the package (default)\n");
	fprintf(stderr, "\t-c - create a new package\n");
	fprintf(stderr, "\t-d - set output directory (extract only)\n");
	fprintf(stderr, "\t-v - set package version (create only)\n");
	fprintf(stderr, "\t-m - set package mountpoint (create only)\n");

	return EXIT_SUCCESS;
}

// main
int main(int argc, char *argv[])
{
	// parse args
	g_program_name = argv[0];
	mabipack_verb_t func = do_extract;
	int opt;
	while ((opt = getopt(argc, argv, "hlecd:v:m:")) != -1) {
		switch (opt) {
		case 'h':
			do_usage();
			exit(EXIT_SUCCESS);

		case 'l':
			func = do_list;
			break;

		case 'e':
			func = do_extract;
			break;

		case 'c':
			func = do_create;
			break;

		case 'd':
			g_extract_dir = optarg;
			break;

		case 'v':
			g_pack_version = atoi(optarg);
			break;

		case 'm':
			g_pack_mountpoint = optarg;
			break;
		}
	}
	if (optind >= argc) {
		fprintf(stderr, "Error: Expected packfile argument after options.\n");
		do_usage();
		exit(EXIT_FAILURE);
	}

	g_packfile = argv[optind++];
	for (int i = optind; i < argc; i++) {
		g_arglist.push_back(argv[i]);
	}

	return func();
}

