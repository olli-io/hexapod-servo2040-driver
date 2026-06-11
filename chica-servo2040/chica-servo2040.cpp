/**
 * Copyright (c) 2023 Eddie Carrera
 * MIT License
 */

#include "main.h"
using namespace plasma;
using namespace servo;

/////////////* Global Variables */////////////
/* Create an array of servo pointers */
const int START_PIN = servo2040::SERVO_1;
const int END_PIN = servo2040::SERVO_18;
const int NUM_SERVOS = (END_PIN - START_PIN) + 1;
ServoCluster servos = ServoCluster(pio0, 0, START_PIN, NUM_SERVOS);

/* Set up the shared analog inputs */
Analog sen_adc = Analog(servo2040::SHARED_ADC);
Analog vol_adc = Analog(servo2040::SHARED_ADC, servo2040::VOLTAGE_GAIN);
Analog cur_adc = Analog(servo2040::SHARED_ADC, servo2040::CURRENT_GAIN,
						servo2040::SHUNT_RESISTOR, servo2040::CURRENT_OFFSET);

/* Set up the analog multiplexer, including the pin for controlling pull-up/pull-down */
AnalogMux mux = AnalogMux(servo2040::ADC_ADDR_0, servo2040::ADC_ADDR_1, servo2040::ADC_ADDR_2,
						  PIN_UNUSED, servo2040::SHARED_ADC);

/* Create the LED bar, using PIO 1 and State Machine 0 */
WS2812 led_bar(servo2040::NUM_LEDS, pio1, 0, servo2040::LED_DATA);

uint servoEnabled = false;

/* Over-current trip state. While latched, SET RELAY 1 is refused; SET RELAY 0
 * clears the latch (an explicit disable always succeeds and re-arms). One
 * dwell timer per tier — 0 means "currently below this tier's threshold". */
static bool     overcurrent_tripped     = false;
static uint64_t overcurrent_since_us[OVERCURRENT_TIER_COUNT] = {0};
static uint64_t last_current_sample_us  = 0;

int main()
{
	/*******************************************************************************
	 * Initializations
	 ******************************************************************************/
	/* Initialize the servo cluster */
	servos.init();

	/* Initialize analog inputs with pull downs */
	for (auto i = 0u; i < servo2040::NUM_SENSORS; i++)
	{
		mux.configure_pulls(servo2040::SENSOR_1_ADDR + i, false, true);
	}

	/* Initialize A0,A1,A2 */
	gpio_init_mask(A0_GPIO_MASK | A1_GPIO_MASK | A3_GPIO_MASK);
	gpio_set_dir_masked(A0_GPIO_MASK | A1_GPIO_MASK | A3_GPIO_MASK,
						GPIO_OUTPUT_MASK); // Set output
	gpio_put_masked(A0_GPIO_MASK | A1_GPIO_MASK | A3_GPIO_MASK,
					GPIO_LOW_MASK); // Set LOW

	stdio_init_all();
	led_bar.start();
#ifdef HOST_LINK_USB
	/* Wait for VCP/CDC connection */
	while (!stdio_usb_connected()){pendingVCP_ledSequence();}
#endif
	/* UART has no connection state to wait on, so drop straight to the
	 * connected indicator. */
	connectedVCP_ledSequence();

	/*******************************************************************************
	 * Application
	 ******************************************************************************/
	while (1)
	{
		/* Monitor and parse serial data */
		parse_and_command_task();

		/* Background safety: latch a fault if the rail draws too much for
		 * too long, drop the relay, and disable the servo cluster. */
		overcurrent_check();

	} // while(1)
}

/*******************************************************************************
 * Function Definitions
 ******************************************************************************/
/*******************************************************************************
 * Core Functions
 ******************************************************************************/
enum read_result { READ_OK, READ_TIMEOUT, READ_RESYNC };

// One-byte pushback slot so a stray MSB-set byte mid-frame is preserved as
// the next frame's command byte rather than dropped on the floor.
static int pending_byte = -1;

static int next_raw_byte(uint32_t timeout_us)
{
	if (pending_byte >= 0)
	{
		int b = pending_byte;
		pending_byte = -1;
		return b;
	}
	return getchar_timeout_us(timeout_us);
}

