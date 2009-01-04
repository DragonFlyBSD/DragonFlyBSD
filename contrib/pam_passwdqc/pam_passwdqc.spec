# $Owl: Owl/packages/pam_passwdqc/pam_passwdqc/pam_passwdqc.spec,v 1.38 2008/02/12 20:28:48 solar Exp $

Summary: Pluggable password quality-control module.
Name: pam_passwdqc
Version: 1.0.5
Release: owl1
License: BSD-compatible
Group: System Environment/Base
URL: http://www.openwall.com/passwdqc/
Source: ftp://ftp.openwall.com/pub/projects/pam/modules/%name/%name-%version.tar.gz
BuildRequires: pam-devel
BuildRoot: /override/%name-%version

%description
pam_passwdqc is a simple password strength checking module for
PAM-aware password changing programs, such as passwd(1).  In addition
to checking regular passwords, it offers support for passphrases and
can provide randomly generated ones.  All features are optional and
can be (re-)configured without rebuilding.

%prep
%setup -q

%build
%__make CFLAGS="-Wall -fPIC -DLINUX_PAM %optflags"

%install
rm -rf %buildroot
%__make install DESTDIR=%buildroot MANDIR=%_mandir SECUREDIR=/%_lib/security

%files
%defattr(-,root,root)
%doc LICENSE README
/%_lib/security/pam_passwdqc.so
%_mandir/man*/*

%changelog
* Tue Feb 12 2008 Solar Designer <solar-at-owl.openwall.com> 1.0.5-owl1
- Replaced the separator characters with some of those defined by RFC 3986
as being safe within "userinfo" part of URLs without encoding.
- Reduced the default value for the N2 parameter to min=... (the minimum
length for passphrases) from 12 to 11.
- Corrected the potentially misleading description of N2 (Debian bug #310595).
- Applied minor grammar and style corrections to the documentation, a
pam_passwdqc message, and source code comments.

* Tue Apr 04 2006 Dmitry V. Levin <ldv-at-owl.openwall.com> 1.0.4-owl1
- Changed Makefile to pass list of libraries to linker after regular
object files, to fix build with -Wl,--as-needed.
- Corrected specfile to make it build on x86_64.

* Wed Aug 17 2005 Dmitry V. Levin <ldv-at-owl.openwall.com> 1.0.3-owl1
- Fixed potential memory leak in conversation wrapper.
- Restricted list of global symbols exported by the PAM module
to standard set of six pam_sm_* functions.

* Wed May 18 2005 Solar Designer <solar-at-owl.openwall.com> 1.0.2-owl1
- Fixed compiler warnings seen on FreeBSD 5.3.
- Updated the Makefile to not require editing on FreeBSD.
- Updated the FreeBSD-specific notes in PLATFORMS.

* Sun Mar 27 2005 Solar Designer <solar-at-owl.openwall.com> 1.0.1-owl1
- Further compiler warning fixes on LP64 platforms.

* Fri Mar 25 2005 Solar Designer <solar-at-owl.openwall.com> 1.0-owl1
- Corrected the source code to not break C strict aliasing rules.

* Wed Jan 26 2005 Solar Designer <solar-at-owl.openwall.com> 0.7.6-owl1
- Disallow unreasonable random= settings.
- Clarified the allowable bit sizes for randomly-generated passphrases and
the lack of relationship between passphrase= and random= options.

* Fri Oct 31 2003 Solar Designer <solar-at-owl.openwall.com> 0.7.5-owl1
- Assume invocation by root only if both the UID is 0 and the PAM service
name is "passwd"; this should solve changing expired passwords on Solaris
and HP-UX and make "enforce=users" safe.
- Produce proper English explanations for a wider variety of settings.
- Moved the "-c" out of CFLAGS, renamed FAKEROOT to DESTDIR.

* Sat Jun 21 2003 Solar Designer <solar-at-owl.openwall.com> 0.7.4-owl1
- Documented that "enforce=users" may not always work for services other
than the passwd command.
- Applied a patch to PLATFORMS from Mike Gerdts of GE Medical Systems
to reflect how Solaris 8 patch 108993-18 (or 108994-18 on x86) changes
Solaris 8's PAM implementation to look like Solaris 9.

* Mon Jun 02 2003 Solar Designer <solar-at-owl.openwall.com> 0.7.3.1-owl1
- Added URL.

* Thu Oct 31 2002 Solar Designer <solar-at-owl.openwall.com> 0.7.3-owl1
- When compiling with gcc, also link with gcc.
- Use $(MAKE) to invoke sub-makes.

* Fri Oct 04 2002 Solar Designer <solar-at-owl.openwall.com>
- Solaris 9 notes in PLATFORMS.

* Wed Sep 18 2002 Solar Designer <solar-at-owl.openwall.com>
- Build with Sun's C compiler cleanly, from Kevin Steves.
- Use install -c as that actually makes a difference on at least HP-UX
(otherwise install would possibly move files and not change the owner).

* Fri Sep 13 2002 Solar Designer <solar-at-owl.openwall.com>
- Have the same pam_passwdqc binary work for both trusted and non-trusted
HP-UX, from Kevin Steves.

* Fri Sep 06 2002 Solar Designer <solar-at-owl.openwall.com>
- Use bigcrypt() on HP-UX whenever necessary, from Kevin Steves of Atomic
Gears LLC.
- Moved the old password checking into a separate function.

* Wed Jul 31 2002 Solar Designer <solar-at-owl.openwall.com>
- Call it 0.6.

* Sat Jul 27 2002 Solar Designer <solar-at-owl.openwall.com>
- Documented that the man page is under the 3-clause BSD-style license.
- HP-UX 11 support.

* Tue Jul 23 2002 Solar Designer <solar-at-owl.openwall.com>
- Applied minor corrections to the man page and at the same time eliminated
unneeded/unimportant differences between it and the README.

* Sun Jul 21 2002 Solar Designer <solar-at-owl.openwall.com>
- 0.5.1: imported the pam_passwdqc(8) manual page back from FreeBSD.

* Tue Apr 16 2002 Solar Designer <solar-at-owl.openwall.com>
- 0.5: preliminary OpenPAM (FreeBSD-current) support in the code and related
code cleanups (thanks to Dag-Erling Smorgrav).

* Thu Feb 07 2002 Michail Litvak <mci-at-owl.openwall.com>
- Enforce our new spec file conventions.

* Sun Nov 04 2001 Solar Designer <solar-at-owl.openwall.com>
- Updated to 0.4:
- Added "ask_oldauthtok" and "check_oldauthtok" as needed for stacking with
the Solaris pam_unix;
- Permit for stacking of more than one instance of this module (no statics).

* Tue Feb 13 2001 Solar Designer <solar-at-owl.openwall.com>
- Install the module as mode 755.

* Tue Dec 19 2000 Solar Designer <solar-at-owl.openwall.com>
- Added "-Wall -fPIC" to the CFLAGS.

* Mon Oct 30 2000 Solar Designer <solar-at-owl.openwall.com>
- 0.3: portability fixes (this might build on non-Linux-PAM now).

* Fri Sep 22 2000 Solar Designer <solar-at-owl.openwall.com>
- 0.2: added "use_authtok", added README.

* Fri Aug 18 2000 Solar Designer <solar-at-owl.openwall.com>
- 0.1, "retry_wanted" bugfix.

* Sun Jul 02 2000 Solar Designer <solar-at-owl.openwall.com>
- Initial version (non-public).
