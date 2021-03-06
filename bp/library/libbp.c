/*
 *	libbp.c:	functions enabling the implementation of
 *			BP applications.
 *
 *	Copyright (c) 2003, California Institute of Technology.
 *	ALL RIGHTS RESERVED.  U.S. Government Sponsorship
 *	acknowledged.
 *
 *	Author: Scott Burleigh, JPL
 *
 *	Modification History:
 *	Date	  Who	What
 *	06-27-03  SCB	Original development.
 *	01-09-06  SCB	Revision per version 4 of Bundle Protocol spec.
 */

#include "bpP.h"

typedef struct
{
	int		interval;	/*	Seconds.		*/
	sm_SemId	semaphore;
} TimerParms;

int	bp_attach()
{
	return bpAttach();
}

int	bp_agent_is_started()
{
	BpVdb	*vdb = getBpVdb();

	return (vdb && vdb->clockPid > 0) ? 1 : 0;
}

Sdr	bp_get_sdr()
{
	return getIonsdr();
}

void	bp_detach()
{
	ionDetach();
}

int	bp_open(char *eidString, BpSAP *bpsapPtr)
{
	Sdr		sdr;
	MetaEid		metaEid;
	VScheme		*vscheme;
	PsmAddress	vschemeElt;
	Sap		sap;
	VEndpoint	*vpoint;
	PsmAddress	vpointElt;

	CHKERR(eidString && *eidString && bpsapPtr);
	*bpsapPtr = NULL;	/*	Default, in case of failure.	*/
	sdr = getIonsdr();
	sdr_begin_xn(sdr);	/*	Just to lock memory.		*/

	/*	First validate the endpoint ID.				*/

	if (parseEidString(eidString, &metaEid, &vscheme, &vschemeElt) == 0)
	{
		sdr_exit_xn(sdr);
		putErrmsg("Malformed EID.", eidString);
		return -1;
	}

	if (vschemeElt == 0)
	{
		sdr_exit_xn(sdr);
		putErrmsg("Scheme not known.", metaEid.schemeName);
		restoreEidString(&metaEid);
		return -1;
	}

	findEndpoint(NULL, metaEid.nss, vscheme, &vpoint, &vpointElt);
	if (vpointElt == 0)
	{
		sdr_exit_xn(sdr);
		putErrmsg("Endpoint not known.", metaEid.nss);
		restoreEidString(&metaEid);
		return -1;
	}

	/*	Endpoint exists; make sure it's not already opened
	 *	by some application.					*/

	if (vpoint->appPid > 0)	/*	Endpoint not closed.		*/
	{
		if (sm_TaskExists(vpoint->appPid))
		{
			sdr_exit_xn(sdr);
			if (vpoint->appPid == sm_TaskIdSelf())
			{
				return 0;
			}

			restoreEidString(&metaEid);
			putErrmsg("Endpoint is already open.",
					itoa(vpoint->appPid));
			return -1;
		}

		/*	Application terminated without closing the
		 *	endpoint, so simply close it now.		*/

		vpoint->appPid = -1;
	}

	/*	Construct the service access point.			*/

	sap.vpoint = vpoint;
	memcpy(&sap.endpointMetaEid, &metaEid, sizeof(MetaEid));
	sap.endpointMetaEid.colon = NULL;
	sap.endpointMetaEid.schemeName = MTAKE(metaEid.schemeNameLength + 1);
	if (sap.endpointMetaEid.schemeName == NULL)
	{
		sdr_exit_xn(sdr);
		putErrmsg("Can't create BpSAP.", NULL);
		restoreEidString(&metaEid);
		return -1;
	}

	sap.endpointMetaEid.nss = MTAKE(metaEid.nssLength + 1);
	if (sap.endpointMetaEid.nss == NULL)
	{
		sdr_exit_xn(sdr);
		MRELEASE(sap.endpointMetaEid.schemeName);
		putErrmsg("Can't create BpSAP.", NULL);
		restoreEidString(&metaEid);
		return -1;
	}

	*bpsapPtr = MTAKE(sizeof(Sap));
	if (*bpsapPtr == NULL)
	{
		sdr_exit_xn(sdr);
		MRELEASE(sap.endpointMetaEid.nss);
		MRELEASE(sap.endpointMetaEid.schemeName);
		putErrmsg("Can't create BpSAP.", NULL);
		restoreEidString(&metaEid);
		return -1;
	}

	istrcpy(sap.endpointMetaEid.schemeName, metaEid.schemeName,
			sizeof sap.endpointMetaEid.schemeName);
	istrcpy(sap.endpointMetaEid.nss, metaEid.nss,
			sizeof sap.endpointMetaEid.nss);
	restoreEidString(&metaEid);
	sap.recvSemaphore = vpoint->semaphore;
	memcpy((char *) *bpsapPtr, (char *) &sap, sizeof(Sap));

	/*	Having created the SAP, give its owner exclusive
	 *	access to the endpoint.					*/

	vpoint->appPid = sm_TaskIdSelf();
	sdr_exit_xn(sdr);	/*	Unlock memory.			*/
	return 0;
}

