/*
 * $DragonFly: src/gnu/usr.bin/sort/long-options.h,v 1.2 2003/11/09 11:41:16 eirikn Exp $
 */

void
  parse_long_options(int _argc, char **_argv, const char *_command_name,
			   const char *_version_string, void (*_usage) (int));
