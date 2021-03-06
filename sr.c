#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat protocol.  Adapted from
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.1  J.F.Kurose

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications (6/6/2008 - CLP): 
   - removed bidirectional GBN code and other code not used by prac. 
   - fixed C style to adhere to current programming style
   (24/3/2013 - CLP)
   - added GBN implementation
**********************************************************************/

#define RTT 15.0      /* round trip time.  MUST BE SET TO 15.0 when submitting assignment */
#define WINDOWSIZE 6  /* Maximum number of buffered unacked packet */
#define SEQSPACE 12   /* min sequence space for SR must be at least windowsize*2 */
#define NOTINUSE (-1) /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver  
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your 
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
    int checksum = 0;
    int i;

    checksum = packet.seqnum;
    checksum += packet.acknum;
    for (i = 0; i < 20; i++)
        checksum += (int)(packet.payload[i]);

    return checksum;
}

bool IsCorrupted(struct pkt packet)
{
    if (packet.checksum == ComputeChecksum(packet))
        return (false);
    else
        return (true);
}

/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE]; /* array for storing packets waiting for ACK */
static int windowfirst, windowlast;   /* array indexes of the first/last packet awaiting ACK */
static int windowcount;               /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;              /* the next sequence number to be used by the sender */
static int send_base, rcv_base;       /* The base number in the sender window and receiver window */
struct pkt *send_buffer;              /* Pointer to the sender buffer what contains unsent packet */
struct pkt *rcv_buffer;               /* Pointer to the receiver buffer what contains packets that arrives out of orders */
static int ackcount;                  /* the number of acked packets */
/*status of each packet */
enum ack_or_not
{
    no,
    yes
};

struct pkt_acked
{
    int seqnum;
    enum ack_or_not status;
};

/*temporary container for sender buffer */
struct pkt_acked *send_window;

/*Test methods for output desired packet info */
void output_snd_buffer()
{
    int i;
    printf("\n");
    /* use for debugging */
    printf("Test function for outputing sender buffer \n");
    for (i = 0; i < windowcount; i++)
    {
        printf("|%i:%i|", buffer[i].seqnum, buffer[i].acknum);
    }

    printf("The current awaiting packet for ACK is %i: \t", windowcount);
    printf("\n");
}

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
    struct pkt sendpkt;
    int i;

    /* if not blocked waiting on ACK */
    if ((windowcount + ackcount) < WINDOWSIZE)
    {
        if (TRACE > 1)
            printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

        /* create packet */
        sendpkt.seqnum = A_nextseqnum;
        sendpkt.acknum = NOTINUSE;
        for (i = 0; i < 20; i++)
            sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);
        /* finish packet creation */

        /* put packet in back of window buffer */
        /* windowlast will always be 0 for alternating bit; but not for SR */
        windowlast = (windowlast + 1) % WINDOWSIZE;
        buffer[windowlast] = sendpkt;
        windowcount++;

        /*output_snd_buffer();*/

        if (TRACE > 0)
            printf("Sending packet %d to layer 3\n", sendpkt.seqnum);

        tolayer3(A, sendpkt);

        /* start timer for the first packet being sent */
        if (windowcount == 1)
            starttimer(A, RTT);

        /* get next sequence number, wrap back to 0 */
        A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
    }
    /* if blocked,  window is full */
    else
    {
        if (TRACE > 0)
            printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}

