/*
 This code is optimized for understandability and changability rather than raw speed
 More info at http://wp.josh.com/2014/05/11/ws2812-neopixels-made-easy/
*/

// Change this to be at least as long as your pixel string (too long will work fine, just be a little slower)

#define PIXELS_INNER 40  // Number of pixels in the inner lower string
#define PIXELS_OUTER 80  // Number of pixels in the outer lower string
#define PIXELS_UPPER 120  // Number of pixels in the upper string
#define PIXELS_TOTAL 240  // Total number of pixels

// These values depend on which pin your string is connected to and what board you are using 
// More info on how to find these at http://www.arduino.cc/en/Reference/PortManipulation

// These values are for the pin that connects to the Data Input pin on the LED strip. They correspond to...

// Arduino Leo/Yun: Digital Pin 8
// DueMilinove/UNO: Digital Pin 12
// Arduino MeagL    PWM Pin 4

// You'll need to look up the port/bit combination for other boards. 

// Note that you could also include the DigitalWriteFast header file to not need to to this lookup.

#define PIXEL_PORT  PORTB  // Port of the pin the pixels are connected to
#define PIXEL_DDR   DDRB   // Port of the pin the pixels are connected to
#define PIXEL_BIT   4      // Bit of the pin the pixels are connected to

// These are the timing constraints taken mostly from the WS2812 datasheets 
// These are chosen to be conservative and avoid problems rather than for maximum throughput 

#define T1H  900    // Width of a 1 bit in ns
#define T1L  600    // Width of a 1 bit in ns

#define T0H  400    // Width of a 0 bit in ns
#define T0L  900    // Width of a 0 bit in ns

#define RES 6000    // Width of the low gap between bits to cause a frame to latch

// Here are some convience defines for using nanoseconds specs to generate actual CPU delays

#define NS_PER_SEC (1000000000L)          // Note that this has to be SIGNED since we want to be able to check for negative values of derivatives

#define CYCLES_PER_SEC (F_CPU)

#define NS_PER_CYCLE ( NS_PER_SEC / CYCLES_PER_SEC )

#define NS_TO_CYCLES(n) ( (n) / NS_PER_CYCLE )

// Mathematical constants

#define QUANTA 100

// Dynamic values we can store globally

int counter = 0;
int stringOffset;    // offset within the LUT for the dominant LED effect
int beatOffset = 0;
int waves = 1;
int colorIndex = 0;
int beatIntensity = 0.1;
int r, g, b;    // cumulative red, green, & blue values per pixel
byte rVal[PIXELS_TOTAL];
byte gVal[PIXELS_TOTAL];
byte bVal[PIXELS_TOTAL];

// Here are lookup tables for our events

float overallAmplitude[QUANTA];    // contains the dominant LED effect (sinusoidal function)
byte redColor[QUANTA];    // how much red to show given the independent variable
byte greenColor[QUANTA];    // how much green to show given the independent variable
byte blueColor[QUANTA];    // how much blue to show given the independent variable
//float beatAmplitude[QUANTA];    // intensify the LEDs on this curve based on the beat

// Actually send a bit to the string. We must to drop to asm to enusre that the complier does
// not reorder things and make it so the delay happens in the wrong place.

inline void sendBit( bool bitVal ) {
  if (  bitVal ) {        // 0 bit
    asm volatile (
      "sbi %[port], %[bit] \n\t"        // Set the output bit
      ".rept %[onCycles] \n\t"                                // Execute NOPs to delay exactly the specified number of cycles
      "nop \n\t"
      ".endr \n\t"
      "cbi %[port], %[bit] \n\t"                              // Clear the output bit
      ".rept %[offCycles] \n\t"                               // Execute NOPs to delay exactly the specified number of cycles
      "nop \n\t"
      ".endr \n\t"
      ::
      [port]    "I" (_SFR_IO_ADDR(PIXEL_PORT)),
      [bit]   "I" (PIXEL_BIT),
      [onCycles]  "I" (NS_TO_CYCLES(T1H) - 2),    // 1-bit width less overhead  for the actual bit setting, note that this delay could be longer and everything would still work
      [offCycles]   "I" (NS_TO_CYCLES(T1L) - 2)     // Minimum interbit delay. Note that we probably don't need this at all since the loop overhead will be enough, but here for correctness
    );
  } else {          // 1 bit
    // **************************************************************************
    // This line is really the only tight goldilocks timing in the whole program!
    // **************************************************************************


    asm volatile (
      "sbi %[port], %[bit] \n\t"        // Set the output bit
      ".rept %[onCycles] \n\t"        // Now timing actually matters. The 0-bit must be long enough to be detected but not too long or it will be a 1-bit
      "nop \n\t"                                              // Execute NOPs to delay exactly the specified number of cycles
      ".endr \n\t"
      "cbi %[port], %[bit] \n\t"                              // Clear the output bit
      ".rept %[offCycles] \n\t"                               // Execute NOPs to delay exactly the specified number of cycles
      "nop \n\t"
      ".endr \n\t"
      ::
      [port]    "I" (_SFR_IO_ADDR(PIXEL_PORT)),
      [bit]   "I" (PIXEL_BIT),
      [onCycles]  "I" (NS_TO_CYCLES(T0H) - 2),
      [offCycles] "I" (NS_TO_CYCLES(T0L) - 2)
    );
  }
    
    // Note that the inter-bit gap can be as long as you want as long as it doesn't exceed the 5us reset timeout (which is A long time)
    // Here I have been generous and not tried to squeeze the gap tight but instead erred on the side of lots of extra time.
    // This has thenice side effect of avoid glitches on very long strings becuase ... :-P
}  

  
inline void sendByte( unsigned char byte ) {
  for( unsigned char bit = 0 ; bit < 8 ; bit++ ) {
    sendBit( bitRead( byte , 7 ) );                // Neopixel wants bit in highest-to-lowest order
                                                   // so send highest bit (bit #7 in an 8-bit byte since they start at 0)
    byte <<= 1;                                    // and then shift left so bit 6 moves into 7, 5 moves into 6, etc
  }
} 

