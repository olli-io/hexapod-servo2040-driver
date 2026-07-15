/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "servo2040.hpp"
#include <cmath>
#include <stdio.h>

using namespace servo;

/* Input Defines */
#define SPECIAL_BYTE1 0x1b
#define SPECIAL_BYTE2 0x5b
#define KEY_UP 0x41
#define KEY_RIGHT 0x43
#define KEY_DOWN 0x42
#define KEY_LEFT 0x44

/* PWM Defines */
#define MIN_PULSE_VALUE 500
#define MAX_PULSE_VALUE 2500

#define RELAY_GPIO_PIN 26

/* Create an array of servo pointers */
const int START_PIN = servo2040::SERVO_1; // Can be changed to only calibrate a
const int END_PIN = servo2040::SERVO_18;  // a group of servos
const int NUM_SERVOS = (END_PIN - START_PIN) + 1;
ServoCluster servos = ServoCluster(pio0, 0, START_PIN, NUM_SERVOS);

/* PWM value storage */
int neg45_PWMvalues[NUM_SERVOS];
int pos45_PWMvalues[NUM_SERVOS];
int center_PWMvalues[NUM_SERVOS];

/* Helper variables */
char inputByte1;
char inputByte2;
char inputByte3;
uint currPWM;
bool calibState = 0; // 0:-45, 1:+45

int main() {
  stdio_init_all();
  gpio_init(RELAY_GPIO_PIN);
  gpio_set_dir(RELAY_GPIO_PIN, GPIO_OUT);
  gpio_put(RELAY_GPIO_PIN, true);

  /* Initialize the servo cluster */
  servos.init();

  while (!stdio_usb_connected())
    ; // Wait for VCP/CDC connection;

  sleep_ms(2000);

  printf("\r\n Script by EddieArchuleta, special thanks to MYP for starting "
         "the hexapod community!\r\n\r\n"
         " Welcome to the rp2040 calibration script!\r\n\r\n"

         " This script will record and return the PWM values at \r\n"
         " -45 and +45 degrees for each servo. It will also return \r\n"
         " the average PWM of the values at the end of the script for \r\n"
         " calibration purposes.\r\n\r\n");

  printf(" This script assumes all 18 servo channels are powered and "
         "attached.\r\n\r\n");

  printf(
      " WARNING:\r\n"
      " If you want to run servos with a higher voltage than 5V, you\r\n"
      " need to cut the 'Separate USB and Ext. Power' trace on the back of\r\n"
      " the board to prevent the RP2040 being damaged\r\n\r\n");

  printf(" Press any key to continue...\r\n\r\n");
  getchar();

  printf(" Centering all servos to uncalibrated center (1500us)\r\n");
  /* Enable all servos (centers all servos) */
  for (auto currServo = START_PIN; currServo < NUM_SERVOS; currServo++) {
    servos.enable(currServo);
    sleep_ms(250); // Give each servo time to center to avoid
  } // drawing too much current at once
  printf(" Done. Please follow instructions to calibrate the servos.");
  printf(" Counter-clockwise (left) is the negative direction, Clockwise "
         "(right) is positive.\r\n\r\n ");
  printf("\r\n\r\n");
  printf(" **Keybinds**********************************************************"
         "**\r\n\r\n");
  printf(" ↑ :\t PWM +20us\r\n"
         " ↓ :\t PWM -20us\r\n"
         " → :\t PWM +5us\r\n"
         " ← :\t PWM -5us\r\n"
         " s :\t save value\r\n"
         " b :\t move back to previous pin\r\n");
  printf(" ********************************************************************"
         "**\r\n\r\n");

  for (auto currServo = START_PIN; currServo < NUM_SERVOS;) {
    if (!calibState) {
      printf(" Pin %d: move servo to -45 deg (CCW) position and press 's' to "
             "save.\r\n",
             currServo + 1);
    } else {
      printf(" Pin %d: move servo to +45 degree (CW) position and press 's' to "
             "save.\r\n",
             currServo + 1);
    }

    while (inputByte1 != 's') {
      inputByte1 = 0;
      inputByte2 = 0;
      inputByte3 = 0;
      currPWM = servos.pulse(currServo);
      printf("    PWM: %d   \r", currPWM);

      /* special KEY_ handling
         3 bytes returned: 0x1b, 0x5b, 0x__ */
      inputByte1 = getchar();
      if (inputByte1 == SPECIAL_BYTE1) {
        inputByte2 = getchar(); // Throw away SPECIAL_BYTE2
        inputByte3 = getchar(); // Arrow ID
        switch (inputByte3) {
        case KEY_UP: {
          currPWM += 20;
          break;
        }
        case KEY_RIGHT: {
          currPWM += 5;
          break;
        }
        case KEY_DOWN: {
          currPWM -= 20;
          break;
        }
        case KEY_LEFT: {
          currPWM -= 5;
          break;
        }
        default:
          break;
        } // switch (serialInput)
      }

      /* Input Hygiene */
      if (currPWM < MIN_PULSE_VALUE) {
        currPWM = MIN_PULSE_VALUE;
      } else if (currPWM > MAX_PULSE_VALUE) {
        currPWM = MAX_PULSE_VALUE;
      }

      servos.pulse(currServo, currPWM);
    } // while (serialInput != ' ')

    /* Exiting keyboard input loop */
    inputByte1 = 0;
    inputByte2 = 0;
    inputByte3 = 0;

    /* Binary state machine */
    if (!calibState) {
      calibState = 1;
      printf("    PWM: %d ✔ saved\r\n", currPWM);
      neg45_PWMvalues[currServo] = currPWM;
    } else {
      calibState = 0;
      printf("    PWM: %d ✔ saved\r\n\r\n", currPWM);
      pos45_PWMvalues[currServo] = currPWM;
      currServo++; // -45/+45 for currServo calibrated at this point, move to
                   // next servo
    }
  } // for (auto currServo = START_PIN; currServo < END_PIN; currServo++)

  /* Take the center PWM value for each servo */
  for (auto currServo = START_PIN; currServo < NUM_SERVOS; currServo++) {
    center_PWMvalues[currServo] = std::round(
        (float(neg45_PWMvalues[currServo] + pos45_PWMvalues[currServo]) / 2));
  }
  printf(" ********************************************************************"
         "**\r\n");
  /* Center all servos with calibrated value */
  printf(" Calibration complete, centering all servos to the calibrated center "
         "value...\r\n\r\n");
  for (auto currServo = START_PIN; currServo < NUM_SERVOS; currServo++) {
    servos.pulse(currServo, center_PWMvalues[currServo]);
    sleep_ms(250); // Give each servo time to center to avoid
  } // drawing too much current at once
  printf(" Done, all servos at calibrated center\r\n"
         " Visually verify that all servos are exactly centered \r\n   "
         " If not, calibration process should be repeated \r\n");
  printf(" ********************************************************************"
         "**\r\n\r\n");

  /* Print results as YAML last, so it's easy to copy */
  printf(" Recorded calibration values:\r\n\r\n");

  printf("calibration_values:\r\n");
  for (auto currServo = START_PIN; currServo < NUM_SERVOS; currServo++) {
    printf("  - { pin: %d, us_at_plus_45: %d, us_at_minus_45: %d }\r\n",
           currServo + 1, pos45_PWMvalues[currServo],
           neg45_PWMvalues[currServo]);
  }
  printf("\r\n");
  return 1;
}
