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

/* Over-current trip state. While latched, servo writes are dropped; either
 * relay command clears the latch and re-arms — SET RELAY 1 to recover directly
 * (re-powers a limp rail), SET RELAY 0 to disable. One dwell timer per tier —
 * 0 means "currently below this tier's threshold". */
static bool     overcurrent_tripped     = false;
static uint64_t overcurrent_since_us[OVERCURRENT_TIER_COUNT] = {0};
static uint64_t last_current_sample_us  = 0;
// Running accumulator for the OVERCURRENT_AVG_SAMPLES-wide averaging window. The
// tier logic runs on the completed average (20 Hz), not each raw 200 Hz sample,
// so servo inrush pulses no longer nuisance-trip. Reset on disable and enable.
static float    overcurrent_sample_sum   = 0.0f;
static uint     overcurrent_sample_count = 0;
// Captured at the latching sample so the host can read why we tripped even
// though live current reads ~0 once the relay is open. Surfaced via STATUS.
static uint     overcurrent_trip_tier    = 0;
static float    overcurrent_trip_current = 0.0f;

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

	/* Initialize the primary relay line (A0) as an output held low. */
	gpio_init_mask(A0_GPIO_MASK);
	gpio_set_dir_masked(A0_GPIO_MASK, GPIO_OUTPUT_MASK); // Set output
	gpio_put_masked(A0_GPIO_MASK, GPIO_LOW_MASK); // Set LOW

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
		// Read-only indices are non-contiguous: TS1..VOLT (18..25) and STATUS
		// (27), with the writable RELAY (26) in between. Reject a SET that
		// overlaps either region.
		uint end = p.startIdx + p.count; // exclusive
		bool hits_ts_volt = !((end <= TS1) || (p.startIdx > VOLT)); // 18..25
		bool hits_status  = (p.startIdx <= STATUS) && (end > STATUS); // 27
		if (hits_ts_volt || hits_status) return false;
	}
	return true;
}

// Reset the over-current debounce timers and averaging window so the next
// enabled window starts from zero — a trip's partial window or a tier's dwell
// must never carry across a power cycle. Called both when the cluster is
// disabled and when it is (re-)enabled, so direct SET RELAY 1 recovery (which
// clears the latch and re-powers in one step, never passing through the
// disabled reset in overcurrent_check) still starts clean.
static void overcurrent_reset_window(void)
{
	for (size_t k = 0; k < OVERCURRENT_TIER_COUNT; k++)
		overcurrent_since_us[k] = 0;
	overcurrent_sample_sum   = 0.0f;
	overcurrent_sample_count = 0;
}

static void apply_set(cmdPkt &p)
{
	for (uint idx = 0; idx < p.count; idx++, p.startIdx++)
	{
		if (p.startIdx <= SERVO18)
		{
			// Only drive when the rail is live. servoEnabled is false while the
			// relay is off AND whenever a trip is latched, so this single guard
			// both ignores relay-off writes (the host energizes servos by
			// SET-ing them AFTER SET RELAY 1, in any order/batch) and keeps the
			// stored pulse (returned by GET) frozen during a trip.
			if (!servoEnabled) continue;
			// Drive only this channel to the host's exact pulse. load_pwm()
			// rebuilds every channel from its stored level, so servos that have
			// not been commanded yet (level 0) stay limp — the host alone
			// determines the energize order.
			servos.pulse(cmdPin_to_hardwarePin((cmdPins)p.startIdx),
						 p.valueBuff[idx], true);
		}
		else if (p.startIdx == RELAY)
		{
			// RELAY is the only writable index >= RELAY; STATUS (27) is
			// read-only and rejected by validate_header, so it never reaches
			// here. Matching exactly (not >=) keeps any future read-only high
			// index from actuating a GPIO if validation ever regresses.
			bool enableState = p.valueBuff[idx] ? true : false;
			// Either relay command clears a latched trip. An enable is safe to
			// clear on because it now re-powers a LIMP rail (drives nothing), so
			// it can never resume the fault pose: recovery is simply SET RELAY 1
			// then re-drive servos. SET RELAY 0 still clears too, for a plain
			// disable.
			if (overcurrent_tripped)
			{
				overcurrent_tripped      = false;
				overcurrent_trip_tier    = 0;
				overcurrent_trip_current = 0.0f;
				connectedVCP_ledSequence();
			}
			if (enableState)
			{
				// Power the rail with every servo limp: energize nothing here.
				// The host drives servos afterward via SET (any order/batch), so
				// it alone determines the energize order and can stagger inrush
				// by pacing. disable_all() guarantees a zero-level buffer BEFORE
				// power is applied, so no un-commanded servo is ever driven and
				// there is no midpoint synthesis. Startup and over-current
				// recovery behave identically here.
				servoEnabled = true;
				// Start the over-current window clean: direct SET RELAY 1 recovery
				// never passes through the disabled reset in overcurrent_check, so
				// a trip's stale dwell must be cleared here or the next window would
				// trip instantly.
				overcurrent_reset_window();
				servos.disable_all();
				gpio_put(cmdPin_to_hardwarePin((cmdPins)p.startIdx), true);
			}
			else
			{
				servoEnabled = false;
				servos.disable_all();
				gpio_put(cmdPin_to_hardwarePin((cmdPins)p.startIdx), false);
			}
		}
	}
}

