/*
	dtn2adminep.c:	Administrative endpoint application process
			for "dtn2" scheme.

	Author: Scott Burleigh, JPL

	Copyright (c) 2006, California Institute of Technology.
	ALL RIGHTS RESERVED.  U.S. Government Sponsorship
	acknowledged.
									*/
#include "dtn2fw.h"

static int	handleStatusRpt(BpDelivery *dlv, BpStatusRpt *rpt)
{
	char	memobuf[1024];

	isprintf(memobuf, sizeof memobuf, "[i] bundle (%s), %lu:%lu, %lu \
status is %d", rpt->sourceEid, rpt->creationTime.seconds,
		rpt->creationTime.count, rpt->fragmentOffset, rpt->reasonCode);
	writeMemo(memobuf);
	return 0;
}

static int	handleCtSignal(BpDelivery *dlv, BpCtSignal *cts)
{
#if 0
	char	memobuf[1024];

	if (cts->succeeded)
	{
		/*	Special handling for reason code =
		 *	CtRedundantTransmission?			*/

		isprintf(memobuf, sizeof memobuf, "custody transfer success \
at %s: %d", cts->sourceEid, cts->reasonCode);
	}
	else	/*	No release of custody.				*/
	{
		/*	Special handling for reason code =
		 *	CtDepleted Storage or CtNoKnownRoute or
		 *	CtNoTimelyContact?  Apply some negative
		 *	incentive to routing through the sourceEid
		 *	named in dlv?					*/

		isprintf(memobuf, sizeof memobuf, "[i] custody transfer \
failure at %s: %d", cts->sourceEid, cts->reasonCode);
	}

	writeMemo(memobuf);
#endif
	return 0;
}

#if defined (VXWORKS) || defined (RTEMS)
int	dtn2adminep(int a1, int a2, int a3, int a4, int a5,
		int a6, int a7, int a8, int a9, int a10)
{
#else
int	main(int argc, char *argv[])
{
#endif
	VScheme		*vscheme;
	PsmAddress	vschemeElt;

	if (bpAttach() < 0)
	{
		putErrmsg("dtn2adminep can't attach to BP.", NULL);
		return 1;
	}

	if (dtn2Init() < 0)
	{
		putErrmsg("dtn2adminep can't load routing database.", NULL);
		return 1;
	}

	findScheme("dtn", &vscheme, &vschemeElt);
	if (vschemeElt == 0)
	{
		putErrmsg("dtn2adminep can't get admin EID.", NULL);
		return 1;
	}

	writeMemo("[i] dtn2adminep is running.");
	if (_handleAdminBundles(vscheme->custodianEidString, handleStatusRpt,
			handleCtSignal) < 0)
	{
		putErrmsg("dtn2adminep crashed.", NULL);
	}

	writeErrmsgMemos();
	writeMemo("[i] dtn2adminep has ended.");
	ionDetach();
	return 0;
}