/* called from layer 3, when a packet arrives for layer 4 
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
    int i;
    int seqfirst = buffer[windowfirst].seqnum;
    int seqlast = buffer[windowlast].seqnum;

    /* if received ACK is not corrupted */
    if (!IsCorrupted(packet))
    {
        if (TRACE > 0)
            printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
        total_ACKs_received++;

        /* check if new ACK or duplicate */
        if (windowcount != 0)
        {
            /* check case when seqnum has and hasn't wrapped */
            if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
                ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast)))
            {
                /* set the acknum to 1 for packeted in sender buffer*/
                buffer[packet.acknum % WINDOWSIZE].acknum = 1;
                ackcount++;
                windowcount--;
                new_ACKs++;
                /* packet is a new ACK */
                if (TRACE > 0)
                    printf("----A: ACK %d is not a duplicate\n", packet.acknum);

                if (buffer[windowfirst].seqnum == packet.acknum)
                {
                    /*slide window*/
                    for (i = 0; i < WINDOWSIZE; i++)
                    {
                        if (buffer[windowfirst].acknum == 1)
                        {
                            windowfirst = (windowfirst + 1) % WINDOWSIZE;
                            ackcount--;
                        }
                    }
                    /* start timer again if still exist packets in window */
                    stoptimer(A);
                    if (windowcount >= 1)
                        starttimer(A, RTT);
                }
            }
            else
            {
                if (TRACE > 0)
                    printf("----A: duplicate ACK received, do nothing!\n");
            }
        }
    }
    else
    {
        if (TRACE > 0)
            printf("----A: corrupted ACK is received, do nothing!\n");
    }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
    int i;

    if (TRACE > 0)
        printf("----A: time out,resend packets!\n");

    /* check if there's any packet in the sender buffer*/
    if (windowcount > 0)
    {
        for (i = 0; i < WINDOWSIZE; ++i)
        {
            /* double check if there's any acked packet in the sender buffer */
            if (buffer[(i + windowfirst) % WINDOWSIZE].acknum != 1)
            {
                if (TRACE > 0)
                    printf("---A: resending packet %d\n", (buffer[(windowfirst + i) % WINDOWSIZE]).seqnum);

                tolayer3(A, buffer[(windowfirst + i) % WINDOWSIZE]);
                /* update total re-sent number of packet */
                packets_resent++;
                /* re-start timing for corrupted or lost packet */
                starttimer(A, RTT);
                break;
            }
        }
    }
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
    int i;
    /* initialise A's window, buffer and sequence number */
    A_nextseqnum = 0; /* A starts with seq num 0, do not change this */
    send_base = 0;
    rcv_base = 0; /*Index number of sender & receiver window */
    windowfirst = 0;
    windowlast = -1; /* windowlast is where the last packet sent is stored.  
		     new packets are placed in winlast + 1 
		     so initially this is set to -1
		   */
    windowcount = 0;

    /*dynamically assign the memory space for send_window */
    send_window = (struct pkt_acked *)malloc(sizeof(struct pkt_acked) * WINDOWSIZE);
    for (i = 0; i < WINDOWSIZE; i++)
    {
        send_window[i].seqnum = NOTINUSE;
        send_window[i].status = no;
    }
}

/********* Receiver (B)  variables and procedures ************/

static int B_windowfirst;               /* array indexes of the first packet awaiting ACK */
static int expectedseqnum;              /* the sequence number expected next by the receiver */
static int B_nextseqnum;                /* the sequence number for the next packets sent by B */
static struct pkt B_buffer[WINDOWSIZE]; /* array for storing packets waiting for ACK */

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
    /* the packet doesn't contain any info in payload but ACK message */
    struct pkt sendpkt;
    int i;
    /* index of receiver buffer, use to check any out of boundary case */
    int B_index;
    /*  if not corrupted, but different to GBN
        the received packet does not have to be
        in order */
    if ((!IsCorrupted(packet)))
    {
        if (TRACE > 0)
            printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);

        /* Initialize packet which does not contain any info in the payload but ACK */
        sendpkt.acknum = packet.seqnum;
        sendpkt.seqnum = B_nextseqnum;
        B_nextseqnum = (B_nextseqnum + 1) % SEQSPACE;
        /* instantiate with '0' to represent nothing in the payload */
        for (i = 0; i < 20; i++)
        {
            sendpkt.payload[i] = '0';
        }
        sendpkt.checksum = ComputeChecksum(sendpkt);

        /* update total number of received packet */
        packets_received++;

        /* deliver to network layer */
        tolayer3(B, sendpkt);

        /* Doing the same check as in routine A_input()
        check if the packet is out of receiver buffer boundary(index) */
        B_index = (expectedseqnum - 1 + WINDOWSIZE) % SEQSPACE;
        if (((expectedseqnum <= B_index) && (packet.seqnum >= expectedseqnum && packet.seqnum <= B_index)) ||
            ((expectedseqnum > B_index) && (packet.seqnum >= expectedseqnum || packet.seqnum <= B_index)))
        {
            /* buffer the packet into receiver buffer for following process */
            B_buffer[packet.seqnum % WINDOWSIZE] = packet;
            /* send packet to layer 3 of B which is in ordr, check with seqnum instead of acknum */
            if (packet.seqnum == expectedseqnum)
            {
                for (i = 0; i < WINDOWSIZE; i++)
                {
                    if (B_buffer[B_windowfirst].seqnum == expectedseqnum)
                    {
                        /* Send packet which is in order */
                        tolayer5(B, B_buffer[B_windowfirst].payload);
                        /* move receiver window by 1*/
                        B_windowfirst = (B_windowfirst + 1) % WINDOWSIZE;
                        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
                    }
                }
            }
        }
    }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
    expectedseqnum = 0;
    B_nextseqnum = 1;
    /* first index of receiver buffer */
    B_windowfirst = 0;
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}
