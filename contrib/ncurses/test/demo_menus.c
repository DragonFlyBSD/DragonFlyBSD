/*
 * $Id: demo_menus.c,v 1.1 2003/04/26 22:10:43 tom Exp $
 *
 * Demonstrate a variety of functions from the menu library.
 * Thomas Dickey - 2003/4/26
 */
/*
item_count			-
item_description		-
item_init			-
item_opts			-
item_opts_off			-
item_opts_on			-
item_term			-
item_userptr			-
item_visible			-
menu_back			-
menu_fore			-
menu_format			-
menu_grey			-
menu_init			-
menu_mark			-
menu_opts			-
menu_opts_on			-
menu_pad			-
menu_pattern			-
menu_request_by_name		-
menu_request_name		-
menu_spacing			-
menu_sub			-
menu_term			-
menu_userptr			-
set_current_item		-
set_item_init			-
set_item_opts			-
set_item_term			-
set_item_userptr		-
set_menu_back			-
set_menu_fore			-
set_menu_grey			-
set_menu_init			-
set_menu_items			-
set_menu_mark			-
set_menu_opts			-
set_menu_pad			-
set_menu_pattern		-
set_menu_spacing		-
set_menu_term			-
set_menu_userptr		-
set_top_row			-
top_row				-
*/

#include <test.priv.h>

#if USE_LIBMENU

#include <menu.h>

int
main(int argc GCC_UNUSED, char *argv[]GCC_UNUSED)
{
    printf("Not implemented - demo for menu library\n");
    return EXIT_SUCCESS;
}
#else
int
main(void)
{
    printf("This program requires the curses menu library\n");
    ExitProgram(EXIT_FAILURE);
}
#endif
