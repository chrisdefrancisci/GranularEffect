#include <Panel.h>

// GUI variables
extern Adafruit_ILI9341 tft;
extern Adafruit_FT6206 ctp;

#define MAX_UINT8 255
// due to screen orientation x/width is the horizontal axis
// and y/height is vertical axis

// parameters are:
// - grain size
// - density
// - on/off
// - recording/freeze

const uint8_t w = 40;
const uint8_t h = 40;
const uint8_t b = 10;

Menu menu;

// functions
bool updateGrain( uint16_t x, uint16_t y, Panel *ppanel );
bool updateScale( uint16_t x, uint16_t y, Panel *ppanel );
bool updateOn( uint16_t x, uint16_t y, Panel *ppanel );
//bool updateRec( uint16_t x, uint16_t y, Panel *ppanel );

// enable/disable effect
volatile bool on = 0;
// enable/disable recording
volatile bool rec = 1;

// audio storage
// use 4 buffers of length 256 so index variable can be 8-bit
// and can easily cycle through buffers
const uint16_t BUF_LEN = 256;
const uint8_t NUM_BUF = 4;
// read from two buffers and write to one 
// imagine as one gigantic circular buffer
uint8_t audio_buffer[NUM_BUF][BUF_LEN];
volatile uint8_t wb = 1; // which buffer is currently being written to
volatile uint8_t rb = 0; // which buffer is currently being read from
volatile uint8_t wp = 0; // which index is currently being written to
volatile uint8_t rp = 0; // which index is currently being read from
volatile uint8_t multiplier = 0; // used to play grain more than once if necessary
// variables for grain effect

// -distance between adjacent grains 
// positive => grains overlap
// negative => space between grains
volatile int8_t density = 0; 
// used for when density < 0
volatile uint16_t cloud_max = (-density)<<7;
volatile uint16_t cloud_count = cloud_max; 
bool cloud_toggle = 0;
// used for when density > 0, maybe could try to combine this with above?
volatile uint8_t mod = MAX_UINT8;
volatile uint8_t newMod = mod;

//data retrieval variables
//volatile uint8_t index = 0; // current position in buffer
// used for density
//volatile uint16_t modulo = BUF_LEN - density;
//uint16_t newModulo = modulo;
//uint16_t r2_start = 0;
//uint8_t iscale = 0; //index variable



void setup() {
  // setting up GUI
  tft.begin();
  ctp.begin(40);
  
  tft.fillScreen(BLACK);
  Panel *grainFader, *scaleKnob, *fxButton;
//  , *freezeButton;
  // size of grain
  grainFader = new Fader(0,h/2, maxx, h, &updateGrain, FG_COLOR2);
  menu.addPanel( grainFader );
  // Changes playback speed
  scaleKnob = new Knob(maxx/2-3*w/4, 2*h, (w+w/2), (h+h/2), &updateScale, FG_COLOR2);
  menu.addPanel( scaleKnob );
  // turn effect on or off 
  fxButton = new Button(maxx/2-w/2, 4*h, w, h, &updateOn, FG_COLOR2);
  menu.addPanel( fxButton );
  // record or hold note
//  freezeButton = new Button(maxx/2-w/2, 6*h, w, h, &updateRec, FG_COLOR2);
//  menu.addPanel( freezeButton );
  // Draw the menu
  menu.drawMenu();

  // setting up interrupts on Analog Read
  DDRD = 0xFE;//set digital pins 0-7 as outputs
  //DDRC=0x00;//set all analog pins as inputs
  DDRC &= B11111110;
  cli();//diable interrupts

  //set up continuous sampling of analog pin 0
  //clear ADCSRA and ADCSRB registers
  ADCSRA = 0;
  ADCSRB = 0;

  ADMUX = 0;//Clear ADMUX register
  ADMUX |= (1 << REFS0); //set reference voltage
  ADMUX |= (1 << ADLAR); //left align the ADC value- so I can read highest 8 bits from ADCH register only
  //since I'm reading A0, I don't need to specifiy which analog pin I want to read from (0 is default)

  ADCSRA |= (1 << ADPS2) | (1 << ADPS0); //set ADC clock with 32 prescaler- 16mHz/32=500kHz
  ADCSRA |= (1 << ADATE); //enabble auto trigger
  ADCSRA |= (1 << ADIE); //enable interrupts when measurement complete
  ADCSRA |= (1 << ADEN); //enable ADC
  ADCSRA |= (1 << ADSC); //start ADC measurements

  sei();//enable interrupts
}

