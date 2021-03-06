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
    Emultates SPROG as far so that it can collaborate with rocrail

    For ROCRAIL we need cmds:

    running:
    - "-": Power Off
    - "+": Power ßOn
    - "O": Send DCC packet given as a sequence of hex bytes.
    
    programming:
    - "C" -- direct mode programming -- write only
    - "V" -- obsolete (but required by NMRA) -- implementation not implemented.

    extensions:

    - "M" -- direct mode programming (ie CVs are programmed direct) --
      to be implemented

*/

//#define TEST 1
#undef TEST

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <avr/interrupt.h>

#include <uart.h>
#include <dcc.h>

#include "r_io.h"
#include "dcc_encoder.h"
#include "simple_dcc.h"

#include "service_mode.h"

char str[INPUT_LINE_LEN + 1]; // store maximally 64 chars plus the terminating null char.

static inline void make_dcc_packet(uint8_t argc, const char* const argv[]) {

  dcc_packet packet = {len: argc};
  
  if(argc < MIN_PACKET_LEN) {
    FPUTL("DCC packet must have 3 or more bytes.", &uart);
    return;
  }

  //! \todo this is alright as long as we do not generate the xor checksum byte ourselves
  if(argc > MAX_PACKET_LEN) argc = MAX_PACKET_LEN; 

  // all arguments for the "O" command are hex:
  uint8_t i;
  for(i = 0; i < argc; i++) {
    packet.pp.byte[i] = strtoul(argv[i], NULL, 16); // from this we say
						 // MAX_ARG must be
						 // MAX_PACKET_LEN
						 // (and -1 if we
						 // calculatre the
						 // packet xor by hand 
  }

  // and here we would need to add the xor

  
  //  FPUTL("Before commit", &uart);
  commit_packet(&packet);
  //FPUTL("After commit", &uart);


#ifdef TEST
  for(i = 0; i < packet.len; i++) {
    fprintf(&uart, "%02x " , packet.pp.byte[i]);
  }
  fputc(EOLCHAR, &uart);
#endif

}




//! same as above, but one local var less!
static inline void make_dcc_packet2(uint8_t argc, char* argv[]) {

  dcc_packet packet = {len: argc};

  // all arguments for the "O" command are hex:
  do {
    argc--;
    packet.pp.byte[argc] = strtoul(argv[argc], NULL, 16); // from this we say
						 // MAX_ARG must be
						 // MAX_PACKET_LEN
						 // (and -1 if we
						 // calculatre the
						 // packet xor by hand 
  } while (argc);

  // and here we would need to add the xor, but rocrail uses sprog in
  // a form where xor is generated in the host, not in sprog.

  commit_packet(&packet);

}

//! same as above, but one local var less, still shorter.
//! @pre argc must be greater than 0.
static inline void make_dcc_packet3(uint8_t argc, char* argv[]) {

  // \todo check that argc is at least 2, otherwise we might crash?

  dcc_packet packet = {len: argc};

  uint8_t* byte_ptr = &packet.pp.byte[0]; 
  char** arg_ptr = &argv[0];



  // all arguments for the "O" command are hex:
  do {
    *byte_ptr = strtoul(*arg_ptr, NULL, 16); // from this we say
						 // MAX_ARG must be
						 // MAX_PACKET_LEN
						 // (and -1 if we
						 // calculatre the
						 // packet xor by hand 
    byte_ptr++;
    arg_ptr++;
  } while (--argc);

  // and here we would need to add the xor, but rocrail uses sprog in
  // a form where xor is generated in the host, not in sprog.

  commit_packet(&packet);

}

/**
 * converts a string into a number following the sprog convention.
 * @pre string is nonempty (as eg coming out of strtok)
 * @param str non-empty string and nonnull.
 * If no explicit conversion symbol is given, we fall through to the
 * default glibc behaviour which deviates from sprog in case the
 * string begins with 0 (sprog does not know of any octal interpretation
 */
static inline uint16_t tokentonum(const char* str) {

  // default base for string conversion.
  uint8_t base = 0; 
  
  if(str[0] == 'h' || str[0] == 'H') {
    base = 16; 
    str++;
  } else if (str[0] == 'b' || str[0] == 'B') {
    base = 2; 
    str++;
  }

  return strtoul(str, NULL, base);

}

