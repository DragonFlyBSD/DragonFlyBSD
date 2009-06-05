#include <sys/iconv.h>
int main() {
	int error = 0;
	error = kiconv_add_xlat16_cspairs(ENCODING_UNICODE, "UTF-8");
        if(error)
		printf("fail\n");
	error = kiconv_add_xlat16_cspairs("KOI8-R", "UTF-8");
        if(error)
		printf("fail\n");
	return error;
}
