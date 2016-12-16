//
//  tally8k.ino
//  tally8k
//
//  Led control for the "Tally 8000" box. For arduino mega.
//
//  Created by Vesa-Pekka Palmu on 2016-12-07.
//  Copyright 2016 Vesa-Pekka Palmu. All rights reserved.
//

/*
 * Wiring
 *
 * A0  - Front status DATA
 * A1  - Front status CLK
 * A2  -
 * A3  -
 * A4  - Relay 1
 * A5  - Relay 2
 * A6  - Relay 3
 * A7  - Relay 4
 *
 * A8  - Relay 5
 * A9  - Relay 6
 * A10 - Relay 7
 * A11 - Relay 8
 * A12 -
 * A13 -
 * A14 -
 * A15 -
 * 0 - RPI serial RX
 * 1 - RPI serial TX
 * 2 - Tally green led in
 * 3 - Tally red led in
 * 4-7 RPI gpio
 *
 * 38 - Remote status data
 * 39 - Remote status clk
 * 40 - Tally 5 data
 * 41 - Tally 5 clk
 * 42 - Tally 6 data
 * 43 - Tally 6 clk
 * 44 - Tally 7 data
 * 45 - Tally 7 clk
 * 46 - Tally 8 data
 * 47 - Tally 8 clk
 * 48 - Tally 9 data
 * 49 - Tally 9 clk
 * 50 - Tally 10 data
 * 51 - Tally 10 clk
 * 52 - Tally 11+12 data
 * 53 - Tally 11+12 clk
 */

/*
 * Tally mapping
 *
 * 1-4    Relays 1-4
 * 5      Relay 5 + Apa102 chain
 * 6-10   Apa102 chains of 8 leds each
 * 11+12  Combined apa chain of 16 leds
 * 13-16  Relays 5-8
 *
 */

// FastLED library for the apa102 leds
#include "FastLED.h"

// Tally state constants
#define TallyUnknown  0 // Not mapped in E2
#define TallySafe     1 // Tally is safe
#define TallyPVM      2 // Tally is in preview
#define TallyPGM      3 // Tally is in program
#define TallyPGMPVM   4 // Tally is in program and preview

// Color order for the apa102 leds
#define ColorOrder BGR

// Tally led colors
#define ColorUnknown  0x050505
#define ColorSafe     0x0000ff
#define ColorPVM      0x00ff00
#define ColorPGM      0xff0000
#define ColorPGMPVM   0xffff00

// Status led colors
#define StatusUnknown 0x020202
#define StatusSafe    0x00000a
#define StatusPVM     0x000a00
#define StatusPGM     0x0a0000
#define StatusPGMPVM  0x0a0a00
#define StatusError   0xffff00

// Number of total tallies
#define Tallies 16

// Front status strip length
#define FrontStatusLeds   12

// Tally apa102 strips
#define TallyStrips       8
#define TallyLedsPerStrip 8

// Status lines from the RPI
volatile bool status_green  = true;
volatile bool status_red    = false;

// Flags
volatile bool refresh_status = false;
volatile bool process_serial = false;

// Relay pins
int relays [8] = {A4, A5, A6, A7, A8, A9, A10, A11};

// Tally states
uint8_t tally_states[Tallies];

// Tally led chains
CRGB tally_leds[TallyStrips][TallyLedsPerStrip];

// Front status leds
CRGB front_status[FrontStatusLeds];

// Status leds in "0" connector
CRGB remote_status[Tallies];

/* Serial message struct
 * The message is as follow:
 * Start by sending ascii 'Q'
 * then the tally number 1-9, A-G
 * and then the state 0-4
 * end the message with 'W'
 *
 * Example: QA3W - tally 10 status PGM
 */ 
struct s_tallymessage {
  uint8_t start_magic;  // 'Q'
  uint8_t tally_number;
  uint8_t tally_state;
  uint8_t end_magic;    // 'W 
};