static inline void program_cv(const uint8_t argc, const char* const argv[]) {

  if(argc != 2) FPUTL("Give exactly two arguments: the CVs and the value to write. Reading not implemented", &uart);
  
  uint16_t cv = tokentonum(argv[0]);
  if(cv < CV_MIN || cv > CV_MAX) FPUTL("CV value must be between 1--1023", &uart); 
  // @todo is it really like above? I thought the real CV values go
  // from 0--1023 and you need to deduct 1 from addresses given in
  // manuals to get the address to write into the CV packet?

  uint8_t data = tokentonum(argv[1]);

  cv--; // convert range 1-1024 to 0-1023

  // there is already a class that constructs such a packet in DCCPacket.h
  dcc_packet packet = {
    .len =  SM_DIRECT_PACKET_LEN,
    .pp.sm.cv_h = CV_HIGH(cv), // \todo really -1?
    .pp.sm.cmd = SM_DIRECT_WR,
    .pp.sm.prefix = SM_PREFIX,
    .pp.sm.cv_l = CV_LOW(cv),
    .pp.sm.data_byte = data,
    .pp.sm.checksum = 0
  };
  
  packet.pp.sm.checksum = xor_checksum(&packet);

  service_mode_on();
  send_sm_dm_sequence(&packet);
  service_mode_off();
};

int main() {

  // enable interrupts -- ie start uart input
  sei();
  
  // say hello to the world -- not clear whether sprog does this as well.
  FPUTL("Start -- Version $Rev$", &uart);

  while(1) {

    r_fgets(str, INPUT_LINE_LEN + 1, &uart); // this did block, right?
					     // -- yes.
    


#ifdef TEST
    fputs("Raw: ", &uart);
    fputs(str, &uart); fputc(EOLCHAR, &uart); // just an echo for testing!
#endif

    
    // first token is cmd:
    char* const cmd = strtok(str, WHITE_SPACE); 

    if(cmd == NULL) continue; // read next line if this one was just an empty line.

#ifdef TEST
    fputs("CMD: ", &uart);
    fputs(cmd, &uart);
    fputc(EOLCHAR, &uart);
#endif

    // other tokens are arguments:
    const char* argv[MAX_ARG];
    uint8_t argc = 0;

    // \todo make shorter by including all conditions in to the condition of the while loop.
    while(argc < MAX_ARG) { // what should max arg be like? -- Sprog says 6.
      argv[argc] = strtok(NULL, WHITE_SPACE); 
      if(argv[argc] == NULL) break; // end of string 
      argc++;
    }

#ifdef TEST
    fputs("ARGS:", &uart);
    uint8_t i;
    for(i = 0; i < argc; i++) {
      fputc(' ', &uart);
      fputs(argv[i], &uart);
    }
    fputc(EOLCHAR, &uart);
#endif 

    

    // interpret command and arguments -- we are tolerant if surplus arguments are supplied.
    if(strlen(cmd) == 1) {
      // valid command 
      switch(toupper(cmd[0])) {
      case 'O':
	// we ignore all packets that are sent while power is off --
	// it would be better to queue them to prevent packet loss. 
	if(is_dcc_on()) make_dcc_packet(argc, argv); // here is a race
						     // condition for
						     // a power-cut
						     // off: we test
						     // whehter dcc is
						     // on, but it
						     // might be
						     // short-cutted
						     // by the time
						     // make_dcc_packet
						     // is execute
	FPUTL(OK,&uart); // is this answer expected according to the sprog manual?
	break;
      case '\e': // ESC is emergency switch off -- should this also be followed by an \r?
      case '-':
	dcc_off(); // do we still accept commands and queue them?
	// no responses for this command?
	//! \todo for this and other cases: only return OK if the
	//command was successfully executed.
	FPUTL(OK,&uart); // is this answer expected according to the sprog manual?
	break;
      case '+': 
	dcc_on(); 
	FPUTL(OK,&uart); // is this answer expected according to the sprog manual?
	break;
      case 'C': 
	if(is_dcc_on()) program_cv(argc, argv);
	break;
      default:
	FPUTL("Unknown Command.", &uart); // Command token too long or
					    // empty. -- this seems to
					    // unsettle the arduino  -- what did I mean with "unsettle"? I think I dealt with the case of empty token above
      }
    }
    else {
      // Command token too long.
      FPUTL("Command token too long or empty.", &uart); 
    }
  }
}
