/*
 *       Copyright (c) 1997 by Simon Shapiro
 *       All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/usr.sbin/dpt/dpt_sysinfo/dpt_sysinfo.c,v 1.3 1999/08/28 01:16:10 peter Exp $
 * $DragonFly: src/usr.sbin/dpt/dpt_sysinfo/dpt_sysinfo.c,v 1.3 2004/12/18 22:48:03 swildner Exp $
 */

/* dpt_ctlinfo.c:  Dunp a DPT HBA Information Block */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/ioctl.h>
#include <scsi/scsi_all.h>
#include <scsi/scsi_message.h>
#include <scsi/scsiconf.h>

#define DPT_MEASURE_PERFORMANCE

#include <sys/dpt.h>


int
main(int argc, char **argv, char **argp)
{
    eata_pt_t		pass_thru;
	dpt_sysinfo_t sysinfo;
    
    int result;
    int fd;
    int ndx;
    
    if ( (fd = open(argv[1], O_RDWR, S_IRUSR | S_IWUSR)) == -1 ) {
		fprintf(stderr, "%s ERROR:  Failed to open \"%s\" - %s\n",
			argv[0], argv[1], strerror(errno));
		exit(1);
    }

    pass_thru.eataID[0] = 'E';
    pass_thru.eataID[1] = 'A';
    pass_thru.eataID[2] = 'T';
    pass_thru.eataID[3] = 'A';
    pass_thru.command   = DPT_SYSINFO;
    pass_thru.command_buffer = (u_int8_t *)&sysinfo;

    if ( (result = ioctl(fd, DPT_IOCTL_SEND, &pass_thru)) != 0 ) {
		fprintf(stderr, "%s ERROR:  Failed to send IOCTL %lx - %s\n",
			argv[0], DPT_IOCTL_SEND,
			strerror(errno));
		exit(1);
    }

	fprintf(stdout, "%x:%x:%d:",
		sysinfo.drive0CMOS, sysinfo.drive1CMOS, sysinfo.numDrives);

	switch (sysinfo.processorFamily) {
	case PROC_INTEL:
		fprintf(stdout, "Intel:");
		switch (sysinfo.processorType) {
		case PROC_8086:
			fprintf(stdout, "8086:");
			break;
		case PROC_286:
			fprintf(stdout, "80286:");
			break;
		case PROC_386:
			fprintf(stdout, "i386:");
			break;
		case PROC_486:
			fprintf(stdout, "80486:");
			break;
		case PROC_PENTIUM:
			fprintf(stdout, "Pentium:");
			break;
		case PROC_P6:
			fprintf(stdout, "Pentium-Pro:");
			break;
		default:
			fprintf(stdout, "Unknown (%d):", sysinfo.processorType);
		}
		break;
	case PROC_MOTOROLA:
		fprintf(stdout, "Motorola:");
		switch (sysinfo.processorType) {
		case PROC_68000:
			fprintf(stdout, "M68000");
			break;
		case PROC_68020:
			fprintf(stdout, "M68020");
			break;
		case PROC_68030:
			fprintf(stdout, "M68030");
			break;
		case PROC_68040:
			fprintf(stdout, "M68040");
			break;
		default:
			fprintf(stdout, "Unknown (%d):", sysinfo.processorType);
		}
		break;
	case PROC_MIPS4000:
		fprintf(stdout, "MIPS:Any:");
		break;
	case PROC_ALPHA:
		fprintf(stdout, "Alpha:Any:");
		break;
	default:
		fprintf(stdout, "Unknown (%d):Any:", sysinfo.processorFamily);
	}

	fprintf(stdout, "%d.%d.%d:",
		sysinfo.smartROMMajorVersion,
		sysinfo.smartROMMinorVersion,
		sysinfo.smartROMRevision);

	fprintf(stdout, "%c%c%c%c%c%c%c%c%c%c%c:",
		(sysinfo.flags & SI_CMOS_Valid)		? '+' : '-',
		(sysinfo.flags & SI_NumDrivesValid)	? '+' : '-',
		(sysinfo.flags & SI_ProcessorValid)	? '+' : '-',
		(sysinfo.flags & SI_MemorySizeValid)	? '+' : '-',
		(sysinfo.flags & SI_DriveParamsValid)	? '+' : '-',
		(sysinfo.flags & SI_SmartROMverValid)	? '+' : '-',
		(sysinfo.flags & SI_OSversionValid)	? '+' : '-',
		(sysinfo.flags & SI_OSspecificValid)	? '+' : '-',
		(sysinfo.flags & SI_BusTypeValid)		? '+' : '-',
		(sysinfo.flags & SI_ALL_VALID)  		? '+' : '-',
		(sysinfo.flags & SI_NO_SmartROM)  	? '+' : '-');

	fprintf(stdout, "%d:", sysinfo.conventionalMemSize);
	fprintf(stdout, "%d:", sysinfo.extendedMemSize);

	switch (sysinfo.osType) {
	case OS_DOS:
		fprintf(stdout, "DOS:");
		break;
	case OS_WINDOWS:
		fprintf(stdout, "Win3.1:");
		break;
	case OS_WINDOWS_NT:
		fprintf(stdout, "NT:");
		break;
	case OS_OS2M:
		fprintf(stdout, "OS/2-std:");
		break;
	case OS_OS2L:
		fprintf(stdout, "OS/2-LADDR:");
		break;
	case OS_OS22x:
		fprintf(stdout, "OS/2-2.x:");
		break;
	case OS_NW286:
		fprintf(stdout, "NetWare-286:");
		break;
	case OS_NW386:
		fprintf(stdout, "NetWare-386:");
		break;
	case OS_GEN_UNIX:
		fprintf(stdout, "Unix:");
		break;
	case OS_SCO_UNIX:
		fprintf(stdout, "SCO Unix:");
		break;
	case OS_ATT_UNIX:
		fprintf(stdout, "AT&T Unix:");
		break;
	case OS_UNIXWARE:
		fprintf(stdout, "UnixWare:");
		break;
	case OS_INT_UNIX:
		fprintf(stdout, "IAC Unix:");
		break;
	case OS_SOLARIS:
		fprintf(stdout, "Solaris:");
		break;
	case OS_QNX:
		fprintf(stdout, "Qnx:");
		break;
	case OS_NEXTSTEP:
		fprintf(stdout, "NextStep:");
		break;
	case OS_BANYAN:
		fprintf(stdout, "Banyan:");
		break;
	case OS_OLIVETTI_UNIX:
		fprintf(stdout, "Olivetti Unix:");
		break;
	case OS_FREEBSD:
		fprintf(stdout, "FreeBSD:");
		break;
	case OS_OTHER:
		fprintf(stdout, "Other (%d):", sysinfo.osType);
		break;
	default:
		fprintf(stdout, "Unknown (%d):", sysinfo.osType);
	}

	fprintf(stdout, "%d.%d.%d.%d:", sysinfo.osMajorVersion,
		sysinfo.osMinorVersion, sysinfo.osRevision,
		sysinfo.osSubRevision);

	switch (sysinfo.busType) {
	case HBA_BUS_ISA:
		fprintf(stdout, "ISA:");
		break;
	case HBA_BUS_EISA:
		fprintf(stdout, "EISA:");
		break;
	case HBA_BUS_PCI:
		fprintf(stdout, "PCI:");
		break;
	default:
		fprintf(stdout, "Unknown (%d):", sysinfo.busType);
	}

	for (ndx = 0; ndx < 16; ndx++) {
		if (sysinfo.drives[ndx].cylinders == 0)
			continue;
		fprintf(stdout, "d%dc%dh%ds%d:", ndx,
			sysinfo.drives[ndx].cylinders,
			sysinfo.drives[ndx].heads,
			sysinfo.drives[ndx].sectors);
	}

	fprintf(stdout, "\n");

    return(0);
}