void	bp_close(BpSAP sap)
{
	VEndpoint	*vpoint;

	if (sap == NULL)
	{
		return;
	}

	vpoint = sap->vpoint;
	if (vpoint->appPid == sm_TaskIdSelf())
	{
		vpoint->appPid = -1;
	}

	MRELEASE(sap->endpointMetaEid.nss);
	MRELEASE(sap->endpointMetaEid.schemeName);
	MRELEASE(sap);
}

int	bp_send(BpSAP sap, int mode, char *destEid, char *reportToEid,
		int lifespan, int classOfService, BpCustodySwitch custodySwitch,
		unsigned char srrFlags, int ackRequested, BpExtendedCOS *ecos,
		Object adu, Object *bundleObj)
{
	Sdr		sdr = getIonsdr();
	BpVdb		*vdb = getBpVdb();
	BpExtendedCOS	defaultECOS = { 0, 0, 0 };
	int		aduOccupancy;
	MetaEid		*sourceMetaEid;
	Throttle	*throttle;

	CHKERR(bundleObj);
	*bundleObj = 0;
	CHKERR(adu);
	if (ecos == NULL)
	{
		ecos = &defaultECOS;
	}
	else
	{
		if (ecos->ordinal == 255)	/*	Reserved.	*/
		{
			ecos->ordinal = 254;
		}
	}

	if (sap)
	{
		sourceMetaEid = &(sap->endpointMetaEid);
	}
	else
	{
		sourceMetaEid = NULL;
	}

	/*	Admission control (bundle production throttling)
	 *	happens here.						*/

	throttle = &(vdb->productionThrottle);
	sdr_begin_xn(sdr);	/*	Just to lock memory.		*/
	aduOccupancy = zco_occupancy(sdr, adu);
	while (aduOccupancy > throttle->capacity)
	{
		sdr_exit_xn(sdr);
		if (mode == BP_NONBLOCKING)
		{
			errno = EWOULDBLOCK;
			return 0;
		}

		if (sm_SemTake(throttle->semaphore) < 0)
		{
			putErrmsg("Can't take throttle semaphore.", NULL);
			return -1;
		}

		if (sm_SemEnded(throttle->semaphore))
		{
			putErrmsg("Bundle agent has been stopped.", NULL);
			return -1;
		}

		sdr_begin_xn(sdr);
	}

	sdr_exit_xn(sdr);	/*	Release memory.			*/

	/*	Now go ahead and send the bundle.			*/

	return bpSend(sourceMetaEid, destEid, reportToEid, lifespan,
			classOfService, custodySwitch, srrFlags, ackRequested,
			ecos, adu, bundleObj, 0);
}

int	bp_track(Object bundleObj, Object trackingElt)
{
	Sdr	sdr = getIonsdr();
		OBJ_POINTER(Bundle, bundle);

	CHKERR(bundleObj && trackingElt);
	sdr_begin_xn(sdr);
	GET_OBJ_POINTER(sdr, Bundle, bundle, bundleObj);
	if (bundle->trackingElts == 0)
	{
		sdr_exit_xn(sdr);
		putErrmsg("Corrupt bundle?  Has no trackingElts list.", NULL);
		return -1;
	}

	sdr_list_insert_last(sdr, bundle->trackingElts, trackingElt);
	if (sdr_end_xn(sdr) < 0)
	{
		putErrmsg("Failed adding bundle tracking elt.", NULL);
		return -1;
	}

	return 0;
}

void	bp_untrack(Object bundleObj, Object trackingElt)
{
	Sdr	sdr = getIonsdr();
		OBJ_POINTER(Bundle, bundle);
	Object	elt;

	CHKVOID(bundleObj && trackingElt);
	sdr_begin_xn(sdr);
	GET_OBJ_POINTER(sdr, Bundle, bundle, bundleObj);
	if (bundle->trackingElts == 0)
	{
		sdr_exit_xn(sdr);
		return;
	}

	for (elt = sdr_list_first(sdr, bundle->trackingElts); elt;
			elt = sdr_list_next(sdr, elt))
	{
		if (sdr_list_data(sdr, elt) == trackingElt)
		{
			break;
		}
	}

	if (elt == 0)		/*	Not found.			*/
	{
		sdr_exit_xn(sdr);
		return;
	}

	sdr_list_delete(sdr, elt, NULL, NULL);
	if (sdr_end_xn(sdr) < 0)
	{
		putErrmsg("Failed removing bundle tracking elt.", NULL);
	}
}