void loop() {
  if (! ctp.touched()) {
    return;
  }

  // Retrieve a point  
  TS_Point p = ctp.getPoint();
  // flip it around to match the screen.
  p.x = map(p.x, 0, 240, 240, 0);
  p.y = map(p.y, 0, 320, 320, 0);
  
  menu.isTouched(p.x, p.y);
}


// Update values from touch screen
bool updateGrain( uint16_t x, uint16_t y, Panel *ppanel )
{
  // can only be used with Fader class
  //uint8_t value;
  uint8_t minval = ppanel->getMin();
  uint8_t maxval = ppanel->getMax();
  // minval/maxval don't exist for some panel objects
  // maybe should just trust that this function is being used with a fader?
  if( minval == 255 || maxval == 255 )
  {
    return false;
  }
  // keep x within bounds
  if( x < ppanel->getMin() )
  {
    x = ppanel->getMin();
  }
  else if( x > ppanel->getMax() )
  {
    x = ppanel->getMax();
  }
  // currently does nothing

  
  return true;
}

bool updateOn( uint16_t x, uint16_t y, Panel *ppanel )
{
  on = !on;
  
  return true;
}

bool updateScale( uint16_t x, uint16_t y, Panel *ppanel )
{
  uint8_t theta;
  // getMax actually refers to radius here
  // The range from ~5:00 to 7:00 is out of bounds
  // so if x is too low, bring it up
  if ( x - ppanel->getX() < ppanel->getMax()/2 )
  {
    x = ppanel->getMax()/2 + ppanel->getX();
  }
  theta = getTheta(x, y, ppanel );

  density = map(theta, 0, 255, -128, 127);
  if ( density < 0 ){ // gets greater range
    newMod = MAX_UINT8;
    cloud_max = (-density)<<6; // should probably use new variables here
    cloud_count = 0; // should probably use new variables here
  }
  else{
    newMod = BUF_LEN - density;
    cloud_max = 0; // should probably use new variables here
    cloud_count = 0; // should probably use new variables here
  }

  return true;
}

//bool updateRec( uint16_t x, uint16_t y, Panel *ppanel )
//{
//  newFreeze = !newFreeze;
//  return true;
//}

// interrupt routine
ISR(ADC_vect) {//when new ADC value ready
  // put new value in buffer
  if(rec){
    audio_buffer[wb][wp] = ADCH;
  }

  if(on){ // if the effect is on
    // determine output
    if( density > 0 && rp < density ){ 
      // add current sample to sample from opposite buffer/other grain, "normalize amplitude"
      // (rb+3)&0x03 gives a convenient way of subtracting 1 when we only care about
      // values 0 through 3 (although I think (rb-1)&0x03 would also work...
      // blends samples from previous grain into current grain
      PORTD = (audio_buffer[rb][rp] + audio_buffer[(rb+3)&0x03][mod + rp])>>2;

    } // density < 0 
    else if ( density < 0 ){ // density >= 0, grains have space between
      if ( cloud_count < cloud_max ){ // if in the "gap" between grains
        PORTD = 128; // set output to "0"
        cloud_count++;
        if( cloud_count == cloud_max){ // start index over
          rp = 0;
        }
      }    
      else{ // output signal
        PORTD = ADCH;//audio_buffer[rb][index];
        if( rp == 255 && rb == 0 ){
          // if at the end of all 4 buffers
          // start the gap before next grain
          cloud_count = 0;
        }
      }
    }
    
    else{ // just output single value
      PORTD = audio_buffer[rb][rp];
    }
  }
  else { // not on
    PORTD = ADCH;
  }
  

  if( wp == MAX_UINT8 ){ 
    // finished writing to a buffer
    // stop writing until rp reaches end
    rec = 0;
  }
  if( wp >= mod ){
    // at the end of a buffer
    if (density > 0){ // for overlapping grains
      if( wp == MAX_UINT8){
        rp = MAX_UINT8;   // will overflow after incremented
        wp = MAX_UINT8;   // and become zero
        mod = newMod; 
        // change to next buffers
        wb++;
        wb &= 0x03;
        rb++;
        rb &= 0x03;
      }
      else {
        rp = MAX_UINT8;
      }
    }
    else { // for not overlapping grains     
      rp = MAX_UINT8; // will roll over to 0 after increment 
      mod = newMod; 
      wp = MAX_UINT8;
      rec = 1;
    }     
  }
  // increment variables 
  wp++;
  rp++;
}

