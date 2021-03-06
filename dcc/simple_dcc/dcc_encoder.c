/* 
 * Copyright 2014 André Grüning <libredcc@email.de>
 *
 * This file is part of LibreDCC
 *
 * LibreDCC is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LibreDCC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LibreDCC.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file 
    $Id$
    
    encodes a dcc packet given as a list of bytes and sends it to WHICH OUTPUT pin?
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>

#include <dcc.h>

#include "dcc_encoder.h"

#define F_CPU_MHZ (F_CPU / 1000000) //! cpu ticks per us
#define HALF_PERIOD_1 (PERIOD_1 / 2) // us duration of 1 signal.
#define HALF_PERIOD_0 (2*HALF_PERIOD_1) // us duration of 0 signal
#define PRESCALER 8

/**
 * \ todo do some error checking here
 */
#define ONE_TICKS ((HALF_PERIOD_1 * F_CPU_MHZ) / PRESCALER)
#define ZERO_TICKS (2*ONE_TICKS)

// advance declarations:
static uint8_t next_bit();
//static const dcc_packet* get_next_packet();
static uint8_t is_new_packet_ready();
static void done_with_packet();

/**
   ISR that generates the DCC signal on pin XX. It queries next_bit to see whether it needs to encode a 0 or 1 bit next.
   It uses timer 2 mode XXX, so that the OCR2A pin toggles each time
   the interrupt is called. We then only set a new period duration
   every second time as the DCC signal consists of a logical 0 follow
   by a logical 1 of the same duration (or the other way round) 
 */
ISR(TIMER2_COMPA_vect) {

  static uint8_t toggle = 0;
  toggle++;
  
  // do only every second time:
  if((toggle & 0x1) == 0) {
    OCR2A = next_bit() ? ONE_TICKS : ZERO_TICKS;
  }
}

//! holds number of 1s to send in the preamble -- this is different
//! for operations and progeamming mode.
volatile uint8_t preamble_len = ENCODER_PREAMBLE_LEN;


/*! to hold the dcc packet to be sent.
  This should in principle be made volatile, but as a change does only
  matter between function calls (isr calls) where dcc_packet is used and we assume
  that functions read from / write back to memory at their start/end
  we can get away without making it volatile.
 */
static dcc_packet packet; 

/**
   generates DCC conform bit sequence from the bytes in packet, bib by
   bit on sunsequence calls.

   Initially calls generate the require number of DCC preamble
   bits. If there is no new packet to send avaiable it continous to
   generate preamble 1 bits until there is a new packet available.

   Further calls then generate the bit sequence of the DCC packet
   until all bit have been sent. The function notifies that the global
   variable packet can be released (and eg updated with the next
   packet to send) by calling done_with_packet().

   It then continunes to generate preamble bits (at least the minimal
   number required) until a new packet is available for bit stream encode.

   @return next bit of the DCC stream generated from packet.
 */
uint8_t next_bit() {

  //! states to go through for DCC signal generation.
  enum {PREAMBLE, START_BIT, BYTE, STOP_BIT};

  static uint8_t state = PREAMBLE;
  static uint8_t bytecount = 0;


  switch (state) {
  case PREAMBLE:
    {

      static uint8_t preamble_ones = 0;
      preamble_ones++;

      // we move to the start bit state if we have had enough 1s in the preamble
      if(preamble_ones >= preamble_len) { 
	preamble_ones = 0;
	state = START_BIT;
      }
      return 1; 
    }

  case START_BIT:
    // check whether there is a new packet to send
    if(is_new_packet_ready()) {
      state = BYTE;
      return 0;
    }
    else { // just create more preamble bits, if there is no packet to send. :-)
      return 1; 
    }

  case BYTE: // when we reach here first we know we have a valid packet to send, and we know we are at the beginning of a packet, so we have a bit to return!
    {
      static uint8_t bitmask = 0x80;

      const uint8_t bit = (packet.pp.byte[bytecount] & bitmask);

      bitmask >>= 1;
      if(bitmask == 0) {
	bitmask = 0x80;
	state = STOP_BIT;
      }
      return bit;
    }

  case STOP_BIT:
    bytecount++;
    if(bytecount >= packet.len) {
      done_with_packet(); // make it clear we are done with the current packet
      bytecount = 0;
      state = PREAMBLE; // oops we arecreating a extra preamble bit here!
      //      free(packet); // should this be done outside the ISR? Does it
		    // take too much time? We will see. 
      //      preamble_ones++;
      //      goto CASE_PREAMBLE; // is this valid?
      return 1;
	  // we are done with this packet
    }
    else { // send next byte
      state = BYTE;
      return 0;
    }
    break;
  }
  // cant happen? or shall we restructure the switch statement, so it falls through to here if not a 0 bit has to be output?
  return 1;
}

