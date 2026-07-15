#pragma once

#include <stdio.h>
#include <cmath>
#include <cstring>
#include "pico/stdlib.h"
#include "servo2040.hpp"
#include "analogmux.hpp"
#include "analog.hpp"
#include "button.hpp"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* Commands */
#define SET_CMD	0xD3 // 0x53 & 0x80
#define GET_CMD	0xC7 // 0x47 & 0x80

/* Relay control pins. Numbers come from hexapod_config.cmake as compile
 * definitions; the fallbacks keep this header self-contained if built without
 * that config. A0 is the primary relay line (host index RELAY). GP27/GP28 are
 * reserved as alternative relay lines for future use — held low at init and not
 * yet exposed on the host protocol. */
#ifndef A0_GPIO_PIN
#define A0_GPIO_PIN				26
#endif
#ifndef RELAY_ALT1_GPIO_PIN
#define RELAY_ALT1_GPIO_PIN		27
#endif
#ifndef RELAY_ALT2_GPIO_PIN
#define RELAY_ALT2_GPIO_PIN		28
#endif
#define A0_GPIO_MASK			(1<<A0_GPIO_PIN)
#define RELAY_ALT1_GPIO_MASK	(1<<RELAY_ALT1_GPIO_PIN)
#define RELAY_ALT2_GPIO_MASK	(1<<RELAY_ALT2_GPIO_PIN)
#define GPIO_OUTPUT_MASK	0xFFFFFFFF
#define GPIO_INPUT_MASK		0x00
#define GPIO_HIGH_MASK		0xFFFFFFFF
#define GPIO_LOW_MASK		0x00

/* Miscellaneous */
#define MAX_COUNT_VALUE		127

/*******************************************************************************
 * Constants
 ******************************************************************************/
/* Timing */
// Per-byte timeout once a command byte has been seen. USB-CDC delivers in
// ~1 ms bulk frames; 5 ms tolerates one missed/late frame plus host
// scheduling jitter while still abandoning a dead transfer well inside one
// 20 ms servo period.
constexpr uint32_t FRAME_BYTE_TIMEOUT_US	= 5000;

// Non-blocking poll for the first byte of a new frame; matches the
// tight-loop semantics in main().
constexpr uint32_t IDLE_POLL_TIMEOUT_US		= 0;

/* LED */
constexpr float BRIGHTNESS		= 0.3f;		// Normalized

/* Ratios */
constexpr float b1024_3_3V_RATIO	= 310.3f;	// touch-sensor GET code (raw ADC)

/* Battery telemetry wire units. VOLT/CURR GET replies carry fixed-point
 * centi-units: wire count = round(engineering_value * 100), i.e. 0.01 V or
 * 0.01 A per count. Protocol-defined — the host multiplies by the reciprocal
 * (0.01) and carries no other scaling. Unsigned, clamped to the 14-bit max so
 * a fault-current spike saturates rather than wrapping. Must match the host
 * constant (kVoltsPerCount/kAmpsPerCount in servo2040_protocol.hpp). */
constexpr float TELEMETRY_COUNTS_PER_UNIT	= 100.0f;
constexpr uint  TELEMETRY_COUNT_MAX			= 16383;

/* Over-current trip. Tiered inverse-time protection sized for a 10 A
 * continuous rail: higher current shortens the trip delay. Every tier is
 * evaluated each sample and tracks its own dwell timer; the first whose
 * dwell reaches its debounce wins. The top tier (debounce_us == 0) is an
 * instant cutoff for dead-short events. */
struct OvercurrentTier
{
	float    threshold_A;
	uint64_t debounce_us;
};

constexpr OvercurrentTier OVERCURRENT_TIERS[] = {
	{ 15.0f,       0 },	// 1.5x rated — instant cutoff
	{ 12.0f,  200000 },	// 1.2x rated — 200 ms
	{ 11.0f, 1000000 },	// 1.1x rated — 1 s sustained
};
constexpr size_t OVERCURRENT_TIER_COUNT =
	sizeof(OVERCURRENT_TIERS) / sizeof(OVERCURRENT_TIERS[0]);

constexpr uint64_t OVERCURRENT_SAMPLE_US	= 10000;

/*******************************************************************************
 * Enumerations
 ******************************************************************************/
typedef enum {
	SERVO1, SERVO2, SERVO3, SERVO4, SERVO5, SERVO6, 
	SERVO7, SERVO8, SERVO9, SERVO10, SERVO11, SERVO12, 
	SERVO13, SERVO14, SERVO15, SERVO16, SERVO17, SERVO18,
	TS1, TS2, TS3, TS4, TS5, TS6, 
	CURR, VOLT, RELAY, cmdPin_num
} cmdPins;

typedef enum {
	set,
	get
} hexapodCmds;

/*******************************************************************************
 * Structures
 ******************************************************************************/
typedef struct {
	hexapodCmds cmd;
	uint startIdx;
	uint count;
	uint valueBuff[MAX_COUNT_VALUE];
} cmdPkt;

/*******************************************************************************
 * Lookup Tables
 ******************************************************************************/
constexpr uint RP_hardwarePins_table[] = 
{
	SERVO1,		SERVO2,		SERVO3,			
	SERVO4,		SERVO5,		SERVO6,			
	SERVO7,		SERVO8,		SERVO9, 		
	SERVO10,	SERVO11,	SERVO12,		
	SERVO13,	SERVO14,	SERVO15, 		
	SERVO16,	SERVO17,	SERVO18,		
	servo::servo2040::SENSOR_1_ADDR,		// TS_L1
	servo::servo2040::SENSOR_2_ADDR,		// TS_L2
	servo::servo2040::SENSOR_3_ADDR,		// TS_L3
	servo::servo2040::SENSOR_4_ADDR,		// TS_R1
	servo::servo2040::SENSOR_5_ADDR,		// TS_R2
	servo::servo2040::SENSOR_6_ADDR,		// TS_R3
	servo::servo2040::CURRENT_SENSE_ADDR,	// CURR
	servo::servo2040::VOLTAGE_SENSE_ADDR,	// VOLT
	A0_GPIO_PIN								// RELAY
};

/*******************************************************************************
 * Function Forward Declarations
 ******************************************************************************/

/*******************************************************************************
 * Core Functions
 ******************************************************************************/
void parse_and_command_task(
void
);

void overcurrent_check(
void
);

/*******************************************************************************
 * VCP/Parsing Support Functions
 ******************************************************************************/
uint cmdPin_to_hardwarePin(
cmdPins cmdPin
);

void vcp_transmit(
uint *txbuff,
uint size
);

/*******************************************************************************
 * LED Support Functions
 ******************************************************************************/
void pendingVCP_ledSequence(
void
);

void connectedVCP_ledSequence(
void
);

void fault_ledSequence(
void
);

/*******************************************************************************
 * Sensing Support Functions
 ******************************************************************************/
float read_current(
void
);

float read_voltage(
void
);

float read_analogPin(
uint sensorAddress
);