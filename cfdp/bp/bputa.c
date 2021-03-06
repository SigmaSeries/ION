/*
	bputa.c:	UT-layer adapter using Bundle Protocol.
									*/
/*									*/
/*	Copyright (c) 2009, California Institute of Technology.		*/
/*	All rights reserved.						*/
/*	Author: Scott Burleigh, Jet Propulsion Laboratory		*/
/*									*/

#include <cfdpP.h>

#define	CFDP_SEND_SVC_NBR	(64)
#define	CFDP_RECV_SVC_NBR	(65)

typedef struct
{
	pthread_t	mainThread;
	int		running;
	BpSAP		rxSap;
} RxThreadParms;

/*	*	*	Receiver thread functions	*	*	*/

static void	*receivePdus(void *parm)
{
	RxThreadParms	*parms = (RxThreadParms *) parm;
	char		ownEid[64];
	Sdr		sdr;
	BpDelivery	dlv;
	int		contentLength;
	ZcoReader	reader;
	unsigned char	*buffer;

	buffer = MTAKE(CFDP_MAX_PDU_SIZE);
	if (buffer == NULL)
	{
		putErrmsg("bputa receiver thread can't get buffer.", NULL);
		parms->running = 0;
		return NULL;
	}

	isprintf(ownEid, sizeof ownEid, "ipn:%lu.%d", getOwnNodeNbr(),
			CFDP_RECV_SVC_NBR);
	if (bp_open(ownEid, &(parms->rxSap)) < 0)
	{
		MRELEASE(buffer);
		putErrmsg("CFDP can't open own 'recv' endpoint.", ownEid);
		parms->running = 0;
		return NULL;
	}

	sdr = bp_get_sdr();
	writeMemo("[i] bputa input has started.");
	while (parms->running)
	{
		if (bp_receive(parms->rxSap, &dlv, BP_BLOCKING) < 0)
		{
			putErrmsg("bputa bundle reception failed.", NULL);
			parms->running = 0;
			continue;
		}

		if (dlv.result == BpPayloadPresent)
		{
			contentLength = zco_source_data_length(sdr, dlv.adu);
			sdr_begin_xn(sdr);
			zco_start_receiving(sdr, dlv.adu, &reader);
			if (zco_receive_source(sdr, &reader, contentLength,
					(char *) buffer) < 0)
			{
				sdr_cancel_xn(sdr);
				putErrmsg("bputa can't receive bundle ADU.",
						itoa(contentLength));
				parms->running = 0;
				continue;
			}

			zco_stop_receiving(sdr, &reader);
			if (sdr_end_xn(sdr) < 0)
			{
				putErrmsg("bputa can't handle bundle delivery.",
						NULL);
				parms->running = 0;
				continue;
			}

			if (cfdpHandleInboundPdu(buffer, contentLength) < 0)
			{
				putErrmsg("bputa can't handle inbound PDU.",
						NULL);
				parms->running = 0;
			}
		}

		bp_release_delivery(&dlv, 1);

		/*	Make sure other tasks have a chance to run.	*/

		sm_TaskYield();
	}

	bp_close(parms->rxSap);
	MRELEASE(buffer);
	writeMemo("[i] bputa input has stopped.");
	return NULL;
}

/*	*	*	Main thread functions	*	*	*	*/