/*
  The following three functions are the public API:
  
  ledSetup() - set up the pin that is connected to the string. Call once at the begining of the program.  
  sendPixel( r g , b ) - send a single pixel to the string. Call this once for each pixel in a frame.
  show() - show the recently sent pixel on the LEDs . Call once per frame. 
  
*/


// Set the specified pin up as digital out

void ledsetup() {
  bitSet( PIXEL_DDR , PIXEL_BIT );
}

inline void sendPixel( unsigned char r, unsigned char g , unsigned char b )  {
  sendByte(g);          // Neopixel wants colors in green then red then blue order
  sendByte(r);
  sendByte(b);
}


// Just wait long enough without sending any bits to cause the pixels to latch and display the last sent frame

void show() {
  _delay_us( (RES / 1000UL) + 1);       // Round up since the delay must be _at_least_ this long (too short might not work, too long not a problem)
}


/*
  That is the whole API. What follows are some demo functions rewriten from the AdaFruit strandtest code...
  
  https://github.com/adafruit/Adafruit_NeoPixel/blob/master/examples/strandtest/strandtest.ino
  
  Note that we always turn off interrupts while we are sending pixels becuase an interupt
  could happen just when we were in the middle of somehting time sensitive.
  
  If we wanted to minimize the time interrupts were off, we could instead 
  could get away with only turning off interrupts just for the very brief moment 
  when we are actually sending a 0 bit (~1us), as long as we were sure that the total time 
  taken by any interrupts + the time in our pixel generation code never exceeded the reset time (5us).
  
*/


// Display a single color on the whole string

void showColor(unsigned char r, unsigned char g, unsigned char b) {
  cli();
  for (int p = 0; p < 2; p++) {
    sendPixel( r , g , b );
  }
  sei();
  show();
}

byte figureColor(float scaledIndex, float colorAmount) {
  return overallAmplitude[int((waves * scaledIndex) + stringOffset) % QUANTA]
        * colorAmount
        //* (1 - (beatIntensity * beatAmplitude[beatOffset]))
        * 2.55;
}

void showFrame() {
  float scaledIndex;
  int pp;
  float beginRatio, endRatio;
  for (int tableSide = 0; tableSide < 4; tableSide++) {
    beginRatio = tableSide / 4.0;
    endRatio = (tableSide + 1) / 4.0;
    for (int p = int(PIXELS_INNER * beginRatio); p < int(PIXELS_INNER * endRatio); p++, pp++) {
      scaledIndex = p * 100.0 / PIXELS_INNER;
      rVal[pp] = figureColor(scaledIndex, redColor[colorIndex]);
      gVal[pp] = figureColor(scaledIndex, greenColor[colorIndex]);
      bVal[pp] = figureColor(scaledIndex, blueColor[colorIndex]);
    }
    for (int p = int(PIXELS_OUTER * beginRatio); p < int(PIXELS_OUTER * endRatio); p++, pp++) {
      scaledIndex = p * 100.0 / PIXELS_OUTER;
      rVal[pp] = figureColor(scaledIndex, redColor[colorIndex]);
      gVal[pp] = figureColor(scaledIndex, greenColor[colorIndex]);
      bVal[pp] = figureColor(scaledIndex, blueColor[colorIndex]);
    }
    for (int p = int(PIXELS_UPPER * beginRatio); p < int(PIXELS_UPPER * endRatio); p++, pp++) {
      scaledIndex = p * 100.0 / PIXELS_UPPER;
      rVal[pp] = figureColor(scaledIndex, redColor[colorIndex]);
      gVal[pp] = figureColor(scaledIndex, greenColor[colorIndex]);
      bVal[pp] = figureColor(scaledIndex, blueColor[colorIndex]);
    }
  }
  cli();
  for (int p = 0; p < PIXELS_TOTAL; p++) {
    sendPixel(rVal[p], gVal[p], bVal[p]);
  }
  sei();
  show();
}