// Header (startIdx, count) and SET payload bytes MUST have MSB clear.
// A MSB-set byte is treated as the start of the next frame.
static read_result read_inframe_byte(int &out)
{
	int c = next_raw_byte(FRAME_BYTE_TIMEOUT_US);
	if (c == PICO_ERROR_TIMEOUT) return READ_TIMEOUT;
	if (c & 0x80) { pending_byte = c; return READ_RESYNC; }
	out = c;
	return READ_OK;
}

static bool validate_header(const cmdPkt &p, bool is_set)
{
	if (p.count == 0)                       return false;
	if (p.count > MAX_COUNT_VALUE)          return false;
	if (p.startIdx >= cmdPin_num)           return false;
	if (p.startIdx + p.count > cmdPin_num)  return false;
	if (is_set)
	{
		// TS1..VOLT (18..25) are read-only; reject any SET that overlaps.
		uint end = p.startIdx + p.count; // exclusive
		bool disjoint = (end <= TS1) || (p.startIdx > VOLT);
		if (!disjoint) return false;
	}
	return true;
}

static void apply_set(cmdPkt &p)
{
	for (uint idx = 0; idx < p.count; idx++, p.startIdx++)
	{
		if (p.startIdx <= SERVO18)
		{
			servos.pulse(cmdPin_to_hardwarePin((cmdPins)p.startIdx),
						 p.valueBuff[idx], servoEnabled);
		}
		else if (p.startIdx >= RELAY)
		{
			bool enableState = p.valueBuff[idx] ? true : false;
			if (p.startIdx == RELAY)
			{
				// Explicit disable always wins and clears any latched trip.
				if (!enableState && overcurrent_tripped)
				{
					overcurrent_tripped = false;
					connectedVCP_ledSequence();
				}
				// Refuse re-enable while the trip is latched; leave A0 LOW.
				if (overcurrent_tripped) continue;
				servoEnabled = enableState;
				if (enableState) servos.enable_all();
				else             servos.disable_all();
			}
			gpio_put(cmdPin_to_hardwarePin((cmdPins)p.startIdx), enableState);
		}
	}
}

static uint sample_pin(uint startIdx)
{
	if (startIdx <= SERVO18)
	{
		return servos.pulse(cmdPin_to_hardwarePin((cmdPins)startIdx));
	}
	if (startIdx <= TS6)
	{
		float v = read_analogPin(cmdPin_to_hardwarePin((cmdPins)startIdx));
		return (uint)round(v * b1024_3_3V_RATIO);
	}
	if (startIdx == CURR)
	{
		return (uint)round(read_current() / CURR_LSb) + 512;
	}
	if (startIdx == VOLT)
	{
		return (uint)round(read_voltage() * b1024_3_3V_RATIO);
	}
	return 0; // unreachable post-validation
}

static void emit_byte(uint8_t b, uint8_t &chk)
{
	chk ^= b;
	putchar_raw(b);
}

static void emit_get_response(cmdPkt &p)
{
	uint8_t chk = 0;
	emit_byte(GET_CMD, chk);
	emit_byte((uint8_t)p.startIdx, chk);
	emit_byte((uint8_t)p.count, chk);

	for (uint i = 0; i < p.count; i++, p.startIdx++)
	{
		uint v = sample_pin(p.startIdx);
		emit_byte(v & 0x7F, chk);
		emit_byte((v >> 7) & 0x7F, chk);
	}
	// Trailing sync/checksum byte: MSB forced set so the host always sees
	// an unambiguous end-of-response marker.
	putchar_raw(chk | 0x80);
}

