#include <Panel.h>

// GUI variables
extern Adafruit_ILI9341 tft;
extern Adafruit_FT6206 ctp;

const uint8_t w = 40;
const uint8_t h = 40;
const uint8_t b = 10;
Menu menu;

// functions
bool updateGrain( uint16_t x, uint16_t y, Panel *ppanel );
bool updateScale( uint16_t x, uint16_t y, Panel *ppanel );
bool updateOn( uint16_t x, uint16_t y, Panel *ppanel );
bool updateRec( uint16_t x, uint16_t y, Panel *ppanel );

// enable/disable effect
bool on = 0;

//audio storage
const uint16_t BUF_LEN = 255;
uint8_t buffer1[BUF_LEN];
uint8_t buffer2[BUF_LEN];

//buffer recording variables
bool toggle = 0;
bool rec = 1;
bool newRec = rec;

//pot checking storage
uint8_t scale = 1;
uint8_t newScale = scale;
uint8_t multiplier = 0;
// grain is half of grain size, in order to use 8-bit multiplication
// then shift over
uint8_t grain = BUF_LEN/2;
uint8_t newGrain = grain;
uint16_t newGrainScale = grain*scale;
uint16_t grainScale = newGrainScale;

//data retrieval variables
uint8_t i = 0;//index variable
uint8_t iscale = 0;//index variable

void setup() {
  // setting up GUI
  tft.begin();
  ctp.begin(40);
  
  tft.fillScreen(BLACK);
  Panel *grainFader, *scaleKnob, *fxButton, *recButton;
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
  recButton = new Button(maxx/2-w/2, 6*h, w, h, &updateRec, FG_COLOR2);
  menu.addPanel( recButton );
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


// other functions
bool updateGrain( uint16_t x, uint16_t y, Panel *ppanel )
{
  // can only be used with Fader class
  uint8_t value;
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
  newGrain = map(x, minval, maxval, 1, BUF_LEN/2);
  newGrainScale = newGrain * newScale;
  
  return true;
}

bool updateOn( uint16_t x, uint16_t y, Panel *ppanel )
{
  on = !on;
}

bool updateScale( uint16_t x, uint16_t y, Panel *ppanel )
{
  uint8_t theta;
  // getMax actually refers to radius
  if ( x - ppanel->getX() < ppanel->getMax()/2 )
  {
    x = ppanel->getMax()/2 + ppanel->getX();
  }
  theta = getTheta(x, y, ppanel );
  
  newScale = map(theta, 0, 255, 1, 16);
  // just testing knob
//  tft.fillRect(20, 80, 20, 10, BLACK);
//  tft.setCursor(20, 80);
//  tft.print(theta);
  
  newGrainScale = newGrain * newScale;
  
  return true;
}

bool updateRec( uint16_t x, uint16_t y, Panel *ppanel )
{
  newRec = !newRec;
}

// interrupt routine
ISR(ADC_vect) {//when new ADC value ready
  if (rec){
    if (toggle){
      buffer1[i] = ADCH;//store incoming
      if( on ) {
        PORTD = buffer2[iscale];
      }
      else{
        PORTD = ADCH;
      }
    } // toggle
    else{ 
      buffer2[i] = ADCH;//store incoming
      if( on ){
        PORTD = buffer1[iscale];
      }
      else{
        PORTD = ADCH;
      }
    }
  } // rec
  else{
    if (toggle){
      if( on ){
        PORTD = buffer2[iscale];
      }
      else{
        PORTD = ADCH;
      }
    }
    else{
      if( on ){
        PORTD = buffer1[iscale];
      }
      else{
        PORTD = ADCH;
      }
    }
  } // rec
  
  i++;//increment i
  iscale = (i*8)/scale - ((grain*multiplier)<<1);
  if (i==(grain<<1)){
    rec = 0;//stop recording
  }

  if (i>=(grainScale)/8){
    if (scale<8){
      if (i==(grain<<1)){
        i = 0;
        iscale = 0;
        rec = newRec; // update rec
        scale = newScale;//update scale
        grain = newGrain;//update grain
        grainScale = newGrainScale; // update product of grain and scale
        toggle ^= 1;//try removing this
        multiplier = 0;
      }
      else if (iscale>=(grain<<1)){
        iscale = 0;
        multiplier++;
      }
    } 
    else{ 
      i = 0;
      iscale = 0;
      rec = newRec; // update rec
      scale = newScale;//update scale
      grain = newGrain;//update grain
      grainScale = newGrainScale; // update product of grain and scale
      toggle ^= 1;//try removing this
      multiplier = 0;
    }
  }
}