int	bp_cancel(Object bundleObj)
{
	Sdr	sdr = getIonsdr();

	sdr_begin_xn(sdr);
	if (bpDestroyBundle(bundleObj, 1) < 0)
	{
		sdr_cancel_xn(sdr);
		putErrmsg("Can't cancel bundle.", NULL);
		return -1;
	}

	if (sdr_end_xn(sdr) < 0)
	{
		putErrmsg("Failure in bundle cancellation.", NULL);
		return -1;
	}

	return 0;
}

static void	*timerMain(void *parm)
{
	TimerParms	*timer = (TimerParms *) parm;
	pthread_mutex_t	mutex;
	pthread_cond_t	cv;
	struct timeval	workTime;
	struct timespec	deadline;
	int		result;

	memset((char *) &mutex, 0, sizeof mutex);
	if (pthread_mutex_init(&mutex, NULL))
	{
		putSysErrmsg("can't start timer, mutex init failed", NULL);
		sm_SemGive(timer->semaphore);
		return NULL;
	}

	memset((char *) &cv, 0, sizeof cv);
	if (pthread_cond_init(&cv, NULL))
	{
		putSysErrmsg("can't start timer, cond init failed", NULL);
		sm_SemGive(timer->semaphore);
		return NULL;
	}

	getCurrentTime(&workTime);
	deadline.tv_sec = workTime.tv_sec + timer->interval;
	deadline.tv_nsec = workTime.tv_usec * 1000;
	pthread_mutex_lock(&mutex);
	result = pthread_cond_timedwait(&cv, &mutex, &deadline);
	pthread_mutex_unlock(&mutex);
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cv);
	if (result)
	{
		errno = result;
		if (errno != ETIMEDOUT)
		{
			putSysErrmsg("timer failure", NULL);
			sm_SemGive(timer->semaphore);
			return NULL;
		}
	}

	/*	Timed out; must wake up the main thread.		*/

	timer->interval = 0;	/*	Indicate genuine timeout.	*/
	sm_SemGive(timer->semaphore);
	return NULL;
}

