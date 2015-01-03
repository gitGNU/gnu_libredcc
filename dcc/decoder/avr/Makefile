# Version: $Id: Makefile 1265 2013-05-04 21:45:24Z ag0015 $

ARCH = avr

PRG = ba_decoder
OBJ = main.o interrupts.o ../share/compose_packet.o eint0_isr.o ../share/decoder.o ../share/io.o io_hw.o eeprom_hw.o error_hw.o

INCLUDE_DIR = -I . -I .. -I ../../include -I ../../../include
LIB_DIR = -L ../../lib/$(ARCH) -L ../../../lib/$(ARCH)
LIBS1 = dcc uart
# could I get those automatically?
LIBS = -ldcc -luart
#LIBS = -l$(LIBS1)
SYMBOLS = -D F_CPU=16000000 -D DEBUG=5 

MCU_TARGET = atmega328p
#MCU_TARGET = attiny22
OPTIMIZE = -O3
#OPTIMIZE = -O0        -fsee        -fsection-anchors        -mno-long-calls        -mno-longcall


DEFS =

CC = avr-gcc
AS = avr-gcc

CFLAGS = -g -Wall $(OPTIMIZE) -mmcu=$(MCU_TARGET) $(DEFS) $(INCLUDE_DIR) $(SYMBOLS) # -fno-jump-tables -
LDFLAGS = -g -Wall $(OPTIMIZE) -mmcu=$(MCU_TARGET) -u uart_init $(LIBS) $(LIB_DIR) #$(LIBS) # -nostdlib $(DEFS) # -fnojum
AFLAGS = -g -Wall -mmcu=$(MCU_TARGET) $(DEFS) $(SYMBOLS) -c


OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump

all: hex ehex

# compiling is done by an implicit rule.

# assembling:

%.o: %.S
	$(AS) $(AFLAGS) -o $@ $< 

#linking:
#$(LIB_DIR)/libs: 
#	make -C $(LIB_DIR)

$(PRG).elf: $(OBJ) #$(LIB_DIR)/libs
#	make -C $(LIB_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

#.PHONY: $(LIB_DIR)/%.a



#$(LIB_DIR)/lib%.a: 
#	$(MAKE) -C $@

# dependency:
dcc_receiver.o: $(INCLUDE_DIR)/uart.h $(INCLUDE_DIR)/dcc.h


clean:
	rm -rf $(OBJ) $(PRG).elf 
	rm -rf *.lst *.map $(EXTRA_CLEAN_FILES)

lst: $(PRG).lst

%.lst: %.elf
	$(OBJDUMP) -h -S $< > $@

# Rules for building the .text rom images
hex: $(PRG).hex

%.hex: %.elf
#	$(OBJCOPY) -j .text -O ihex $< $@
	$(OBJCOPY) -j .text -j .data -O ihex $< $@


bin: $(PRG).bin

%.bin: %.elf
	$(OBJCOPY) -j .text -O bin $< $@
#	$(OBJCOPY) -j .text -j .data -O ihex $< $@


# Rules for building the .eeprom rom images
ehex: $(PRG)_eeprom.hex

%_eeprom.hex: %.elf
	$(OBJCOPY) -j .eeprom --change-section-lma .eeprom=0 -O ihex $< $@ || { echo empty $@ not generated; exit 0; }

# Rules for Uploading to the Arduino board:
upload: all
	avrdude -p m328p -c arduino -P /dev/ttyACM0 -Ueeprom:w:$(PRG)_eeprom.hex
	avrdude -p m328p -c arduino -P /dev/ttyACM0 -Uflash:w:$(PRG).hex

upload1: all
	avrdude -p m328p -c arduino -P /dev/ttyACM1 -Ueeprom:w:$(PRG)_eeprom.hex
	avrdude -p m328p -c arduino -P /dev/ttyACM1 -Uflash:w:$(PRG).hex