volatile s_tallymessage msg;
uint8_t  msg_field;

void setup() {
  // Initialize the serial hardware
  Serial.begin(115200);
  msg_field = 0;  // Index of incoming serial message field number
  
  // Initialize tally states
  for (int i = 0; i < Tallies; i++) {
    tally_states[i] = TallyUnknown;
  }
  
  // Relays
  for (int i = 0; i <8; i++) {
    pinMode(relays[i], OUTPUT);
    // They are active LOW devices
    digitalWrite(relays[i], HIGH);
  }
  
  // Status lines
  pinMode(2, INPUT);
  pinMode(3, INPUT);
  
  // Apa led chains
  // Front status
  FastLED.addLeds<APA102, A0, A1, ColorOrder>(front_status, 12);
  // Rear remote status
  FastLED.addLeds<APA102, 38, 39, ColorOrder>(remote_status, 16);
  // Tally led chains
  FastLED.addLeds<APA102, 40, 41, ColorOrder>(tally_leds[0], TallyLedsPerStrip);
  FastLED.addLeds<APA102, 42, 43, ColorOrder>(tally_leds[1], TallyLedsPerStrip);
  FastLED.addLeds<APA102, 44, 45, ColorOrder>(tally_leds[2], TallyLedsPerStrip);
  FastLED.addLeds<APA102, 46, 47, ColorOrder>(tally_leds[3], TallyLedsPerStrip);
  FastLED.addLeds<APA102, 48, 49, ColorOrder>(tally_leds[4], TallyLedsPerStrip);
  FastLED.addLeds<APA102, 50, 51, ColorOrder>(tally_leds[5], TallyLedsPerStrip);
  // Pointer abuse to gracefully deal with the special chained tallies
  FastLED.addLeds<APA102, 52, 53, ColorOrder>(tally_leds[6], TallyLedsPerStrip*2);
  
  status_green = true;
  status_red = false;
  
  // Initalize all tallies 'unknown' by default
  for (int i=1; i<Tallies; i++) {
    setTally(i, TallyUnknown);
  }
  // Send the state to the leds.
  FastLED.show();
}

void loop() {
  /*
  // Simple test routine for now, toggle all tallies on and then off one by one
  for (int i=1; i<=Tallies; i++) {
    setTally(i,TallyPGM);
    FastLED.show();
    delay(2000);
  }
    
  for (int i=1; i<=Tallies; i++) {
    setTally(i, TallySafe);
    FastLED.show();
    delay(2000);
  }
  */
  if (process_serial) {
    setTally(msg.tally_number, msg.tally_state);
    process_serial=false;
    FastLED.show();
  }
}

// Read serial data between loops
void serialEvent() {
  Serial.print("X");
  // Read new data until we get a complete message or data is exhausted
  while (!process_serial && Serial.available()) {
    char in = (char)Serial.read();
    if (in == 'Q') {
      // Start of message
      msg.start_magic = 0xff;
      Serial.print("M");
      msg_field = 1;
    } else if (msg_field == 1) {
      uint8_t num;
      if (in > '0' && in < ':') {
        // 1-9
        num = (uint8_t)in - '0';
      } else {
        // Count from A-> (A=10, B=11)
        num = (uint8_t)in - 'A' + 10;
      }
      Serial.print(num);
      Serial.print(" ");
      if (num > 0 && num <= Tallies) {
        msg.tally_number = num;
        msg_field++;
      } else {
        // Tally number out of range
        msg_field = 0;
        Serial.println("E:N");
      }
    } else if (msg_field == 2) {
      msg.tally_state = (uint8_t)in - '0';
      if (msg.tally_state < 5) {
        msg_field++;
        Serial.print(msg.tally_state);
      } else {
        // Tally state out of range
        msg_field = 0;
        Serial.println("E:S");
      }
    } else if (msg_field == 3 && in == 'W') {
      // Got a complete message
      process_serial = true;
      msg_field = 0;
      Serial.println("C");
      return;
    } else {
      // Invalid communication received, too many fields or too little fields
      msg_field = 0;
      Serial.println("EEE");
    }
  }
}

