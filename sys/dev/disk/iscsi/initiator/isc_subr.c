/*-
 * Copyright (c) 2005-2008 Daniel Braniss <danny@cs.huji.ac.il>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/iscsi/initiator/isc_subr.c,v 1.3 2009/02/14 11:34:57 rrs Exp $
 */
/*
 | iSCSI
 | $Id: isc_subr.c,v 1.20 2006/12/01 09:10:17 danny Exp danny $
 */

#include "opt_iscsi_initiator.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/proc.h>
#include <sys/ioccom.h>
#include <sys/queue.h>
#include <sys/kthread.h>
#include <sys/syslog.h>
#include <sys/mbuf.h>
#include <sys/libkern.h>
#include <sys/eventhandler.h>
#include <sys/mutex2.h>

#include <bus/cam/cam.h>

#include "dev/disk/iscsi/initiator/iscsi.h"
#include "dev/disk/iscsi/initiator/iscsivar.h"

static char *
i_strdupin(char *s, size_t maxlen)
{
     size_t	len;
     char	*p, *q;

     p = kmalloc(maxlen, M_ISCSI, M_WAITOK);
     if(copyinstr(s, p, maxlen, &len)) {
	  kfree(p, M_ISCSI);
	  return NULL;
     }
     q = kmalloc(len, M_ISCSI, M_WAITOK);
     bcopy(p, q, len);
     kfree(p, M_ISCSI);

     return q;
}

/*
 | XXX: not finished coding
 */
int
i_setopt(isc_session_t *sp, isc_opt_t *opt)
{
     const int	digsize = 6;
     size_t	len;
     char	hdigest[digsize], ddigest[digsize];

     debug_called(8);
     if(opt->maxRecvDataSegmentLength > 0) {
	  sp->opt.maxRecvDataSegmentLength = opt->maxRecvDataSegmentLength;
	  sdebug(2, "maxRecvDataSegmentLength=%d", sp->opt.maxRecvDataSegmentLength);
     }
     if(opt->maxXmitDataSegmentLength > 0) {
	  // danny's RFC
	  sp->opt.maxXmitDataSegmentLength = opt->maxXmitDataSegmentLength;
	  sdebug(2, "maXmitDataSegmentLength=%d", sp->opt.maxXmitDataSegmentLength);
     }
     if(opt->maxBurstLength != 0) {
	  sp->opt.maxBurstLength = opt->maxBurstLength;
	  sdebug(2, "maxBurstLength=%d", sp->opt.maxBurstLength);
     }

     if(opt->targetAddress != NULL) {
	  if(sp->opt.targetAddress != NULL)
	       kfree(sp->opt.targetAddress, M_ISCSI);
	  sp->opt.targetAddress = i_strdupin(opt->targetAddress, 128);
	  sdebug(4, "opt.targetAddress='%s'", sp->opt.targetAddress);
     }
     if(opt->targetName != NULL) {
	  if(sp->opt.targetName != NULL)
	       kfree(sp->opt.targetName, M_ISCSI);
	  sp->opt.targetName = i_strdupin(opt->targetName, 128);
	  sdebug(4, "opt.targetName='%s'", sp->opt.targetName);
     }
     if(opt->initiatorName != NULL) {
	  if(sp->opt.initiatorName != NULL)
	       kfree(sp->opt.initiatorName, M_ISCSI);
	  sp->opt.initiatorName = i_strdupin(opt->initiatorName, 128);
	  sdebug(4, "opt.initiatorName='%s'", sp->opt.initiatorName);
     }

     if(opt->maxluns > 0) {
	  if(opt->maxluns > ISCSI_MAX_LUNS)
	       sp->opt.maxluns = ISCSI_MAX_LUNS; // silently chop it down ...
	  sp->opt.maxluns = opt->maxluns;
	  sdebug(4, "opt.maxluns=%d", sp->opt.maxluns);
     }

     /* Try to copy the userland pointer containing the header digest */
     if(opt->headerDigest != NULL) {
	  if ((copyinstr(opt->headerDigest, &hdigest, digsize, &len)) != EFAULT) {
	       sdebug(2, "opt.headerDigest='%s'", hdigest);
	       if(strcmp(hdigest, "CRC32C") == 0) {
		    sp->hdrDigest = (digest_t *)crc32_ext;
		    sdebug(2, "headerDigest set");
	       }
	  }
     }
     /* Try to copy the userland pointer containing the data digest */
     if(opt->dataDigest != NULL) {
	  if ((copyinstr(opt->dataDigest, &ddigest, digsize, &len)) != EFAULT) {
	       if (strcmp(ddigest, "CRC32C") == 0) {
		    sp->dataDigest = (digest_t *)crc32_ext;
		    sdebug(2, "dataDigest set");
	       }
	  }
     }

     return 0;
}

void
i_freeopt(isc_opt_t *opt)
{
     if(opt->targetAddress != NULL) {
	  kfree(opt->targetAddress, M_ISCSI);
	  opt->targetAddress = NULL;
     }
     if(opt->targetName != NULL) {
	  kfree(opt->targetName, M_ISCSI);
	  opt->targetName = NULL;
     }
     if(opt->initiatorName != NULL) {
	  kfree(opt->initiatorName, M_ISCSI);
	  opt->initiatorName = NULL;
     }
}
