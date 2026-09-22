/******************************************************************************
 * @file    msp1.h
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   MICS-2714 NO2 sensor and heater driver via NAU7802 ADC and MCP4725 DAC.
 *
 *          NAU7802 channel assignment:
 *            CH1 – differential NO2 sensor output  (OUT+ / OUT-)
 *            CH2 – heater sense, differential       (DRIVE / HEAT)
 *                  → V_CH2 = V_DRIVE − V_HEAT = drop across R14
 *
 *          Heater path:
 *            MCP4725 → buffer U2A → R14 (50 Ω) → R_heater (66 Ω nom.) → GND
 *            V_DRIVE = DAC_code / 4096 × VDD_DAC  (VDD_DAC from "dac_vdd_mv")
 *            I_heat  = V_R14  / R14
 *            V_heat  = V_DRIVE − V_R14
 *            P_heat  = V_heat × I_heat / 1000          (mW)
 *
 *          NO2 front-end (as implemented, see s_avgToRno2Ohm() in msp1.cpp):
 *            Rno2 = V_diff_mV × MSP1_NO2_GAIN_OHM / 1000       (Ω)
 *          The value is published unclamped; ADC clipping is reported
 *          separately through msp1SensorData_t::saturated. R0
 *          (msp1Config_t::r0No2_ohm) is the baseline of the transmitted
 *          Rno2/R0 ratio — see tMsp1_peekRno2Ratio().
 *
 * @version 0.3
 * @date    2026-04-08
 * @copyright Copyright (c) 2026
 *****************************************************************************/

#ifndef MSP1_H
#define MSP1_H

#include <stdint.h>
#include <stdbool.h>
#include "shared_values.h"

/* ---- I2C addresses ---- */

/** NAU7802 fixed I2C address */
#define MSP1_NAU7802_I2C_ADDR        (0x2AU)

/** MCP4725 I2C address scan range — 0x60..0x67 (A2:A1 factory-programmed,
 *  A0 pin).  The actual address is discovered at init by probing the range. */
#define MSP1_MCP4725_I2C_ADDR_MIN    (0x60U)
#define MSP1_MCP4725_I2C_ADDR_MAX    (0x67U)

/** MCP4725 full-scale 12-bit count */
#define MSP1_DAC_MAX_VALUE           (4095U)

/* ---- NAU7802 channel assignment ---- */

/**
 * @brief NAU7802 ADC channels identified by their function.
 *
 * The values match the NAU7802 0-based channel index written to the
 * CTRL2.CHS register field (channel 1 → 0, channel 2 → 1).
 */
typedef enum __MSP1_ADC_CHANNEL__
{
  /** Differential channel reading the MICS-2714 sensor output (OUT+/OUT-). */
  MSP1_ADC_CH_SENSOR = 0,
  /** Channel dedicated to heater management (DRIVE-HEAT drop across R14).  */
  MSP1_ADC_CH_HEATER = 1,
} msp1AdcChannel_t;

/* ---- supply voltages ---- */

/** Default index into the NAU7802 LDO/AVDD table (MSP1_LDO_TABLE in msp1.cpp):
 *  index 5 = 3.9 V. Compile-time fallback for msp1Config_t::ldoIndex when
 *  "ldo_index" is absent from config.
 *
 *  The NAU7802 runs off its internal LDO, so the step programmed with setLDO()
 *  IS AVDD: that one table row drives both the setLDO() call and the mV used in
 *  every raw→mV conversion.
 *
 *  AVDD sets the ADC input range, which is per channel — see MSP1_ADC_PROFILE
 *  in msp1.cpp, applied on every channel switch. At the default 3.9 V:
 *    CH0 (sensor): front end selected by MSP1_CH0_PGA in msp1.cpp — PGA in
 *                  the path at GAIN=1 (full scale ±AVDD/2, the default), or
 *                  PGA bypassed + extended common mode near REFN (±AVDD).
 *    CH1 (heater): PGA in the path, ADC_VCM normal, for full CMRR on the
 *                  high-side differential measurement. Full scale = ±AVDD/2
 *                  → ±1950 mV (V_R14 is far below this).
 *
 *  ADC saturation: V_diff max = 3200 mV (at Rno2=66kΩ) fits inside CH0's
 *  ±3900 mV full scale only with MSP1_CH0_PGA=0. With the PGA in the path CH0
 *  stops at ±AVDD/2 (±1950 mV at the default), so the top of the 2 k–66 kΩ
 *  sensor range clips — a bench setting, not a release one. Lowering
 *  ldo_index shrinks every range proportionally. */