/*! flag to synchronise between ISR and main thread.
  The following functions and macros try to hide the mechanism of
  synchronisation between ISR and main thread. But essentially the
  ISR check whether flag is 1 to indicate that the main thread has
  provided a new dcc packet. The ISR sets the flag to 0 to indicate
  that it is done with the packet. The main thread only overwrites
  the packet with a new packet is flag is 0, and then sets flag 1 if
  it has deposited a new packet.
*/
static volatile uint8_t flag = 0;

/**
   checks flag whether new packet is available. Only to be called from
   within ISR.

   @return true if new packet is available.
 */
static inline uint8_t is_new_packet_ready() {
  return flag;
}

/** checks flag whether ISR is still busy with packet. To be called
    only from main thread 

    @return true if ISR is still busy with last packet
*/
inline uint8_t busy_with_last_packet() {
  return flag;
}

/**
   notify that ISR is done with current packet and hence global
   variable packet can be set to a new packet. Only to be called from ISR.
 */
inline static void done_with_packet() {
  flag = 0;
}

inline static void dcc_signal_off() {

  TCCR2B = 0; // timer off
  // switch the output OCR2A to zero for the sake of strange boosters:
  PORTB &= ~ _BV(PB3);

#ifdef SHORTCUT
  //! disable short cut interrupt.
  EIMSK &= ~ _BV(INT0); // check with ATmel manual how often
  //! \todo do I need to clear the flag here as well?
#endif

 }


/*! 
   wait until the current packet has been send (if any) and then
   switch off DCC signal generation -- to be run on the main thread
 */
void dcc_off() {
  //! wait for current packet to be send
  //! probably the packet will not be recognised by the decoders as the comcluding 1 might be missing
  //! but all internal variables will be in a defined state -- so that
  //! dcc_on() will start at the beginning of the preamble.  
  //! well we might miss a preamble 1 -- but that is unlikely... and
  //! recoverable
  //! we cannot have a new packet, aS
  while(busy_with_last_packet()); 
  //cli() not cli as the UART interrupts need to run!
  // stop timer or disable the interrupt? We stop the timer:
  
  dcc_signal_off();
}

/*! switches dcc signal generation on.
  Must not be called from an ISR.
  @post Global interrupts will be enabled */

void dcc_on() {

#ifdef SHORTCUT

  cli();

  /* don´t to anything if we are running altogether and 
     don´t switch on if we still have a shortcut. */
  if (is_dcc_on() || is_shortcut()) return; 
  
  /** force first packet, especially to reset state of next_bit if we
      were interrupted by a emergence_dcc_off().
  */
  packet = idle_packet;
  flag = 1;

  // enable shortcut interrupt.
  EIMSK |= _BV(INT0); // check with ATmel manual how often 
  //! \todo check whether and how I need to resit the INT0-flag?

#endif

  TCCR2B = _BV(CS21); // prescaler 8 and start timer (and part of CTC
		      // mode -- what happens if we get a short-cut
		      // interrupt just now -- we really have to check
		      // the level of the signal, not the edge!
                      // to avoid this we switch of the global
		      // interrupt -- so that an shortcut has either
		      // been processed before the switch on -- or
		      // will be processed after it.
  

#ifdef SHORTCUT

  sei();

  // and now normally commit another idle packet, but following in the rules:
  commit_packet(&idle_packet);

#endif

}


