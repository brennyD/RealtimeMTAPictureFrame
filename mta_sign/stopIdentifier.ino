#include <Adafruit_Protomatter.h>  // For RGB matrix

uint8_t rgbPins[] = { 42, 41, 40, 38, 39, 37 };
uint8_t addrPins[] = { 45, 36, 48, 35, 21 };
uint8_t clockPin = 2;
uint8_t latchPin = 47;
uint8_t oePin = 14;

#define WIDTH 32
#define HEIGHT 16
#define TOTAL_STATES 3

Adafruit_Protomatter matrix(
  WIDTH, 6, 1, rgbPins, 3, addrPins,
  clockPin, latchPin, oePin, false);


uint8_t state = 0;
uint32_t position = 0;

void setup() {
  Serial.begin(9600);
  matrix.begin();
  

}

void loop() {
   int x = (position) % WIDTH;
   int y = ((position) / WIDTH) % HEIGHT;
   matrix.fillScreen(matrix.color565(0,0,0));
  if(Serial.available() > 0){
    char foo = Serial.read();
    if(foo == 113) { //q
      state++;
    }
    if(state % TOTAL_STATES == 2) {
      
      if(foo == 97) {
        position--;
      } else if (foo == 100) {
        position++;
      }
    }
  }

  if(state % TOTAL_STATES == 0) {
    matrix.fillScreen(matrix.color565(255,255,255));
  } else if (state % TOTAL_STATES == 1) {
    position++;
  }
  matrix.drawPixel(x,y, matrix.color565(255,255,255));
  Serial.print("X: ");Serial.print(x);Serial.print(" Y: ");Serial.println(y);


  matrix.show();
  delay(1000);
}