#define MSP1_LDO_DEFAULT_INDEX       (5U)

/** "ldo_index" value selecting EXTERNAL AVDD: the internal LDO is switched
 *  off (AVDDS bit cleared, setLDO(NAU7802_EXTERNAL)) and AVDD must be fed
 *  externally on the AVDD pin. In this mode the voltage used by every raw→mV
 *  conversion is msp1Config_t::avddExt_mV ("avdd_ext_mv" in config), since
 *  the chip can no longer tell the firmware what AVDD is. Equals
 *  MSP1_LDO_TABLE_LEN (one past the last real LDO step). */
#define MSP1_LDO_INDEX_EXTERNAL      (8U)

/** Default externally-supplied AVDD (mV) — compile-time default for
 *  msp1Config_t::avddExt_mV when "avdd_ext_mv" is absent from config. Only
 *  used when ldo_index == MSP1_LDO_INDEX_EXTERNAL. 3.3 V board rail. */
#define MSP1_AVDD_EXT_DEFAULT_MV     (3300.0f)

/** Accepted range for msp1Config_t::avddExt_mV (mV) — NAU7802 AVDD operating
 *  range per datasheet. Out-of-range falls back to the default in
 *  tMsp1_setConfig(). */
#define MSP1_AVDD_EXT_MIN_MV         (2400.0f)
#define MSP1_AVDD_EXT_MAX_MV         (5500.0f)

/** Default VDD of the MCP4725 DAC (mV) — compile-time default for
 *  msp1Config_t::dacVdd_mV when "dac_vdd_mv" is absent from config. VDD is
 *  the DAC's reference: the output spans 0 … VDD_DAC.
 *
 *  Unrelated to AVDD, deliberately: AVDD feeds the NAU7802 (internal LDO or
 *  external supply, per ldo_index), while this is the DAC's own supply, which
 *  the firmware cannot program — it is ALWAYS taken from config. */
#define MSP1_DAC_VDD_DEFAULT_MV      (3300.0f)

/** Accepted range for msp1Config_t::dacVdd_mV (mV) — MCP4725 VDD operating
 *  range per datasheet. Out-of-range falls back to the default in
 *  tMsp1_setConfig(). */
#define MSP1_DAC_VDD_MIN_MV          (2700.0f)
#define MSP1_DAC_VDD_MAX_MV          (5500.0f)

/* ---- heater constants ---- */

/** Series current-limiting resistor R14 between DAC buffer and heater (Ω).
 *  NAU7802 CH2 measures the differential voltage across this resistor,
 *  high-side — hence CH1's differential/full-CMRR profile in MSP1_ADC_PROFILE. */
#define MSP1_HEATER_R14_OHM          (50.0f)

/** Maximum allowed heater dissipation (mW), enforced in tMsp1_measureHeater()
 *  on the *theoretical* power (derived from the DAC setpoint and the measured
 *  V_R14 — see msp1HeaterData_t::power_mW). A continuous limiter, not a
 *  latched fault: if exceeded, dacSetpoint is scaled by
 *  sqrt(MSP1_HEATER_MAX_POWER_MW / measured power), since P ∝ DAC-code² for a
 *  resistive heater. Never cut to zero.
 *  ⚠ ABSOLUTE LIMIT — do not raise above 50 mW. */
#define MSP1_HEATER_MAX_POWER_MW     (50.0f)

