/* Execute the actual command_loadall() with a recording loader backend. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CMD_OK 0
#define CMD_ERROR 1
typedef struct variable {
	const char *name;
	char *data[1];
	struct variable *next;
} *dvar_t;
static struct variable variables[8];
static int variable_count;
static const char *DirBase = "";
static const char *firmware_list_value;
static char kernel_name[] = "/boot/kernel/kernel";
static char kernel_options[] = "-v";
static char *kernel_name_value = kernel_name;
static char *kernel_options_value;
static int kernel_result = CMD_OK;

/* Environment values are borrowed, not allocations owned by callers. */
static void loader_free(void *pointer)
{
	assert(pointer != kernel_name && pointer != kernel_options);
	assert(pointer == NULL || pointer != firmware_list_value);
	free(pointer);
}
#define free loader_free
static char records[10][4][128];
static int counts[10], record_count;
static dvar_t dvar_first(void) { return variable_count ? &variables[0] : NULL; }
static dvar_t dvar_next(dvar_t value) { return value->next; }
static void add(const char *name, char *value)
{
	int i = variable_count++;
	variables[i].name = name;
	variables[i].data[0] = value;
	if (i)
		variables[i - 1].next = &variables[i];
}
static char *loader_getenv(const char *name)
{
	if (!strcmp(name, "kernelname"))
		return kernel_name_value;
	if (!strcmp(name, "kernel_options"))
		return kernel_options_value;
	if (!strcmp(name, "firmware"))
		return (char *)firmware_list_value;
	return NULL;
}
#define getenv loader_getenv
static int perform(int argc, char **argv)
{
	assert(record_count < 10 && argc <= 4);
	counts[record_count] = argc;
	for (int i = 0; i < argc; ++i)
		snprintf(records[record_count][i], 128, "%s", argv[i]);
	++record_count;
	if (!strcmp(argv[0], "unload")) {
		kernel_name_value = NULL;
	} else if (argc >= 2 && strcmp(argv[1], "-t") &&
	    kernel_name_value == NULL) {
		if (kernel_result == CMD_OK)
			kernel_name_value = kernel_name;
		return kernel_result;
	}
	return CMD_OK;
}

/* SOURCE_UNDER_TEST */

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (!strcmp(argv[1], "microcode") ||
	    !strcmp(argv[1], "microcode_override")) {
		add("cpu_microcode_load", "YES");
		add("cpu_microcode_name", "/boot/firmware/amd-ucode.bin");
		if (!strcmp(argv[1], "microcode_override"))
			add("cpu_microcode_type", "wrong");
		assert(command_loadall(0, NULL) == CMD_OK);
		assert(record_count == 3 && counts[2] == 4);
		assert(!strcmp(records[2][1], "-t"));
		assert(!strcmp(records[2][2], "cpu_microcode"));
		assert(!strcmp(records[2][3], "/boot/firmware/amd-ucode.bin"));
	} else if (!strcmp(argv[1], "firmware_list")) {
		firmware_list_value = "  chip/one.bin\t\tchip/two.bin  ";
		assert(command_loadall(0, NULL) == CMD_OK);
		assert(record_count == 4);
		assert(!strcmp(records[2][2], "firmware"));
		assert(!strcmp(records[2][3], "chip/one.bin"));
		assert(!strcmp(records[3][3], "chip/two.bin"));
	} else if (!strcmp(argv[1], "repeat") ||
	    !strcmp(argv[1], "kernel_failure")) {
		kernel_options_value = kernel_options;
		if (!strcmp(argv[1], "kernel_failure"))
			kernel_result = CMD_ERROR;
		for (int attempt = 0; attempt < 2; ++attempt) {
			assert(command_loadall(0, NULL) == kernel_result);
			assert(!strcmp(kernel_options_value, "-v"));
			assert(kernel_name_value ==
			    (kernel_result == CMD_OK ? kernel_name : NULL));
			assert(counts[attempt * 2 + 1] == 3);
			assert(!strcmp(records[attempt * 2 + 1][1], "kernel"));
			assert(!strcmp(records[attempt * 2 + 1][2], "-v"));
		}
		assert(record_count == 4);
	} else if (!strcmp(argv[1], "firmware_separators")) {
		const char *lists[] = {"", " \t ", "one", "one two",
		    "one\ttwo", " \tone \t two\t "};
		const int images[] = {0, 0, 1, 2, 2, 2};
		for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
			firmware_list_value = lists[i];
			record_count = 0;
			assert(command_loadall(0, NULL) == CMD_OK);
			assert(record_count == 2 + images[i]);
			if (images[i] >= 1)
				assert(!strcmp(records[2][3], "one"));
			if (images[i] == 2)
				assert(!strcmp(records[3][3], "two"));
			assert(firmware_list_value == lists[i]);
		}
	} else if (!strcmp(argv[1], "modules")) {
		add("example_load", "YES");
		add("example_name", "example.ko");
		add("disabled_load", "NO");
		add("image_load", "YES");
		add("image_type", "md_image");
		assert(command_loadall(0, NULL) == CMD_OK);
		assert(record_count == 4);
		assert(counts[2] == 2 && !strcmp(records[2][1], "example.ko"));
		assert(counts[3] == 4 && !strcmp(records[3][2], "md_image"));
	} else
		abort();
	return 0;
}