int	bp_receive(BpSAP sap, BpDelivery *dlvBuffer, int timeoutSeconds)
{
	Sdr		sdr = getIonsdr();
	VEndpoint	*vpoint;
			OBJ_POINTER(Endpoint, endpoint);
	Object		dlvElt;
	Object		bundleAddr;
	Bundle		bundle;
	TimerParms	timerParms;
	pthread_t	timerThread;
	int		result;
	char		*dictionary;

	CHKERR(sap && dlvBuffer);
	if (timeoutSeconds < BP_BLOCKING)
	{
		putErrmsg("Illegal timeout interval.", itoa(timeoutSeconds));
		return -1;
	}

	vpoint = sap->vpoint;
	sdr_begin_xn(sdr);
	if (vpoint->appPid != sm_TaskIdSelf())
	{
		sdr_exit_xn(sdr);
		putErrmsg("Can't receive: not owner of endpoint.",
				itoa(vpoint->appPid));
		return -1;
	}

	if (sm_SemEnded(vpoint->semaphore))
	{
		sdr_exit_xn(sdr);
		writeMemo("[?] Endpoint has been stopped.");

		/*	End task, but without error.			*/

		return -1;
	}

	/*	Get oldest bundle in delivery queue, if any; wait
	 *	for one if necessary.					*/

	GET_OBJ_POINTER(sdr, Endpoint, endpoint, sdr_list_data(sdr,
			vpoint->endpointElt));
	dlvElt = sdr_list_first(sdr, endpoint->deliveryQueue);
	if (dlvElt == 0)
	{
		sdr_exit_xn(sdr);
		if (timeoutSeconds == BP_POLL)
		{
			dlvBuffer->result = BpReceptionTimedOut;
			return 0;
		}

		/*	Wait for semaphore to be given, either by the
		 *	deliverBundle() function or by timer thread.	*/

		if (timeoutSeconds == BP_BLOCKING)
		{
			timerParms.interval = -1;
		}
		else	/*	This is a receive() with a deadline.	*/
		{
			timerParms.interval = timeoutSeconds;
			timerParms.semaphore = vpoint->semaphore;
			if (pthread_create(&timerThread, NULL, timerMain,
					&timerParms) < 0)
			{
				putSysErrmsg("Can't enable interval timer",
						NULL);
				return -1;
			}
		}

		/*	Take endpoint semaphore.			*/

		if (sm_SemTake(vpoint->semaphore) < 0)
		{
			putErrmsg("Can't take endpoint semaphore.", NULL);
			return -1;
		}

		if (sm_SemEnded(vpoint->semaphore))
		{
			writeMemo("[i] Endpoint has been stopped.");

			/*	End task, but without error.		*/

			return -1;
		}

		/*	Have taken the semaphore, one way or another.	*/

		sdr_begin_xn(sdr);
		dlvElt = sdr_list_first(sdr, endpoint->deliveryQueue);
		if (dlvElt == 0)	/*	Still nothing.		*/
		{
			/*	Either sm_SemTake() was interrupted
			 *	or else timer thread gave semaphore.	*/

			sdr_exit_xn(sdr);
			if (timerParms.interval == 0)
			{
				/*	Timer expired.			*/

				dlvBuffer->result = BpReceptionTimedOut;
				pthread_join(timerThread, NULL);
			}
			else	/*	Interrupted.			*/
			{
				dlvBuffer->result = BpReceptionInterrupted;
				if (timerParms.interval != -1)
				{
					pthread_cancel(timerThread);
					pthread_join(timerThread, NULL);
				}
			}

			return 0;
		}
		else		/*	Bundle was delivered.		*/
		{
			if (timerParms.interval != -1)
			{
				pthread_cancel(timerThread);
				pthread_join(timerThread, NULL);
			}
		}
	}

	/*	At this point, we have got a dlvElt and are in an SDR
	 *	transaction.						*/

	bundleAddr = sdr_list_data(sdr, dlvElt);
	sdr_stage(sdr, (char *) &bundle, bundleAddr, sizeof(Bundle));
	dictionary = retrieveDictionary(&bundle);
	if (dictionary == (char *) &bundle)
	{
		sdr_cancel_xn(sdr);
		putErrmsg("Can't retrieve dictionary.", NULL);
		return -1;
	}

	/*	Now fill in the data indication structure.		*/

	dlvBuffer->result = BpPayloadPresent;
	if (printEid(&bundle.id.source, dictionary,
			&dlvBuffer->bundleSourceEid) < 0)
	{
		sdr_cancel_xn(sdr);
		putErrmsg("Can't print source EID.", NULL);
		return -1;
	}

	dlvBuffer->bundleCreationTime.seconds = bundle.id.creationTime.seconds;
	dlvBuffer->bundleCreationTime.count = bundle.id.creationTime.count;
	dlvBuffer->adminRecord = bundle.bundleProcFlags & BDL_IS_ADMIN;
	dlvBuffer->adu = zco_add_reference(sdr, bundle.payload.content);
	dlvBuffer->ackRequested = bundle.bundleProcFlags & BDL_APP_ACK_REQUEST;

	/*	Now before returning we send delivery status report
	 *	if it is requested.					*/

	if (SRR_FLAGS(bundle.bundleProcFlags) & BP_DELIVERED_RPT)
	{
		bundle.statusRpt.flags |= BP_DELIVERED_RPT;
		getCurrentDtnTime(&bundle.statusRpt.deliveryTime);
	}

	if (bundle.statusRpt.flags)
	{
		result = sendStatusRpt(&bundle, dictionary);
		if (result < 0)
		{
			sdr_cancel_xn(sdr);
			putErrmsg("Can't send status report.", NULL);
			return -1;
		}
	}

	/*	Finally delete the delivery list element and, if
	 *	possible, destroy the bundle itself.			*/

	if (dictionary)
	{
		MRELEASE(dictionary);
	}

	sdr_list_delete(sdr, dlvElt, (SdrListDeleteFn) NULL, NULL);
	bundle.dlvQueueElt = 0;
	sdr_write(sdr, bundleAddr, (char *) &bundle, sizeof(Bundle));
	if (bpDestroyBundle(bundleAddr, 0) < 0)
	{
		sdr_cancel_xn(sdr);
		putErrmsg("Can't destroy bundle.", NULL);
		return -1;
	}

	if (sdr_end_xn(sdr) < 0)
	{
		putErrmsg("Failure in bundle reception.", NULL);
		return -1;
	}

	return 0;
}

void	bp_interrupt(BpSAP sap)
{
	/*	Give semaphore, simulating reception notice.		*/

	if (sap != NULL && sap->recvSemaphore != SM_SEM_NONE)
	{
		sm_SemGive(sap->recvSemaphore);
	}
}

void	bp_release_delivery(BpDelivery *dlvBuffer, int releasePayload)
{
	Sdr	sdr = getIonsdr();

	CHKVOID(dlvBuffer);
	if (dlvBuffer->result == BpPayloadPresent)
	{
		if (dlvBuffer->bundleSourceEid)
		{
			MRELEASE(dlvBuffer->bundleSourceEid);
			dlvBuffer->bundleSourceEid = NULL;
		}

		if (releasePayload)
		{
			if (dlvBuffer->adu)
			{
				sdr_begin_xn(sdr);
				zco_destroy_reference(sdr, dlvBuffer->adu);
				if (sdr_end_xn(sdr) < 0)
				{
					putErrmsg("Failed releasing delivery.",
							NULL);
				}

				dlvBuffer->adu = 0;
			}
		}
	}
}