/** Maximum allowed heater current (mA) — ceiling for
 *  tMsp1_heaterControlStep() setpoints, and for the DAC itself: if the
 *  *measured* current (I = V_R14 / R14, measured high-side across R14)
 *  exceeds this in tMsp1_measureHeater(), dacSetpoint is scaled by
 *  (MSP1_HEATER_MAX_CURRENT_MA / measured current), since I ∝ DAC-code for a
 *  resistive heater. Same principle as the power limiter above; whichever of
 *  the two demands the lower DAC code wins — above R_heat ≈ 66 Ω the power
 *  limiter binds first, this one only bites on a lower R.
 *  Deliberately conservative: the sensor spec allows 29 mA (= 1.45 V across
 *  R14), this is MSP1_HEATER_MAX_POWER_MW at the nominal heater resistance
 *  (66 Ω), I_max = sqrt(0.050 / 66) × 1000 ≈ 27.53 mA.
 *  ⚠ ABSOLUTE LIMIT — do not raise above 29 mA. */
#define MSP1_HEATER_MAX_CURRENT_MA   (27.53f)

/** DAC code ceiling, left at the hardware maximum (4095): the actual
 *  enforcement is the runtime power/current check in tMsp1_measureHeater(),
 *  not a static ceiling. Every DAC write hard-clamps above this value.
 *  For reference, exactly MSP1_HEATER_MAX_POWER_MW at the nominal heater
 *  resistance would be V_DRIVE = 116 × sqrt(0.050/66) = 3.193 V →
 *  DAC = floor(3193.0 / 3300 × 4095) = 3961. */
#define MSP1_DAC_MAX_SAFE            (4095U)

/** Nominal heater power (mW) — target of the open-loop soft-start ramp: the
 *  DAC is ramped from zero to the code that dissipates this much at the
 *  configured R@Tmax (s_rampSeedDAC() in msp1.cpp), then closed-loop
 *  regulation on R_heat takes over. NOT a steady-state setpoint: at regime
 *  the power is whatever holding R_heat at R@Tmax requires. */
#define MSP1_HEATER_NOMINAL_POWER_MW (43.0f)

/** Slew-rate limit on tMsp1_heaterControlStep()'s output: maximum DAC-count
 *  change applied per call, in either direction. Bounds how far a single
 *  noisy or stale reading can move the DAC in one step — the control law
 *  scales dacSetpoint by sqrt(target rise / measured rise) in R_heat, so a
 *  reading taken before the ADC has settled after a DAC change (or taken on
 *  a still-cold heater) could otherwise produce an extreme ratio and jump
 *  straight to an extreme output. Does not limit the initial soft-start ramp
 *  (see MSP1_HEATER_RAMP_STEPS), only ongoing corrections. */
#define MSP1_HEATER_PI_MAX_STEP      (200U)

/** Minimum spacing (ms) between successive tMsp1_heaterControlStep() calls,
 *  enforced with vTaskDelay() at the end of the function — applies to every
 *  call site (continuous servicing in SYS_STATE_WAIT_FOR_TIMEOUT, the
 *  pre-BME680 top-up, and the retry loop in SYS_STATE_READ_SENSORS) so the
 *  control loop never runs faster than this, independent of how often the
 *  caller invokes it. Gives each correction time to actually show up in the
 *  next NAU7802 reading (~100 ms at 10 SPS) before another PI step reacts. */
#define MSP1_HEATER_CONTROL_LOOP_DELAY_MS (300U)

/** Number of geometric open-loop ramp steps used to bring the heater DAC
 *  quickly from 0 toward a new target before handing off to the PI loop.
 *  Each step covers 1/MSP1_HEATER_RAMP_DIVISOR of the *remaining* gap to the
 *  target, so the rise is large and fast at first and decelerates as it
 *  nears the target — never a single instantaneous jump, and never
 *  approached from above. Final convergence and steady-state hold are left
 *  to tMsp1_heaterControlStep()'s PI action. */
#define MSP1_HEATER_RAMP_STEPS        (8U)