#ifdef SHORTCUT

/*! switches off dcc signal generation immediately. Sets the dcc
    output to 0 and abort the current package being send. Does some
    cleanup of states to ensure a smooth restart of dcc generation
    when the dcc generariob resumes via a call dcc_on().
    To be called from an ISR
 */ 
void emergency_dcc_off() {

  //! set dcc output pin to 0 and stop generarting timer interrupts
  //! \todo does this really set the pin to zero in all cases? Or can
  //! a pin toggle occur while this ISR is executed?
  dcc_signal_off();
  
  //! release current packets, so that main thread does not block
  done_with_packet(); // but it might still be that a new packet has
		      // been loaded by the time we start dcc again.

  /** bring next_bit into shape -- this is not possible without making
      the local variables of next_bit() public. It is also not
      strictly required, the worst that can happen is that we send a
      corrupt packet on start up (what is the worst it could be
      interpreted?)
  */

  //! but we should send a number of idle packets or rest packets on
  //! power on to sync.

}
#endif

/** To be called from the routine that provides the new packets 
we must make sure this is translated into something atomic 
blocks hard on flag! Must not be called from an ISR **/

void commit_packet(const dcc_packet* const new_packet) {
  while(flag); // wait until last packet has been processed
               // better sleep here instead?
  packet = *new_packet; // this operation is not atmic, it copies the packet
  flag = 1; // but should not do any harm, because this one is atomic
}

void init_encoder() __attribute__((naked)) __attribute__((section(".init8")));
void init_encoder() {

  // we use OC2A pin = PB3 as output for the dcc signal.

  TCCR2A = _BV(COM2A0) | _BV(WGM21); // toggle OC2A on compare match and CTC mode.

  PORTB &= ~ _BV(PB3);
  DDRB |=  _BV(PB3); // make OC2A/PB3 output, we do not know which
		     // value it has initially, but we do not care for
		     // DCC, but for the sake of strange boosters, we
		     // set it to zero.

  OCR2A = ONE_TICKS; //ONE_TICKS; // should be 116 with the current settings
						   // for a one as the
						   // default with
						   // 58us duration
						   // (and it will be
						   // 232 for a zero
						   // -- so fits into
						   // the register!

  TIMSK2 = _BV(OCIE2A); // enable interrupt for compare match A

  // now done in the dcc_on!
  //  TCCR2B = _BV(CS21); // prescaler 8 and start timer (and part of CTC mode)

  // for a clean start
  TCCR2B = 0;
}

#ifdef SHORTCUT

/*! we use the external interrupt 0 because it has a high priority
  Alternatively we could check eg in every call to the timer2
  interrupt whether a shortcut has happend

  @sideeffect uses PD2 (which doubles as INT0 pin)
*/
ISR(INT0_vect) {
  emergency_dcc_off();
}

/** this assumes the shortcut signal is given via an open-collector
    pull to zero */
void init_shortcut() __attribute__((naked)) __attribute__((section(".init8")));
void init_shortcut() {
  
  // make PD2 input:
  DDRD &= ~ _BV(PD2);
  // switch one pull-up
  PORTD |= _BV(PD2);

  // set up intrrupt to be triggered by a flat zero.
  // EICRA &= ~ (_BV(ISC00) || _BV(ISC01));

  // alternatively, only be triggered by a falling edge
  EICRA &= ~ _BV(ISC00);
  EICRA |= _BV(ISC01);
  
  // enable interrupt INT0 -- no -- only enable it when we call dcc_on
  // EIMSK |= _BV(INT0); // check with ATmel manual how often the flag is set and when it is cleared

}

#endif
