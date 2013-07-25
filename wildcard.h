#pragma once

bool wc_match(const char *mask, const char *str);
bool wc_match_nocase(const char *mask, const char *str);

static inline bool wc_match(const std::string &mask, const std::string &str)
{
	return wc_match(mask.c_str(), str.c_str());
}
static inline bool wc_match_nocase(const std::string &mask, const std::string &str)
{
	return wc_match_nocase(mask.c_str(), str.c_str());
}


