
#include <string>
#include <cstring>

#include "wildcard.h"

template <typename CASE_CONVERTER>
bool wc_match_tmpl(const char *mask, const char *str)
{
	const char *cp = NULL, *mp = NULL;
	CASE_CONVERTER case_converter;
	
	while ((*str) && (*mask != '*')) {
		if ((case_converter(*mask) != case_converter(*str)) && (*mask != '?')) 	{
			return false;
		}
		mask++;
		str++;
	}
	while (*str) {
		if (*mask == '*') {
			if (!*++mask) {
				return true;
			}
			mp = mask;
			cp = str+1;
		} else if ((case_converter(*mask) == case_converter(*str)) || (*mask == '?')) {
			mask++;
			str++;
		} else {
			mask = mp;
			str = cp++;
		}
	}
	while (*mask == '*') {
		mask++;
	}
	
	return *mask ? false : true;
}

struct case_conv
{
	inline char operator()(char c) const
	{
		return c;
	}
};
bool wc_match(const char *mask, const char *str)
{
	return wc_match_tmpl<case_conv>(mask, str);
}

struct nocase_conv
{
	inline char operator()(char c) const
	{
		return tolower(c);
	}
};
bool wc_match_nocase(const char *mask, const char *str)
{
	return wc_match_tmpl<nocase_conv>(mask, str);
}

