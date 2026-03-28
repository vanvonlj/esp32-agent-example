#include <FastLED.h>
#include "config/BoardConfig.h"
#include "lib/MatrixUtil/MatrixUtil.h"

#define NUM_LEDS (MATRIX_WIDTH * MATRIX_HEIGHT)
CRGB leds[NUM_LEDS];

// Game settings
#define MAX_METEORS 8
#define INITIAL_METEOR_SPEED 400  // ms between meteor movements
#define MIN_METEOR_SPEED 100      // Maximum speed
#define SPAWN_CHANCE 30           // % chance to spawn meteor each cycle

// Player position (bottom row)
int8_t playerX = MATRIX_WIDTH / 2;

// Meteor structure
struct Meteor {
  int8_t x;
  int8_t y;
  CRGB color;
  bool active;
};

Meteor meteors[MAX_METEORS];

// Game state
unsigned long lastMeteorMove = 0;
unsigned long meteorSpeed = INITIAL_METEOR_SPEED;
uint16_t score = 0;
bool gameOver = false;
unsigned long gameOverTime = 0;
bool paused = false;
unsigned long pauseBlinkTime = 0;

// IMU includes (reusing from Snake)
#include "WS_QMI8658.h"
extern IMUdata Accel;

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 1500) { delay(10); }
  if (Serial) MU_PrintMeta();

  // Initialize LED matrix
  MU_ADD_LEDS(LED_PIN, leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS_LIMIT);
  FastLED.clear();
  FastLED.show();

  // Initialize IMU
  QMI8658_Init();

  // Initialize game
  resetGame();

  if (Serial) {
    Serial.println("=== METEOR DODGE ===");
    Serial.println("Tilt left/right to dodge meteors!");
    Serial.println("Hold board flat to PAUSE");
    Serial.println("Tilt backward to RESUME");
  }
}

void resetGame() {
  playerX = MATRIX_WIDTH / 2;
  score = 0;
  meteorSpeed = INITIAL_METEOR_SPEED;
  gameOver = false;

  // Clear all meteors
  for (int i = 0; i < MAX_METEORS; i++) {
    meteors[i].active = false;
  }
}

void spawnMeteor() {
  // Find inactive meteor slot
  for (int i = 0; i < MAX_METEORS; i++) {
    if (!meteors[i].active) {
      meteors[i].x = random(0, MATRIX_WIDTH);
      meteors[i].y = 0;
      meteors[i].active = true;

      // Random meteor colors
      int colorChoice = random(0, 5);
      switch(colorChoice) {
        case 0: meteors[i].color = CRGB(60, 0, 0); break;   // Red
        case 1: meteors[i].color = CRGB(60, 30, 0); break;  // Orange
        case 2: meteors[i].color = CRGB(60, 60, 0); break;  // Yellow
        case 3: meteors[i].color = CRGB(60, 0, 60); break;  // Magenta
        case 4: meteors[i].color = CRGB(0, 60, 60); break;  // Cyan
      }
      break;
    }
  }
}

void updateMeteors() {
  // Move meteors down
  for (int i = 0; i < MAX_METEORS; i++) {
    if (meteors[i].active) {
      meteors[i].y++;

      // Check if meteor hit bottom row (player row)
      if (meteors[i].y >= MATRIX_HEIGHT - 1) {
        // Check collision with player
        if (meteors[i].x == playerX) {
          gameOver = true;
          gameOverTime = millis();
          if (Serial) {
            Serial.print("GAME OVER! Final Score: ");
            Serial.println(score);
          }
        }
        // Deactivate meteor
        meteors[i].active = false;

        // Increase score if meteor missed (reached bottom without hitting)
        if (!gameOver && meteors[i].y >= MATRIX_HEIGHT - 1) {
          score++;

          // Speed up every 5 points
          if (score % 5 == 0 && meteorSpeed > MIN_METEOR_SPEED) {
            meteorSpeed -= 20;
            if (Serial) {
              Serial.print("Level up! Score: ");
              Serial.print(score);
              Serial.print(" Speed: ");
              Serial.println(meteorSpeed);
            }
          }
        }
      }
    }
  }

  // Spawn new meteors randomly
  if (random(0, 100) < SPAWN_CHANCE) {
    spawnMeteor();
  }
}