/** Divisor applied to the remaining DAC gap at each ramp step (2 = halving
 *  the residual distance to the target on every step). */
#define MSP1_HEATER_RAMP_DIVISOR      (2U)

/** Delay between successive open-loop ramp steps (ms) — compile-time default
 *  for msp1Config_t::rampStepDelayMs when "ramp_step_delay_ms" is absent from
 *  config. Safety does not depend on it (the ramp target is amplitude-clamped
 *  for the cold worst case in s_rampSeedDAC()), so it only trades boot time
 *  for a gentler rise: 500 ms completes the ramp in ~4 s and leaves room for
 *  a measurement between steps; 1000-2000 ms stretches it to 8-16 s. The
 *  ramp blocks the calling task for MSP1_HEATER_RAMP_STEPS × this delay. */
#define MSP1_HEATER_RAMP_DELAY_DEFAULT_MS (500U)

/** Maximum accepted msp1Config_t::rampStepDelayMs (ms): caps the blocking
 *  soft-start at MSP1_HEATER_RAMP_STEPS × 5 s = 40 s. Above this,
 *  tMsp1_setConfig() falls back to the default. */
#define MSP1_HEATER_RAMP_DELAY_MAX_MS     (5000U)

/* ---- NO2 sensor bias-network constants ---- */

/** Feedback network GAIN */
#define MSP1_NO2_GAIN_OHM              (20000.0f)

/**
 * Baseline sensor resistance R0 (Ω). Compile-time default for
 * msp1Config_t::r0No2_ohm — see tMsp1_setConfig().
 */
#define MSP1_NO2_R0_OHM              (2000.0f)

/* ---- runtime-configurable parameters (config_v4.json "msp1_params") ---- */

/** Default target heater operating temperature Tmax (°C) — compile-time
 *  default for msp1Config_t::rheatTempMaxC when "rheat_temp_max_c" is
 *  absent from config. */
#define MSP1_HEATER_TEMP_MAX_DEFAULT_C   (220.0f)

/** Default heater resistance at the Tamb reference (Ω) — compile-time
 *  default for msp1Config_t::rheatTambOhm when "rheat_tamb_ohm" is absent
 *  from config. Same value previously hardcoded as the fixed nominal R_heat. */
#define MSP1_HEATER_TAMB_DEFAULT_OHM     (66.0f)

/** Default heater resistance temperature coefficient Kr (Ω/°C) — compile-time
 *  default for msp1Config_t::krOhmPerC when "kr" is absent from config.
 *  From the MICS-2714 heater characterisation in the sensor spec document. */
#define MSP1_HEATER_KR_DEFAULT_OHM_PER_C (0.043f)

/** Default reference temperature Tamb (°C) at which rheatTambOhm is assumed
 *  rated, i.e. R_heat(Tamb) == rheatTambOhm — compile-time default for
 *  msp1Config_t::rheatTambRefC when "rheat_tamb_ref_c" is absent from config
 *  (or out of the valid range below). Used to project the heater resistance
 *  at rheatTempMaxC — the regulation target, see s_getRHeatEffective() in
 *  msp1.cpp:
 *    R@Tmax = (Tmax − Tamb) × Kr + R@Tamb
 *  ⚠ Assumption: there is no live ambient/board temperature input into this
 *  model, so this is a fixed reference (the temperature at which the sensor
 *  was characterised), not the true instantaneous ambient. Wire in a live
 *  reading (e.g. from BME680) later if that changes. */
#define MSP1_HEATER_TAMB_REF_C           (20.0f)

/** Accepted range for msp1Config_t::rheatTambRefC. Anything outside it — or
 *  at/above rheatTempMaxC, which would make the target rise zero or negative
 *  — is rejected in tMsp1_setConfig() and replaced by
 *  MSP1_HEATER_TAMB_REF_C. */
#define MSP1_HEATER_TAMB_REF_MIN_C       (-40.0f)
#define MSP1_HEATER_TAMB_REF_MAX_C       (85.0f)