void parse_and_command_task(void)
{
	for (;;)
	{
		int c = next_raw_byte(IDLE_POLL_TIMEOUT_US);
		if (c == PICO_ERROR_TIMEOUT) return;
		if ((c & 0x80) == 0)              continue; // stray non-command byte
		if (c != SET_CMD && c != GET_CMD) continue; // unknown command

		cmdPkt pkt;
		pkt.cmd = (c == SET_CMD) ? set : get;

		int b;
		if (read_inframe_byte(b) != READ_OK) continue;
		pkt.startIdx = (uint)b;
		if (read_inframe_byte(b) != READ_OK) continue;
		pkt.count = (uint)b;

		if (!validate_header(pkt, pkt.cmd == set)) continue;

		if (pkt.cmd == set)
		{
			// Drain the entire payload into valueBuff BEFORE issuing any
			// side effects, so a mid-frame drop never produces a
			// half-applied SET.
			bool ok = true;
			for (uint idx = 0; idx < pkt.count; idx++)
			{
				int lo, hi;
				if (read_inframe_byte(lo) != READ_OK) { ok = false; break; }
				if (read_inframe_byte(hi) != READ_OK) { ok = false; break; }
				pkt.valueBuff[idx] = (uint)lo | ((uint)hi << 7);
			}
			if (!ok) continue;
			apply_set(pkt);
		}
		else
		{
			emit_get_response(pkt);
		}
	}
}
/*******************************************************************************
 ******************************************************************************/

/*******************************************************************************
 * VCP/Parsing Support Functions
 ******************************************************************************/
uint cmdPin_to_hardwarePin(cmdPins cmdPin)
{
	return RP_hardwarePins_table[cmdPin];
}
/*******************************************************************************
 ******************************************************************************/
void vcp_transmit(uint *txbuff, uint size)
{
	for (uint byte = 0; byte < size; byte++)
	{
		putchar_raw(txbuff[byte]);
	}
}

/*******************************************************************************
 * LED Support Functions
 ******************************************************************************/
void pendingVCP_ledSequence(void)
{
	static float offset = 0.0;
	const uint updates = 50;

	offset += 0.005;

	// Update all the LEDs
	for (auto i = 0u; i < servo2040::NUM_LEDS; i++)
	{
		float hue = (float)i / (float)servo2040::NUM_LEDS;
		led_bar.set_hsv(i, hue + offset, 1.0f, BRIGHTNESS);
	}

	sleep_ms(1000 / updates);
}

void connectedVCP_ledSequence(void)
{
	for (auto i = 0u; i < servo2040::NUM_LEDS; i++)
	{
		led_bar.set_hsv(i, 0.333f, 1.0f, BRIGHTNESS);
	}
}

void fault_ledSequence(void)
{
	for (auto i = 0u; i < servo2040::NUM_LEDS; i++)
	{
		led_bar.set_hsv(i, 0.0f, 1.0f, BRIGHTNESS);
	}
}

/*******************************************************************************
 * Safety Functions
 ******************************************************************************/
void overcurrent_check(void)
{
	if (overcurrent_tripped) return;
	if (!servoEnabled)
	{
		// Cleared every time the cluster is disabled so that the next enable
		// starts each tier's debounce window from zero (avoids inrush
		// carrying over).
		for (size_t k = 0; k < OVERCURRENT_TIER_COUNT; k++)
			overcurrent_since_us[k] = 0;
		return;
	}

	uint64_t now = time_us_64();
	if (now - last_current_sample_us < OVERCURRENT_SAMPLE_US) return;
	last_current_sample_us = now;

	float i = read_current();
	for (size_t k = 0; k < OVERCURRENT_TIER_COUNT; k++)
	{
		const OvercurrentTier& tier = OVERCURRENT_TIERS[k];
		if (i >= tier.threshold_A)
		{
			if (overcurrent_since_us[k] == 0)
				overcurrent_since_us[k] = now;

			// debounce_us == 0 trips on the same sample (instant cutoff).
			if (now - overcurrent_since_us[k] >= tier.debounce_us)
			{
				overcurrent_tripped  = true;
				servoEnabled         = false;
				servos.disable_all();
				gpio_put(A0_GPIO_PIN, 0);
				fault_ledSequence();
				return;
			}
		}
		else
		{
			overcurrent_since_us[k] = 0;
		}
	}
}

/*******************************************************************************
 * Sensing Support Functions
 ******************************************************************************/
float read_current(void)
{
	mux.select(servo2040::CURRENT_SENSE_ADDR);
	return (cur_adc.read_current());
}
/*******************************************************************************
 ******************************************************************************/
float read_voltage(void)
{
	mux.select(servo2040::VOLTAGE_SENSE_ADDR);
	return (vol_adc.read_voltage());
}
/*******************************************************************************
 ******************************************************************************/
float read_analogPin(uint sensorAddress)
{
	mux.select(sensorAddress);
	return (sen_adc.read_voltage());
}