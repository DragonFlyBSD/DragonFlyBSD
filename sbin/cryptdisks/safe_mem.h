void *_alloc_safe_mem(size_t req_sz, const char *file, int line);
void _free_safe_mem(void *mem, const char *file, int line);
void check_and_purge_safe_mem(void);

#define alloc_safe_mem(x) \
	_alloc_safe_mem(x, __FILE__, __LINE__)

#define free_safe_mem(x) \
	_free_safe_mem(x, __FILE__, __LINE__)