/** CH0/CH1 display sliding-window depth. The ring averages are recomputed on
 *  EVERY new CH0 sample (~one wait-loop giro, ~2 s); this only sets how many
 *  samples each average spans — deeper = smoother but slower to track
 *  changes. */
#define MSP1_DISPLAY_AVG_SAMPLES         (5U)

/* ---- data structures ---- */

/**
 * @brief Accumulated sensor readings from NAU7802 CH1 (MICS-2714 output).
 */
typedef struct __MSP1_SENSOR_DATA__
{
  int64_t  rawSum;           /**< Running sum of raw samples, unclamped: the
                                   zero floor sits on the average            */
  uint32_t sampleCount;      /**< Number of samples added to rawSum               */
  float    avgVoltage_mV;    /**< Averaged differential voltage (mV); valid when
                                   dataReady is true                              */
  float    lastRaw_mV;       /**< Last individual raw sample converted to mV      */
  bool     saturated;        /**< true if last sample was at ADC positive full-scale (clipped) */
  bool     dataReady;        /**< true after computeSensorAverage() is called     */
} msp1SensorData_t;

/**
 * @brief Derived NO2 quantities.
 *
 *   Rno2 = vDiff_mV × MSP1_NO2_GAIN_OHM / 1000       (Ω, unclamped)
 */
typedef struct __MSP1_NO2_DATA__
{
  float vDiff_mV;            /**< Averaged differential voltage from CH1 (mV)    */
  float rno2_ohm;            /**< Derived sensor resistance Rno2 (Ω)             */
  bool  dataReady;           /**< true after tMsp1_computeRno2() is called       */
} msp1No2Data_t;

/**
 * @brief Heater state derived from NAU7802 CH2 (DRIVE−HEAT) and the DAC output.
 *
 *   V_R14   = V_CH2                           (drop across R14, measured by CH2)
 *   V_DRIVE = dacSetpoint / 4096 × VDD_DAC    (from DAC setpoint)
 *   I_heat  = V_R14 / R14                     (mA)
 *   V_heat  = V_DRIVE − V_R14                 (mV)
 *   P_heat  = V_heat × I_heat / 1000          (mW)
 *   R_heat  = V_heat / I_heat                 (Ω)
 */
typedef struct __MSP1_HEATER_DATA__
{
  float    vR14_mV;            /**< Voltage across R14 measured by CH2, high-side (mV) */
  float    vHeater_mV;         /**< Voltage across the heater: V_DRIVE − V_R14 (mV)    */
  float    current_mA;         /**< Heater current I = V_R14 / R14 (mA)                */
  float    rHeat_ohm;          /**< Measured heater resistance V_heat / I (Ω) — the
                                     control feedback variable, and the image of the
                                     element temperature                               */
  float    power_mW;           /**< Theoretical dissipated power (mW), for monitoring
                                     only — not used by the control loop               */
  uint16_t dacSetpoint;        /**< DAC count currently applied to the heater         */
} msp1HeaterData_t;

/**
 * @brief Top-level MSP1 module state.
 */
typedef struct __MSP1_DATA__
{
  msp1SensorData_t sensor;       /**< CH1 gas sensor accumulation state          */
  msp1No2Data_t    no2;          /**< Derived NO2 concentration quantities       */
  msp1HeaterData_t heater;       /**< Heater measurement and control state       */
  bool             nau7802Ready; /**< true when NAU7802 is initialised           */
  bool             mcp4725Ready; /**< true when MCP4725 is initialised           */
} msp1Data_t;

/**
 * @brief Runtime-configurable MSP1 parameters, normally loaded once at
 *        startup from config_v4.json's "msp1_params" section (see
 *        sdCard.cpp parseConfig()) via tMsp1_setConfig(). Each field
 *        defaults to its compile-time MSP1_..._DEFAULT_* constant when the
 *        corresponding JSON key is absent — the caller is expected to have
 *        already applied that fallback before calling tMsp1_setConfig().
 */