// Set a given tally to a given state
// We use "natural" indexing starting from 1
// Tally mapping: 1-4: Relays 1-4
// 5-13: apa102 chains
// 14-17: Relays 5-8
void setTally(int tally, uint8_t state) {
  if (tally > 0 && tally < 5) {
    // Relays 1-4
    if (state > TallyPVM) {
      // Tally is in PGM
      digitalWrite(relays[tally-1], LOW);
    } else {
      digitalWrite(relays[tally-1], HIGH);
    }
  } else if (tally == 5) {
    // Special case, relay and apa102 chain
    if (state > TallyPVM) {
      // Tally is in PGM
      digitalWrite(relays[4], LOW);
    } else {
      digitalWrite(relays[4], HIGH);
    }
    // Leds
    for (int i = 0; i < TallyLedsPerStrip; i++) {
      tally_leds[0][i] = tallyColor(state);
    }
  } else if (tally < 13) {
    // Apa102 chains
    int chain = tally - 5;
    for (int i = 0; i < TallyLedsPerStrip; i++) {
      tally_leds[chain][i] = tallyColor(state);
    }
  } else if (tally < 16) {
    // Relays 6-8
    // Get the relay pin designator
    int pin = relays[tally-13+5];
    if (state) {
      digitalWrite(pin, LOW);
    } else {
      digitalWrite(pin, HIGH);
    }
  }
  
  // Update status displays
  tally_states[tally-1] = state;
  updateStatus();
}

// Update the status led colors
void updateStatus() {
  if (status_red) {
    // The 'red status led' is on, just display all red
    // This signal will blink so real status will alternate with the error signal
    for (int i = 0; i < Tallies; i++) {
      status_leds[i] = StatusError;
    }
  } else if (status_green) {
    // Green line means everything is fine, display the state that we have
    for (int i = 0; i < Tallies; i++) {
      status_leds[i] = statusColor(i);
    }
  } else {
    // No status "leds" are on
    for (int i = 0; i < Tallies; i++) {
      status_leds[i] = StatusUnknown;
    }
  }
}

uint32_t tallyColor(uint8_t state) {
  uint32_t color;
  // apa102 chains
  switch (state ) {
    case TallyUnknown:  color = ColorUnknown; break;
    case TallySafe:     color = ColorSafe; break;
    case TallyPGM:      color = ColorPGM; break;
    case TallyPGMPVM:   color = ColorPGMPVM; break;
    default:            color = TallyPGM;
  }
  return color;
}

uint32_t statusColor(int tally) {
  uint32_t color;
  switch (tally_states[tally]) {
    case TallyUnknown:  color = StatusUnknown; break;
    case TallySafe:     color = StatusSafe; break;
    case TallyPVM:      color = StatusPVM; break;
    case TallyPGM:      color = StatusPGM; break;
    case TallyPGMPVM:   color = StatusPGMPVM; break;
    default:            color = StatusError;
  }
  return color;
}

// Simple fuction to just move a dot across all the chains to test wiring
void ledTest() {
  //Front status
  for (int i = 0; i < 12; i++) {
     front_status[i] = CRGB::Red;
     FastLED.show();
     front_status[i] = CRGB::Black;
     delay(50);
  }
  
  //Remote status leds
  for (int i = 0; i < 16; i++) {
    remote_status[i] = CRGB::Red;
    FastLED.show();
    remote_status[i] = CRGB::Black;
  }
  
  // Loop over all the various tally strips
  for (int x = 0; x < TallyStrips; x++) {
    // This inner loop will go over each led in the current strip, one at a time
    for (int i = 0; i < TallyLedsPerStrip; i++) {
      tally_leds[x][i] = CRGB::Red;
      FastLED.show();
      tally_leds[x][i] = CRGB::Black;
      delay(50);
    }
  }
}