#if defined (VXWORKS) || defined (RTEMS)
int	bputa(int a1, int a2, int a3, int a4, int a5,
		int a6, int a7, int a8, int a9, int a10)
{
#else
int	main(int argc, char **argv)
{
#endif
	char		ownEid[64];
	BpSAP		txSap;
	RxThreadParms	parms;
	Sdr		sdr;
	pthread_t	rxThread;
	Object		pduZco;
	OutFdu		fduBuffer;
	BpUtParms	utParms;
	unsigned long	destinationNodeNbr;
	char		destEid[64];
	char		reportToEidBuf[64];
	char		*reportToEid;
	Object		newBundle;
	Object		pduElt;

	if (bp_attach() < 0)
	{
		putErrmsg("CFDP can't attach to BP.", NULL);
		return 0;
	}

	isprintf(ownEid, sizeof ownEid, "ipn:%lu.%d", getOwnNodeNbr(),
			CFDP_SEND_SVC_NBR);
	if (bp_open(ownEid, &txSap) < 0)
	{
		putErrmsg("CFDP can't open own 'send' endpoint.", ownEid);
		return 0;
	}

	if (txSap == NULL)
	{
		putErrmsg("bputa can't get Bundle Protocol SAP.", NULL);
		return 0;
	}

	if (cfdpAttach() < 0)
	{
		bp_close(txSap);
		putErrmsg("bputa can't attach to CFDP.", NULL);
		return 0;
	}

	sdr = bp_get_sdr();
	parms.mainThread = pthread_self();
	parms.running = 1;
	if (pthread_create(&rxThread, NULL, receivePdus, &parms))
	{
		bp_close(txSap);
		putSysErrmsg("bputa can't create receiver thread", NULL);
		return -1;
	}

	writeMemo("[i] bputa is running.");
	while (parms.running)
	{
		/*	Get an outbound CFDP PDU for transmission.	*/

		if (cfdpDequeueOutboundPdu(&pduZco, &fduBuffer) < 0)
		{
			writeMemo("[?] bputa can't dequeue outbound CFDP PDU; \
terminating.");
			parms.running = 0;
			continue;
		}

		/*	Determine quality of service for transmission.	*/

		if (fduBuffer.utParmsLength == sizeof(BpUtParms))
		{
			memcpy((char *) &utParms, (char *) &fduBuffer.utParms,
					sizeof(BpUtParms));
		}
		else
		{
			memset((char *) &utParms, 0, sizeof(BpUtParms));
			utParms.reportToNodeNbr = 0;
			utParms.lifespan = 86400;	/*	1 day.	*/
			utParms.classOfService = BP_STD_PRIORITY;
			utParms.custodySwitch = SourceCustodyRequired;
			utParms.srrFlags = 0;
			utParms.ackRequested = 0;
			utParms.extendedCOS.flowLabel = 0;
			utParms.extendedCOS.flags = 0;
			utParms.extendedCOS.ordinal = 0;
		}

		cfdp_decompress_number(&destinationNodeNbr,
				&fduBuffer.destinationEntityNbr);
		if (destinationNodeNbr == 0)
		{
			writeMemo("[?] bputa declining to send to node 0.");
			continue;
		}

		isprintf(destEid, sizeof destEid, "ipn:%lu.%d",
				destinationNodeNbr, CFDP_RECV_SVC_NBR);
		if (utParms.reportToNodeNbr == 0)
		{
			reportToEid = NULL;
		}
		else
		{
			isprintf(reportToEidBuf, sizeof reportToEidBuf,
					"ipn:%lu.%d", utParms.reportToNodeNbr,
					CFDP_RECV_SVC_NBR);
			reportToEid = reportToEidBuf;
		}

		/*	Send PDU in a bundle when flow control allows.	*/

		newBundle = 0;
		sdr_begin_xn(sdr);
		while (parms.running && newBundle == 0)
		{
			switch (bp_send(txSap, BP_NONBLOCKING, destEid,
				reportToEid, utParms.lifespan,
				utParms.classOfService, utParms.custodySwitch,
				utParms.srrFlags, utParms.ackRequested,
				&utParms.extendedCOS, pduZco, &newBundle))
			{
			case 0:		/*	No space for bundle.	*/
				if (errno == EWOULDBLOCK)
				{
					if (sdr_end_xn(sdr) < 0)
					{
						putErrmsg("bputa xn failed.",
								NULL);
						parms.running = 0;
					}

					microsnooze(250000);
					sdr_begin_xn(sdr);
					continue;
				}

				/*	Intentional fall-through.	*/

			case -1:
				putErrmsg("bputa can't send PDU in bundle; \
terminating.", NULL);
				parms.running = 0;
			}
		}

		if (newBundle == 0)
		{
			if (sdr_end_xn(sdr) < 0)
			{
				putErrmsg("bputa transaction failed.", NULL);
				parms.running = 0;
			}

			continue;	/*	Must have stopped.	*/
		}

		/*	Enable cancellation of this PDU.		*/

		pduElt = sdr_list_insert_last(sdr, fduBuffer.extantPdus,
				newBundle);
		if (pduElt)
		{
			bp_track(newBundle, pduElt);
		}

		if (sdr_end_xn(sdr) < 0)
		{
			putErrmsg("bputa can't track PDU; terminating.", NULL);
			parms.running = 0;
		}

		/*	Make sure other tasks have a chance to run.	*/

		sm_TaskYield();
	}

	if (rxThread)
	{
		bp_interrupt(parms.rxSap);
		pthread_join(rxThread, NULL);
	}

	bp_close(txSap);
	writeErrmsgMemos();
	writeMemo("[i] Stopping bputa.");
	ionDetach();
	return 0;
}