typedef struct __MSP1_CONFIG__
{
  float r0No2_ohm;     /**< NO2 baseline resistance R0 (Ω)                   */
  float rheatTempMaxC; /**< Target heater operating temperature Tmax (°C)    */
  float rheatTambOhm;  /**< Heater resistance at rheatTambRefC (Ω)           */
  float rheatTambRefC; /**< Temperature at which rheatTambOhm is rated (°C);
                            validated in tMsp1_setConfig(), falls back to
                            MSP1_HEATER_TAMB_REF_C when out of range        */
  float krOhmPerC;     /**< Heater resistance temperature coefficient (Ω/°C) */
  uint8_t ldoIndex;    /**< NAU7802 LDO/AVDD selection: index into MSP1_LDO_TABLE
                            (msp1.cpp), or MSP1_LDO_INDEX_EXTERNAL to switch the
                            internal LDO off (AVDD supplied externally);
                            out-of-range falls back to MSP1_LDO_DEFAULT_INDEX. */
  float avddExt_mV;    /**< Externally-supplied AVDD (mV); the ADC reference used
                            for raw→mV conversion ONLY when ldoIndex ==
                            MSP1_LDO_INDEX_EXTERNAL — with the internal LDO on,
                            AVDD comes from MSP1_LDO_TABLE instead.             */
  float dacVdd_mV;     /**< MCP4725 DAC supply/reference VDD (mV) — ALWAYS taken
                            from config, independent of ldoIndex/AVDD.          */
  uint16_t rampStepDelayMs; /**< Delay between soft-start ramp steps (ms);
                            validated in tMsp1_setConfig(), falls back to
                            MSP1_HEATER_RAMP_DELAY_DEFAULT_MS above
                            MSP1_HEATER_RAMP_DELAY_MAX_MS.                      */
} msp1Config_t;

/* ---- public API ---- */

/*****************************************************************************
 * @brief Initialise both NAU7802 (GAIN=1, 10 SPS) and MCP4725 (output=0)
 *        over I2C.  Must be called before any other function in this module.
 *
 * @return mspStatus_t STATUS_OK if both ready, STATUS_ERR otherwise.
 *****************************************************************************/
mspStatus_t tMsp1_init(void);

/*****************************************************************************
 * @brief Override the runtime-configurable MSP1 parameters (r0No2_ohm,
 *        rheatTempMaxC, rheatTambOhm, rheatTambRefC, krOhmPerC, ldoIndex).
 *        Immediately affects the heater control target: s_getRHeatEffective()
 *        (in msp1.cpp) recomputes R@Tmax from
 *        rheatTambOhm/rheatTambRefC/rheatTempMaxC/krOhmPerC on every call, so
 *        a change here takes effect on the next control step.
 *
 *        Out-of-range values are replaced by their compile-time default and
 *        the stored config is snapped to the applied value, so
 *        tMsp1_getConfig() (and the config file it feeds) never holds an
 *        invalid one: rheatTambRefC outside
 *        [MSP1_HEATER_TAMB_REF_MIN_C, MSP1_HEATER_TAMB_REF_MAX_C] or not
 *        below rheatTempMaxC, ldoIndex above MSP1_LDO_INDEX_EXTERNAL,
 *        avddExt_mV / dacVdd_mV outside their MSP1_*_MIN/MAX_MV ranges.
 *
 *        Safe to call before tMsp1_init(); does not touch the DAC or ADC.
 *
 * @param cfg  Values to apply; must not be NULL.
 *****************************************************************************/
void tMsp1_setConfig(const msp1Config_t *cfg);

/*****************************************************************************
 * @brief Copy the MSP1 parameters currently in effect (for config
 *        persistence / display / diagnostics).
 *
 * @param out  Destination buffer; must not be NULL.
 *****************************************************************************/
void tMsp1_getConfig(msp1Config_t *out);

/* -- sensor CH1: accumulation --------------------------------------------- */