// Encode a battery telemetry reading (volts or amps) into an unsigned wire
// count of fixed centi-units, clamped to the 14-bit range. read_voltage()/
// read_current() are already non-negative, so only the upper clamp bites (a
// fault-current spike saturates rather than wrapping).
static uint telemetry_counts(float value)
{
	long c = std::lround(value * TELEMETRY_COUNTS_PER_UNIT);
	if (c < 0) c = 0;
	if (c > (long)TELEMETRY_COUNT_MAX) c = TELEMETRY_COUNT_MAX;
	return (uint)c;
}

// Pack the latched over-current state into the 14-bit STATUS word. Reads only
// static RAM — no mux/ADC/GPIO — so serving it from the GET path cannot perturb
// an in-flight CURR/VOLT sample in the same frame. Returns 0 when not tripped.
static uint overcurrent_status_word(void)
{
	if (!overcurrent_tripped) return 0;
	uint w = STATUS_TRIPPED_MASK;
	w |= (overcurrent_trip_tier << STATUS_TIER_SHIFT) & STATUS_TIER_MASK;
	long ci = std::lround(overcurrent_trip_current * STATUS_CURRENT_COUNTS_PER_A);
	if (ci < 0)                            ci = 0;
	if (ci > (long)STATUS_CURRENT_COUNT_MAX) ci = STATUS_CURRENT_COUNT_MAX;
	w |= ((uint)ci << STATUS_CURRENT_SHIFT) & STATUS_CURRENT_MASK;
	return w;
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
		return (uint)std::round(v * b1024_3_3V_RATIO);
	}
	if (startIdx == CURR)
	{
		return telemetry_counts(read_current());
	}
	if (startIdx == VOLT)
	{
		return telemetry_counts(read_voltage());
	}
	if (startIdx == STATUS)
	{
		return overcurrent_status_word();
	}
	return 0; // RELAY (defined 0) or unreachable post-validation
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
		// Cleared every time the cluster is disabled so the next enable starts
		// each tier's debounce window from zero (avoids inrush carrying over).
		// The enable path resets the same state, so direct SET RELAY 1 recovery
		// starts clean too.
		overcurrent_reset_window();
		return;
	}

	uint64_t now = time_us_64();
	if (now - last_current_sample_us < OVERCURRENT_SAMPLE_US) return;
	last_current_sample_us = now;

	// Accumulate raw samples at OVERCURRENT_SAMPLE_US (200 Hz); only evaluate the
	// tiers once a full window is collected, against its average (20 Hz).
	overcurrent_sample_sum += read_current();
	if (++overcurrent_sample_count < OVERCURRENT_AVG_SAMPLES) return;
	float i = overcurrent_sample_sum / overcurrent_sample_count;
	overcurrent_sample_sum   = 0.0f;
	overcurrent_sample_count = 0;

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
				overcurrent_tripped      = true;
				overcurrent_trip_tier    = (uint)k;
				overcurrent_trip_current = i;
				servoEnabled             = false;
				servos.disable_all();
				// Never resume the pre-trip positions that drew the fault:
				// recovery is SET RELAY 0 -> SET RELAY 1 (limp rail) -> the
				// host re-drives servos with fresh SETs.
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