//void detonate( unsigned char r, unsigned char g, unsigned char b, unsigned int startdelayms) {
//  while (startdelayms) {
//    showColor( r , g , b );      // Flash the color
//    showColor( 0 , 0 , 0 );
//    delay( startdelayms );
//    startdelayms =  ( startdelayms * 4 ) / 5 ;           // delay between flashes is halved each time until zero
//  }

  // Then we fade to black....
//  for (int fade = 256; fade > 0; fade--) {
//    showColor( (r * fade) / 256 ,(g*fade) /256 , (b*fade)/256 );
//  }
//  showColor(0, 0, 0);
//}


void setup() {
  analogReference(EXTERNAL);
  ledsetup();
//  Serial.begin(9600);
  // sin^2(x) in radians so the waveform always stays positive
  int i;
  float x;
  for (i = 0, x = 0.0; i < QUANTA; i++, x += (PI / double(QUANTA))) {
    //sin^2(x):
    //overallAmplitude[i] = 0.5 * (1.0 - cos(2.0 * x));
    //sin^6(x):
    overallAmplitude[i] = ((-15 * cos(2.0 * x)) + (6.0 * cos(4.0 * x)) - cos(6.0 * x) + 10) / 32.0; 
  }
  // red value starts = 0 and grows from x = 0 to x = QUANTA/2, where it becomes & stays 1
  for (int i = 0, x = 0; i < QUANTA; i++, x += (2 * 100 / QUANTA)) {
    redColor[i] = (i > QUANTA / 2) ? 100 : x;
  }
  // blue value starts = 0 and peaks at 1 at QUANTA/2 then drops back to 0
  for (int i = 0, x = -100; i < QUANTA; i++, x += (2 * 100 / QUANTA)) {
    greenColor[i] = -1 * abs(x) + 100;
  }
  // blue value starts = 1 and shrinks from x = QUANTA/2 to x = QUANTA down to 0
  for (int i = 0, x = 2; i < QUANTA; i++, x -= (2 * 100 / QUANTA)) {
    blueColor[i] = (i <= QUANTA / 2) ? 100 : x;
  }
  // simple sawtooth function for now
//  for (i = 0; i < QUANTA; i++) {
//    beatAmplitude[QUANTA] = x;
//  }
}

#define SONAR_SENSOR_LBOUND 50
#define SONAR_SENSOR_UBOUND 900
#define SONAR_SENSOR_DIFF   (SONAR_SENSOR_UBOUND - SONAR_SENSOR_LBOUND)
#define MAX_JUMP_VALUE 10
int jumpScale = SONAR_SENSOR_DIFF / MAX_JUMP_VALUE;

void loop() {
  // Some example procedures showing how to display to the pixels:
  showFrame();
  //showColor(255,255,255);
  //stringOffset = (stringOffset + 1) % QUANTA;
    int pinA0 = analogRead(A0);
  int jump = 1;
  if (pinA0 > 0) {
    jump = min(max(0, (pinA0 - SONAR_SENSOR_LBOUND) / jumpScale), MAX_JUMP_VALUE) + 4;
  }
  stringOffset = stringOffset + jump;
  stringOffset %= QUANTA;
  counter++;
  if (counter % 2 == 0) {
    beatOffset = (beatOffset + 1) % QUANTA;
  }
  int pinA1 = analogRead(A1);
  if (pinA1 > -1) {
    colorIndex = min(max(0, (pinA1 - SONAR_SENSOR_LBOUND + 40) / (1.0 * SONAR_SENSOR_DIFF / QUANTA)), QUANTA - 1);
    //colorIndex += 1;
    //colorIndex %= 100;
  }
//  Serial.print(pinA0);
//  Serial.print("\t");
//  Serial.print(pinA1);
//  Serial.print("\t");
//  Serial.print(colorIndex);
//  Serial.println();

  if (counter % 20 == 0) {
    beatIntensity = (beatIntensity + 0.1);
    if (beatIntensity > 1.01)
      beatIntensity = 0.1;
  }
  if (counter % 800 == 0) {
    //waves = (waves % 10) + 1; 
    counter = 0;
  }
  //detonate(255, 255, 255, 1000);
}