/*****************************************************************************
 * @brief Read one raw sample from NAU7802 CH1 and add it to the running
 *        accumulator. Waits (bounded) for the conversion to be ready.
 *
 *        Negative differential values are clamped to zero.
 *
 * @return mspStatus_t STATUS_OK when a sample was successfully accumulated,
 *         STATUS_ERR on conversion timeout or ADC not initialised.
 *****************************************************************************/
mspStatus_t tMsp1_accumulateSensor(void);

/*****************************************************************************
 * @brief Compute the averaged sensor voltage from the accumulated samples,
 *        store it in sensor.avgVoltage_mV and set sensor.dataReady, then
 *        reset the accumulator for the next integration window.
 *
 *        Call when sensor.sampleCount has reached the desired integration
 *        target (e.g. 36 000 for a 1-hour window at 10 SPS).
 *
 * @return mspStatus_t STATUS_ERR if no samples have been accumulated yet.
 *****************************************************************************/
mspStatus_t tMsp1_computeSensorAverage(void);

/*****************************************************************************
 * @brief Sliding-window averages over the most recent
 *        MSP1_DISPLAY_AVG_SAMPLES readings, for the display pages.
 *        Any output pointer may be NULL if not needed.
 *
 * @param out_ch0_mV      Averaged CH0 sensor voltage (mV).
 * @param out_vR14_mV     Averaged CH1 V_R14 (mV).
 * @param out_current_mA  Averaged heater current derived from V_R14 (mA).
 * @return mspStatus_t STATUS_ERR until the first sample has been taken.
 *****************************************************************************/
mspStatus_t tMsp1_getDisplayAverages(float *out_ch0_mV, float *out_vR14_mV, float *out_current_mA);

/*****************************************************************************
 * @brief Derive Rno2 (Ω) from the last averaged sensor voltage:
 *
 *          Rno2 = avgVoltage_mV × MSP1_NO2_GAIN_OHM / 1000
 *
 *        Published unclamped — ADC clipping is reported separately through
 *        sensor.saturated. Stores the result in no2.rno2_ohm and sets
 *        no2.dataReady.
 *
 *        Must be called after tMsp1_computeSensorAverage().
 *
 * @return mspStatus_t STATUS_ERR if sensor.dataReady is false.
 *****************************************************************************/
mspStatus_t tMsp1_computeRno2(void);

/*****************************************************************************
 * @brief Rno2 (Ω) derived from the sliding-window ring average of the last
 *        MSP1_DISPLAY_AVG_SAMPLES CH0 samples, i.e. a live preview that does
 *        not depend on no2.rno2_ohm having been finalised by a transmission
 *        window. Settling time is fixed at ~MSP1_DISPLAY_AVG_SAMPLES
 *        wait-loop giri (~12 s), independent of the measurement period.
 *
 * @param out_rno2_ohm  Receives the ring-averaged Rno2 (Ω); untouched on error.
 * @return mspStatus_t STATUS_ERR until the ring holds at least one sample.
 *****************************************************************************/
mspStatus_t tMsp1_peekRno2Ring(float *out_rno2_ohm);

/*****************************************************************************
 * @brief Rno2/R0 ratio from the same ring average as tMsp1_peekRno2Ring(),
 *        divided by the configured baseline (msp1Config_t::r0No2_ohm). This
 *        is the quantity transmitted as pollutionData.nitrogenDioxide and
 *        shown on the NO2 display page — one implementation, so the two
 *        cannot drift.
 *
 * @param out_ratio  Receives Rno2/R0; untouched on error.
 * @return mspStatus_t STATUS_ERR if the ring is empty or R0 is not positive.
 *****************************************************************************/
mspStatus_t tMsp1_peekRno2Ratio(float *out_ratio);

/* -- heater CH2 + DAC ----------------------------------------------------- */

