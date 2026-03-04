#include <Arduino_GFX_Library.h>

/* --- Pin Configurations --- */
#define TFT_SCK  19 // D8
#define TFT_MOSI 21 // D10
#define TFT_MISO -1 // Not used for this display
#define TFT_CS   1  // D1
#define TFT_DC   3  // D3
#define TFT_RST  2  // D2

#define ENC_A    20 // D4
#define ENC_B    22 // D5
#define ENC_SW   0  // D0

#define BLACK 0x0000
#define WHITE 0xFFFF
#define GREEN 0x07E0
#define CYAN  0x07FF
#define RED   0xF800
/* --- Display Setup --- */
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0 /* Rotation */, true /* IPS */);

/* --- Variables --- */
int counter = 0;
int aState;
int aLastState;  

void setup() {
  Serial.begin(115200);

  // Start Display
  if (!gfx->begin()) {
    Serial.println("Display Init Failed!");
  }
  gfx->fillScreen(BLACK);
  
  // Setup Encoder Pins
  pinMode(ENC_A, INPUT);
  pinMode(ENC_B, INPUT);
  pinMode(ENC_SW, INPUT_PULLUP);

  aLastState = digitalRead(ENC_A);

  // Initial UI
  updateDisplay();
}

void loop() {
  aState = digitalRead(ENC_A); 

  // Encoder rotation logic
  if (aState != aLastState) {     
    if (digitalRead(ENC_B) != aState) { 
      counter++;
    } else {
      counter--;
    }
    updateDisplay();
  } 
  aLastState = aState;

  // Reset button logic
  if (digitalRead(ENC_SW) == LOW) {
    counter = 0;
    updateDisplay();
    delay(300); // Debounce
  }
}

void updateDisplay() {
  gfx->fillScreen(BLACK);
  gfx->setCursor(60, 100);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(3);
  gfx->print("Value: ");
  gfx->setCursor(100, 140);
  gfx->setTextColor(GREEN);
  gfx->print(counter);
}