void updatePlayer() {
  // Read IMU
  QMI8658_Loop();

  // Tilt controls (Y-axis for left/right)
  static uint8_t tiltAccumLeft = 0;
  static uint8_t tiltAccumRight = 0;

  if (Accel.y > 0.2) {
    tiltAccumLeft += Accel.y * 15;
    tiltAccumRight = 0;
  } else if (Accel.y < -0.2) {
    tiltAccumRight += abs(Accel.y) * 15;
    tiltAccumLeft = 0;
  } else {
    tiltAccumLeft = 0;
    tiltAccumRight = 0;
  }

  // Move player based on accumulated tilt
  if (tiltAccumLeft >= 10) {
    playerX--;
    if (playerX < 0) playerX = 0;
    tiltAccumLeft = 0;
  }
  if (tiltAccumRight >= 10) {
    playerX++;
    if (playerX >= MATRIX_WIDTH) playerX = MATRIX_WIDTH - 1;
    tiltAccumRight = 0;
  }
}

void drawGame() {
  FastLED.clear();

  if (gameOver) {
    // Game over animation - flash red
    if ((millis() - gameOverTime) % 500 < 250) {
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(60, 0, 0);
      }
    }
  } else if (paused) {
    // Paused - show game state with blinking blue border
    // Draw meteors
    for (int i = 0; i < MAX_METEORS; i++) {
      if (meteors[i].active) {
        leds[MU_XY(meteors[i].x, meteors[i].y)] = meteors[i].color;
      }
    }
    // Draw player (dimmed)
    leds[MU_XY(playerX, MATRIX_HEIGHT - 1)] = CRGB(0, 30, 0);

    // Blinking blue border to indicate pause
    if ((millis() - pauseBlinkTime) % 600 < 300) {
      // Top and bottom rows
      for (int x = 0; x < MATRIX_WIDTH; x++) {
        leds[MU_XY(x, 0)] = CRGB(0, 0, 60);
        leds[MU_XY(x, MATRIX_HEIGHT - 1)] = CRGB(0, 0, 60);
      }
      // Left and right columns
      for (int y = 1; y < MATRIX_HEIGHT - 1; y++) {
        leds[MU_XY(0, y)] = CRGB(0, 0, 60);
        leds[MU_XY(MATRIX_WIDTH - 1, y)] = CRGB(0, 0, 60);
      }
    }
  } else {
    // Draw meteors
    for (int i = 0; i < MAX_METEORS; i++) {
      if (meteors[i].active) {
        leds[MU_XY(meteors[i].x, meteors[i].y)] = meteors[i].color;
      }
    }

    // Draw player (green spaceship)
    leds[MU_XY(playerX, MATRIX_HEIGHT - 1)] = CRGB(0, 60, 0);
  }

  FastLED.show();
  if (Serial) MU_SendFrameCSV(leds);
}

void loop() {
  unsigned long currentTime = millis();

  // Read IMU for pause detection
  QMI8658_Loop();

  // Pause: board held flat (Z-axis near -1.0g, minimal X/Y tilt)
  // Unpause: tilt backward (negative X > 0.3)
  static bool wasPauseGesture = false;
  static bool wasUnpauseGesture = false;

  bool isPauseGesture = (Accel.z > -1.05 && Accel.z < -0.85 &&
                         abs(Accel.x) < 0.15 && abs(Accel.y) < 0.15);
  bool isUnpauseGesture = (Accel.x < -0.3);  // Tilt backward

  if (isPauseGesture && !wasPauseGesture && !gameOver && !paused) {
    // Pause on new flat gesture
    paused = true;
    pauseBlinkTime = millis();
    if (Serial) Serial.println("PAUSED - Tilt backward to resume");
  }

  if (isUnpauseGesture && !wasUnpauseGesture && paused) {
    // Unpause on backward tilt
    paused = false;
    if (Serial) Serial.println("RESUMED");
  }

  wasPauseGesture = isPauseGesture;
  wasUnpauseGesture = isUnpauseGesture;

  if (gameOver) {
    drawGame();

    // Auto-restart after 2 seconds
    if (currentTime - gameOverTime > 2000) {
      resetGame();
      paused = false;
      if (Serial) Serial.println("New Game Started!");
    }
    delay(50);
    return;
  }

  if (paused) {
    // Just draw while paused, don't update game state
    drawGame();
    delay(50);
    return;
  }

  // Update player position
  updatePlayer();

  // Update meteors at set speed
  if (currentTime - lastMeteorMove >= meteorSpeed) {
    updateMeteors();
    lastMeteorMove = currentTime;
  }

  // Draw everything
  drawGame();

  delay(10);
}
