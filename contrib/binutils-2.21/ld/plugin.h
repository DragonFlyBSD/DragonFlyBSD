/* Plugin control for the GNU linker.
   Copyright 2010, 2011 Free Software Foundation, Inc.

   This file is part of the GNU Binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#ifndef GLD_PLUGIN_H
#define GLD_PLUGIN_H


/* This is the only forward declaration we need to avoid having
   to include the plugin-api.h header in order to use this file.  */
struct ld_plugin_input_file;

/* Handle -plugin arg: find and load plugin, or return error.  */
extern int plugin_opt_plugin (const char *plugin);

/* Accumulate option arguments for last-loaded plugin, or return
   error if none.  */
extern int plugin_opt_plugin_arg (const char *arg);

/* Return true if any plugins are active this run.  Only valid
   after options have been processed.  */
extern bfd_boolean plugin_active_plugins_p (void);

/* Load up and initialise all plugins after argument parsing.  */
extern int plugin_load_plugins (void);

/* Return name of plugin which caused an error in any of the above.  */
extern const char *plugin_error_plugin (void);

/* Call 'claim file' hook for all plugins.  */
extern int plugin_call_claim_file (const struct ld_plugin_input_file *file,
		int *claimed);

/* Call 'all symbols read' hook for all plugins.  */
extern int plugin_call_all_symbols_read (void);

/* Call 'cleanup' hook for all plugins at exit.  */
extern void plugin_call_cleanup (void);

/* Generate a dummy BFD to represent an IR file, for any callers of
   plugin_call_claim_file to use as the handle in the ld_plugin_input_file
   struct that they build to pass in.  The BFD is initially writable, so
   that symbols can be added to it; it must be made readable after the
   add_symbols hook has been called so that it can be read when linking.  */
extern bfd *plugin_get_ir_dummy_bfd (const char *name, bfd *template);

/* Notice-symbol bfd linker callback hook.  */
extern bfd_boolean plugin_notice (struct bfd_link_info *info,
		const char *name, bfd *abfd, asection *section,
		bfd_vma value);

/* Multiple-definition bfd linker callback hook.  */
extern bfd_boolean plugin_multiple_definition (struct bfd_link_info *info,
		const char *name,
		bfd *obfd, asection *osec, bfd_vma oval,
		bfd *nbfd, asection *nsec, bfd_vma nval);

#endif /* !def GLD_PLUGIN_H */
