#include <stdlib.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

int get_callinfo(char *fname, size_t fnlen, unsigned long long *ofs)
{
	unw_context_t context;
	unw_cursor_t cursor;
	int ret;

	if(unw_getcontext(&context)) {
		return -1;
	}
	if(unw_init_local(&cursor, &context)) {
		return -1;
	}

	unw_step(&cursor);
	unw_step(&cursor);
	unw_step(&cursor);

	ret = unw_get_proc_name(&cursor, fname, fnlen, (unw_word_t *)ofs);
	(*ofs) -= 5;

	return ret;
}