/*****************************************************************************
 * @brief Read one sample from NAU7802 CH2 (DRIVE−HEAT) and update all
 *        heater-derived quantities (V_R14, V_heat, I, R_heat, P). Waits
 *        (bounded) for the conversion to be ready.
 *
 *        Two independent continuous limiters may act here — neither faults
 *        nor zeroes the DAC, each just scales dacSetpoint down by a ratio
 *        computed directly from *this* measurement. The more restrictive of
 *        the two wins if both apply:
 *          - If measured power exceeds MSP1_HEATER_MAX_POWER_MW, scale by
 *            sqrt(limit / measured) — P ∝ DAC-code² for a resistive heater.
 *          - If measured current exceeds MSP1_HEATER_MAX_CURRENT_MA, scale
 *            by (limit / measured) — I ∝ DAC-code.
 *
 * @return mspStatus_t STATUS_OK on success, STATUS_ERR on conversion timeout
 *         or peripheral not initialised.
 *****************************************************************************/
mspStatus_t tMsp1_measureHeater(void);

/*****************************************************************************
 * @brief Closed-loop step: adjust the DAC to hold the heater resistance at
 *        R@Tmax = (rheatTempMaxC − rheatTambRefC) × krOhmPerC +
 *        rheatTambOhm — every term from config (msp1Config_t).
 *
 *        Constant R_heat, not constant current: the heater resistance is the
 *        image of the element temperature, so holding it constant is what
 *        keeps the MICS-2714 at its operating temperature when ambient or
 *        supply drift. Feedback is heater.rHeat_ohm (V_heat / I) from the
 *        last tMsp1_measureHeater() call.
 *
 *        Control law — the temperature rise above ambient is proportional to
 *        the dissipated power (R_meas − R@Tamb = Kr·Rth·P), and P ∝ DAC-code²
 *        at a given R_heat, so:
 *          DAC_new = dacSetpoint × sqrt((R@Tmax − R@Tamb) / (R_meas − R@Tamb))
 *        The unknown thermal resistance Rth cancels in the ratio, so no gain
 *        needs tuning. The request is additionally capped so it never asks
 *        for more than MSP1_HEATER_MAX_CURRENT_MA / MSP1_HEATER_MAX_POWER_MW
 *        (predicted from this measurement) — otherwise it would fight the
 *        limiters in tMsp1_measureHeater() every cycle. Final DAC count is
 *        hard-clamped to MSP1_DAC_MAX_SAFE and slew-rate limited
 *        (MSP1_HEATER_PI_MAX_STEP) against a single noisy reading.
 *
 *        Both limiters in tMsp1_measureHeater() can scale dacSetpoint down
 *        independently of this loop; this function's next call just resumes
 *        from wherever that left the setpoint, no explicit hand-off needed.
 *
 *        Soft-start: when called from a stopped heater (dacSetpoint == 0, or
 *        no measurable current), this function first blocking-ramps the DAC
 *        in fast, decelerating open-loop steps (see MSP1_HEATER_RAMP_STEPS)
 *        toward the MSP1_HEATER_NOMINAL_POWER_MW operating point and returns
 *        — closed-loop correction only kicks in from the next call onward, so
 *        it always converges from below.
 *
 *        Call tMsp1_measureHeater() before each control step.
 *
 * @return mspStatus_t STATUS_OK, STATUS_ERR if peripherals not ready or the
 *         configured R@Tmax is not above R@Tamb.
 *****************************************************************************/
mspStatus_t tMsp1_heaterControlStep(void);

/* -- data access ---------------------------------------------------------- */

/*****************************************************************************
 * @brief True when both peripherals are initialised. Read under the
 *        data-access mutex, so it is safe from any task.
 *****************************************************************************/
bool bMsp1_isReady(void);

/*****************************************************************************
 * @brief Copy a coherent snapshot of the module state under the data-access
 *        mutex.  Safe to call from any task (e.g. the display task).
 *
 * @param out  Destination buffer; must not be NULL.
 *****************************************************************************/
void tMsp1_getDataCopy(msp1Data_t *out);

/*****************************************************************************
 * @brief Log all MSP1 module state to the serial debug output.
 *****************************************************************************/
void vMsp1_printStatus(void);

#endif /* MSP1_H */

//****** EOF ******
