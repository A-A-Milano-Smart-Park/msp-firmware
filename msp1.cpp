/******************************************************************************
 * @file    msp1.cpp
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   MICS-2714 NO2 sensor and heater driver via NAU7802 ADC and MCP4725 DAC.
 * @version 0.3
 * @date    2026-04-08
 * @copyright Copyright (c) 2026
 *****************************************************************************/

#include "msp1.h"
#include "config.h"
#include "mspOs.h"
#include <Adafruit_NAU7802.h>
#include <Adafruit_MCP4725.h>
#include <Wire.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---- local constants ---- */

/**
 * NAU7802 PGA gain, shared by both channels (CTRL1 GAINS has no per-channel
 * copy). Both channels run with the PGA in the path at this gain; GAIN_1 is
 * also the only gain at which the -DMSP1_CH0_PGA=0 build may bypass it.
 */
#define MSP1_NAU7802_GAIN NAU7802_GAIN_1

/** Numeric PGA gain programmed by MSP1_NAU7802_GAIN: the Adafruit enum value
 *  is log2(gain) (NAU7802_GAIN_1 = 0 ... NAU7802_GAIN_128 = 7). */
#define MSP1_PGA_GAIN ((float)(1U << (unsigned)MSP1_NAU7802_GAIN))

/** Minimum sample rate: 10 SPS maximises noise integration. */
#define MSP1_NAU7802_SPS NAU7802_RATE_10SPS

/**
 * Counts spanned by the input full scale of the 24-bit two's-complement output:
 * (VINxP − VINxN) = ±0.5 × VREF / PGA_gain maps to ±2^23, so
 * V = code / 2^23 × full scale. The highest code is MSP1_ADC_MAX_CODE.
 */
#define MSP1_ADC_FULL_SCALE (8388608.0f)

/** Highest positive output code (2^23 − 1): where the ADC clips. */
#define MSP1_ADC_MAX_CODE   (8388607)

/**
 * NAU7802 ADC control register (REG 0x15) and its ADC_VCM field [3:2]: input
 * common-mode selection for single-ended (unipolar) operation. Requires the PGA
 * to be bypassed; the ADC gain is halved and common-mode rejection is reduced.
 *   0b00 -> normal differential mode, PGA in the path, full CMRR
 *   0b10 -> extended common mode for inputs sitting near REFN (ground-referred)
 *   0b11 -> extended common mode for inputs sitting near REFP (AVDD-referred)
 * Only bits [3:2] are touched — the field is written read-modify-write so the
 * chopper bits [5:4] set by Adafruit_NAU7802::begin() survive.
 */
#define MSP1_NAU7802_REG_ADC     (0x15U)
#define MSP1_ADC_VCM_MASK        (0x0CU)
#define MSP1_ADC_VCM_NORMAL      (0x00U) /* 0b00 << 2 */
#define MSP1_ADC_VCM_NEAR_REFN   (0x08U) /* 0b10 << 2 */

/**
 * NAU7802 CTRL2 (REG 0x02), offset-calibration fields. CALS starts a
 * calibration and the chip clears it when done; CAL_ERR is the result. CHS and
 * CRS share the byte, hence read-modify-write.
 */
#define MSP1_NAU7802_REG_CTRL2   (0x02U)
#define MSP1_CTRL2_CAL_ERR       (0x08U)
#define MSP1_CTRL2_CALS          (0x04U)
#define MSP1_CTRL2_CALMOD_MASK   (0x03U)
#define MSP1_CTRL2_CALMOD_SYSTEM_OFFSET (0x02U) /* 0b10 */

/** REG 0x11 bit 3, SI: shorts the inputs so a calibration sees a true zero. */
#define MSP1_NAU7802_REG_I2C_CTRL (0x11U)
#define MSP1_I2C_CTRL_SI          (0x08U)

/** Bound on the CALS wait, so a chip that never clears it cannot wedge boot. */
#define MSP1_ADC_CALIBRATION_TIMEOUT_MS (2000U)

/**
 * CH0 front end. 1 (default) = PGA in the path and ADC_VCM normal, the same
 * profile as CH1: full scale +/-AVDD/(2 x gain), and the input must stay
 * inside the PGA common-mode range, off the rails. 0 = PGA bypassed with the
 * extended common mode near REFN, full scale +/-AVDD.
 *
 * Bench knob, -DMSP1_CH0_PGA=0 selects the bypass. It also gates the CH0 zero
 * calibration in s_initADC(): REG 0x11 SI shorts inside the PGA, so with the
 * PGA bypassed the calibration never sees a short and bakes in whatever is
 * live on CH0 instead — last attempt stored 255 mV and published a real
 * 230 mV input as 0.
 */
#ifndef MSP1_CH0_PGA
#define MSP1_CH0_PGA (1)
#endif

#if (MSP1_CH0_PGA == 0)
static_assert(MSP1_NAU7802_GAIN == NAU7802_GAIN_1,
              "PGA bypass (MSP1_CH0_PGA=0) is only legal at NAU7802_GAIN_1");
#endif

/**
 * Readings discarded after every channel switch to let the PGA settle.
 * Matches the reference driver example (10 reads at 10 SPS ≈ 1 s).
 */
#define MSP1_NAU7802_CHANNEL_FLUSH_READS (10U)

/**
 * Bound on every wait for the next conversion (see s_waitConversion).
 * Expected latency is ≤100 ms at 10 SPS; this is a generous multiple so a
 * stuck/absent conversion (bus contention, hardware fault) returns
 * STATUS_ERR instead of blocking the caller — and with it the whole main
 * loop, since the sampling functions run on every measurement-cycle loop()
 * pass — forever.
 */
#define MSP1_ADC_CONVERSION_TIMEOUT_MS (500U)

/**
 * Minimum V_R14 required to compute heater quantities (mV).
 * Below this threshold no current is assumed to be flowing.
 */
#define MSP1_HEATER_MIN_VR14_MV (1.0f)

/**
 * Floor on the measured resistance rise above R@Tamb (Ω) used as the control
 * loop's denominator. A still-cold (or noisily measured) heater would give a
 * near-zero — or negative — rise and an unbounded correction ratio; the floor
 * keeps the ratio finite, and the slew limit does the rest.
 * 0.5 Ω ≈ 12 °C of rise at the default Kr.
 */
#define MSP1_HEATER_MIN_RISE_OHM (0.5f)

/* ---- local types ---- */

/**
 * Per-channel ADC front-end profile, re-applied on every channel switch.
 * With MSP1_CH0_PGA=1 (default) both rows are the same:
 *
 *   CH0 (sensor)  MICS-2714 output. MSP1_CH0_PGA=1: PGA in the path, ADC_VCM
 *                 0b00, full scale ±AVDD/(2·gain), input off the rails.
 *                 MSP1_CH0_PGA=0: PGA bypassed + extended common mode near
 *                 REFN, full scale ±AVDD, input may sit at a rail.
 *
 *   CH1 (heater)  Drop across R14, measured high-side between the DAC buffer
 *                 and the heater: a genuine differential signal riding on a
 *                 large common mode. Here CMRR is what the measurement lives
 *                 on, so the PGA stays in the path and ADC_VCM stays at 0b00.
 *                 Full scale is the normal ±AVDD/(2·gain).
 *
 * fsMult is the full-scale multiplier the mode implies, applied in s_rawToMv().
 */
typedef struct __MSP1_ADC_PROFILE_T__
{
  bool    pgaBypass;
  uint8_t vcmBits;
  float   fsMult;
} msp1AdcProfile_t;

/** Display ring buffer: last MSP1_DISPLAY_AVG_SAMPLES readings of one channel. */
typedef struct __MSP1_DISP_RING_T__
{
  float   buf[MSP1_DISPLAY_AVG_SAMPLES];
  uint8_t head;
  uint8_t count;
} msp1DispRing_t;

/* Indexed by msp1AdcChannel_t (CH_SENSOR = 0, CH_HEATER = 1). */
static const msp1AdcProfile_t MSP1_ADC_PROFILE[] = {
#if (MSP1_CH0_PGA != 0)
  { false, MSP1_ADC_VCM_NORMAL,    1.0f }, /* CH0 sensor: PGA in path, FS +/-AVDD/(2*G) */
#else
  { true,  MSP1_ADC_VCM_NEAR_REFN, 2.0f }, /* CH0 sensor: single-ended, PGA bypassed  */
#endif
  { false, MSP1_ADC_VCM_NORMAL,    1.0f }, /* CH1 heater: differential, PGA in path   */
};

/**
 * NAU7802 LDO/AVDD steps, indexed by the msp1_params "ldo_index" config field.
 * With the internal LDO on, this table is the single source of AVDD: the .ldo
 * enum is what setLDO() programs and the .mv is what every raw->mV conversion
 * scales by — one row, both uses, no drift.
 * Voltage in mV (same unit as the conversion math — no scaling anywhere).
 * Index order = ascending voltage, so index 5 = 3900 mV = MSP1_LDO_DEFAULT_INDEX.
 *
 * Index MSP1_LDO_INDEX_EXTERNAL (== table length, one past the last row) is
 * NOT in the table: it switches the internal LDO off (setLDO(NAU7802_EXTERNAL))
 * and AVDD is then whatever is fed on the pin — the conversion voltage comes
 * from s_config.avddExt_mV instead (see s_avdd_mV()).
 */
static const struct { float mv; NAU7802_LDOVoltage ldo; } MSP1_LDO_TABLE[] = {
  {2400.0f, NAU7802_2V4}, {2700.0f, NAU7802_2V7}, {3000.0f, NAU7802_3V0}, {3300.0f, NAU7802_3V3},
  {3600.0f, NAU7802_3V6}, {3900.0f, NAU7802_3V9}, {4200.0f, NAU7802_4V2}, {4500.0f, NAU7802_4V5},
};
#define MSP1_LDO_TABLE_LEN (sizeof(MSP1_LDO_TABLE) / sizeof(MSP1_LDO_TABLE[0]))

/* s_ldoIndex is seeded with MSP1_LDO_DEFAULT_INDEX, so an out-of-range default
 * would read past the table before main() even runs. */
static_assert(MSP1_LDO_DEFAULT_INDEX < MSP1_LDO_TABLE_LEN,
              "MSP1_LDO_DEFAULT_INDEX outside MSP1_LDO_TABLE");

/* s_avdd_mV() indexes the table for every value below EXTERNAL, so EXTERNAL
 * must sit exactly one past the last row. */
static_assert(MSP1_LDO_INDEX_EXTERNAL == MSP1_LDO_TABLE_LEN,
              "MSP1_LDO_INDEX_EXTERNAL must equal MSP1_LDO_TABLE length");

/* ---- static module state ---- */

static Adafruit_NAU7802 s_nau7802;
static Adafruit_MCP4725 s_mcp4725;

static msp1Data_t s_data;

/** Currently active NAU7802 channel; 0xFF = uninitialised. */
static uint8_t s_currentChannel = 0xFFU;

/** Runtime-configurable parameters, seeded with the compile-time defaults;
 *  overridden by tMsp1_setConfig() (normally from config_v4.json). Field
 *  order matches msp1Config_t. */
static msp1Config_t s_config = {
  MSP1_NO2_R0_OHM,
  MSP1_HEATER_TEMP_MAX_DEFAULT_C,
  MSP1_HEATER_TAMB_DEFAULT_OHM,
  MSP1_HEATER_TAMB_REF_C,
  MSP1_HEATER_KR_DEFAULT_OHM_PER_C,
  MSP1_LDO_DEFAULT_INDEX,
  MSP1_AVDD_EXT_DEFAULT_MV,
  MSP1_DAC_VDD_DEFAULT_MV,
  MSP1_HEATER_RAMP_DELAY_DEFAULT_MS,
};

/** Resolved LDO selection in effect — MSP1_LDO_TABLE index, or
 *  MSP1_LDO_INDEX_EXTERNAL for LDO off / external AVDD. Drives both setLDO()
 *  and, through s_avdd_mV(), every raw->mV conversion (see s_rawToMv). Seeded
 *  with the default so s_initADC() has a valid setLDO() argument even if
 *  tMsp1_setConfig() is never called. */
static uint8_t s_ldoIndex = MSP1_LDO_DEFAULT_INDEX;

/** Display ring buffers: last MSP1_DISPLAY_AVG_SAMPLES CH0/CH1 readings.
 *  Written only by the sampling task (accumulateSensor / measureHeater);
 *  the published averages below are shared and mutex-guarded. */
static msp1DispRing_t s_ch0Ring; /* CH0 sensor voltage (mV) */
static msp1DispRing_t s_ch1Ring; /* CH1 V_R14 (mV)          */
static float   s_dispAvgCh0_mV  = 0.0f;
static float   s_dispAvgVr14_mV = 0.0f;
static bool    s_dispAvgValid   = false;

/* ---- forward declarations ---- */

static void        s_ringPush(msp1DispRing_t *r, float value);
static float       s_ringAverage(const msp1DispRing_t *r);
static void        s_applyLdoIndex(uint8_t requested);
static float       s_avdd_mV(void);
static float       s_adcFullScale_mV(msp1AdcChannel_t channel);
static float       s_rawToMv(float raw, msp1AdcChannel_t channel);
static bool        s_regRead(uint8_t reg, uint8_t *out);
static bool        s_regUpdate(uint8_t reg, uint8_t mask, uint8_t value);
static bool        s_setAdcVcm(uint8_t vcmBits);
static bool        s_calibrateZero(msp1AdcChannel_t channel);
static bool        s_applyAdcProfile(msp1AdcChannel_t channel);
static bool        s_waitConversion(const char *tag);
static bool        s_flushConversions(const char *tag);
static bool        s_selectChannel(msp1AdcChannel_t channel);
static float       s_getRHeatEffective(void);
static uint16_t    s_currentToDAC(float current_mA);
static uint16_t    s_rampSeedDAC(void);
static void        s_rampToDAC(uint16_t targetCode);
static mspStatus_t s_setDAC(uint16_t setpoint);
static float       s_avgToRno2Ohm(float avgVoltage_mV);
static mspStatus_t s_initADC(void);
static mspStatus_t s_initDAC(void);

/* ---- private helpers ---- */

static void s_ringPush(msp1DispRing_t *r, float value)
{
  r->buf[r->head] = value;
  r->head = (uint8_t)((r->head + 1U) % MSP1_DISPLAY_AVG_SAMPLES);
  if (r->count < MSP1_DISPLAY_AVG_SAMPLES)
  {
    r->count++;
  }
}

static float s_ringAverage(const msp1DispRing_t *r)
{
  if (r->count == 0U)
  {
    return 0.0f;
  }
  float fSum = 0.0f;
  for (uint8_t i = 0U; i < r->count; i++)
  {
    fSum += r->buf[i];
  }
  return fSum / (float)r->count;
}

/******************************************************************************
 * @brief Resolve a requested LDO index to a valid one (a table index or
 *        MSP1_LDO_INDEX_EXTERNAL). Does not touch hardware — s_initADC()
 *        applies setLDO() from the resolved index.
 *
 *        Out-of-range falls back to the DEFAULT, not to EXTERNAL: a typo in
 *        config must not silently switch the LDO off and leave the ADC
 *        unpowered on boards with no external AVDD supply.
 *****************************************************************************/
static void s_applyLdoIndex(uint8_t requested)
{
  uint8_t idx = requested;
  if (idx > MSP1_LDO_INDEX_EXTERNAL)
  {
    log_w("MSP1 ldo_index %u out of range [0..%u]; using default %u",
          requested, (unsigned)MSP1_LDO_INDEX_EXTERNAL, (unsigned)MSP1_LDO_DEFAULT_INDEX);
    idx = MSP1_LDO_DEFAULT_INDEX;
  }
  s_ldoIndex = idx;
}

/******************************************************************************
 * @brief AVDD (mV) in effect — the ADC reference every raw->mV conversion
 *        scales by. With the internal LDO on it is the MSP1_LDO_TABLE row
 *        setLDO() programs, so supply and math cannot disagree; with the LDO
 *        off (MSP1_LDO_INDEX_EXTERNAL) the chip cannot report the externally
 *        fed AVDD, so it is the user-configured avddExt_mV.
 *****************************************************************************/
static float s_avdd_mV(void)
{
  return (s_ldoIndex == MSP1_LDO_INDEX_EXTERNAL) ? s_config.avddExt_mV
                                                 : MSP1_LDO_TABLE[s_ldoIndex].mv;
}

/******************************************************************************
 * @brief Input full-scale voltage (mV) of the given channel, from the AVDD in
 *        effect (s_avdd_mV()) — datasheet §9.2/§9.3:
 *          PGA in the path:  ±0.5 × VREF / PGA_gain
 *          PGA bypassed:     ±0.5 × VREF (the gain no longer applies)
 *        The extended common mode used by the CH0 bypass build halves the ADC
 *        gain, hence its fsMult of 2 (full scale ±AVDD).
 *****************************************************************************/
static float s_adcFullScale_mV(msp1AdcChannel_t channel)
{
  const msp1AdcProfile_t *p = &MSP1_ADC_PROFILE[channel];
  float fPgaGain = p->pgaBypass ? 1.0f : MSP1_PGA_GAIN;

  return (s_avdd_mV() / 2.0f) * p->fsMult / fPgaGain;
}

/******************************************************************************
 * @brief Convert a raw NAU7802 value to millivolts, for the given channel.
 *        The channel is an explicit argument rather than "whatever is selected
 *        right now" because the averaging helpers convert accumulated CH0 sums
 *        at times when CH1 may well be the active channel — reading the live
 *        channel here would silently scale those by the wrong full scale.
 *        Takes a float so the averaging path shares this implementation.
 *****************************************************************************/
static float s_rawToMv(float raw, msp1AdcChannel_t channel)
{
  return (raw / MSP1_ADC_FULL_SCALE) * s_adcFullScale_mV(channel);
}

/******************************************************************************
 * @brief Read one NAU7802 register. The Adafruit driver exposes no access to
 *        ADC_VCM, SI or the calibration fields and keeps its I2C device
 *        private, hence the direct Wire access.
 *****************************************************************************/
static bool s_regRead(uint8_t reg, uint8_t *out)
{
  Wire.beginTransmission((uint8_t)MSP1_NAU7802_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0U)
  {
    return false;
  }
  if (Wire.requestFrom((uint8_t)MSP1_NAU7802_I2C_ADDR, (uint8_t)1U) != 1U)
  {
    return false;
  }

  *out = (uint8_t)Wire.read();

  return true;
}

/******************************************************************************
 * @brief Read-modify-write the @p mask bits of a NAU7802 register. Every
 *        register here shares its byte with settings owned elsewhere (ADC_VCM
 *        with begin()'s chopper bits, the calibration fields with the channel
 *        select and sample rate), so nothing is ever written blind.
 *****************************************************************************/
static bool s_regUpdate(uint8_t reg, uint8_t mask, uint8_t value)
{
  uint8_t u8Reg = 0U;

  if (!s_regRead(reg, &u8Reg))
  {
    return false;
  }

  uint8_t u8New = (uint8_t)((u8Reg & (uint8_t)(~mask)) | (value & mask));
  if (u8New == u8Reg)
  {
    return true;
  }

  Wire.beginTransmission((uint8_t)MSP1_NAU7802_I2C_ADDR);
  Wire.write(reg);
  Wire.write(u8New);

  return (Wire.endTransmission() == 0U);
}

/******************************************************************************
 * @brief Write the ADC_VCM field of REG 0x15, preserving the chopper bits set
 *        by begin().
 *****************************************************************************/
static bool s_setAdcVcm(uint8_t vcmBits)
{
  return s_regUpdate(MSP1_NAU7802_REG_ADC, MSP1_ADC_VCM_MASK, vcmBits);
}

/******************************************************************************
 * @brief Apply a channel's ADC front-end profile (PGA bypass + ADC_VCM).
 *        Order matters: the extended common mode is only valid with the PGA
 *        bypassed, so entering it bypasses first, and leaving it clears
 *        ADC_VCM before putting the PGA back in the path. With both profiles
 *        equal (MSP1_CH0_PGA=1) the ADC_VCM write is skipped as a no-op.
 *****************************************************************************/
static bool s_applyAdcProfile(msp1AdcChannel_t channel)
{
  const msp1AdcProfile_t *p = &MSP1_ADC_PROFILE[channel];
  bool bOk = true;

  if (p->pgaBypass)
  {
    bOk = s_nau7802.setPGABypass(true) && bOk;
    bOk = s_setAdcVcm(p->vcmBits)      && bOk;
  }
  else
  {
    bOk = s_setAdcVcm(p->vcmBits)       && bOk;
    bOk = s_nau7802.setPGABypass(false) && bOk;
  }

  return bOk;
}

/******************************************************************************
 * @brief Wait (bounded) for the next conversion to become available.
 *
 *        Bounded and not a plain spin because available() is reliably false
 *        right after a channel switch, and every measurement-cycle loop()
 *        pass alternates CH0/CH1: an unbounded wait would freeze the whole
 *        main loop if a conversion is ever late.
 *
 * @param tag  Caller name, used in the timeout warning.
 * @return true when a conversion is ready, false on timeout (already logged).
 *****************************************************************************/
static bool s_waitConversion(const char *tag)
{
  uint32_t u32Start_ms = millis();

  while (!s_nau7802.available())
  {
    if ((millis() - u32Start_ms) > MSP1_ADC_CONVERSION_TIMEOUT_MS)
    {
      log_w("%s: NAU7802 conversion timeout", tag);
      return false;
    }
    delay(1U);
  }

  return true;
}

/******************************************************************************
 * @brief Discard MSP1_NAU7802_CHANNEL_FLUSH_READS conversions to let the ADC
 *        settle. Needed after anything that changes what it is looking at: a
 *        channel switch, a profile change, or releasing the calibration short.
 *****************************************************************************/
static bool s_flushConversions(const char *tag)
{
  for (uint8_t i = 0U; i < MSP1_NAU7802_CHANNEL_FLUSH_READS; i++)
  {
    if (!s_waitConversion(tag))
    {
      return false;
    }
    (void)s_nau7802.read();
  }

  return true;
}

/******************************************************************************
 * @brief Select a NAU7802 input channel by its function (see msp1AdcChannel_t).
 *        Re-applies that channel's front-end profile, then flushes
 *        MSP1_NAU7802_CHANNEL_FLUSH_READS conversions so the ADC has time to
 *        settle before real data is collected.
 *****************************************************************************/
static bool s_selectChannel(msp1AdcChannel_t channel)
{
  if ((uint8_t)channel == s_currentChannel)
  {
    return true;
  }

  (void)s_nau7802.setChannel((uint8_t)channel);
  s_currentChannel = (uint8_t)channel;

  /* Each channel's front end is re-applied on every switch (see
   * MSP1_ADC_PROFILE). Costs at most two short I2C writes; the settling they
   * need is already paid for by the flush below. */
  if (!s_applyAdcProfile(channel))
  {
    log_w("selectChannel: ADC profile write failed (channel %u)", (unsigned)channel);
  }

  return s_flushConversions("selectChannel");
}

/******************************************************************************
 * @brief Zero-calibrate one channel: select it, short the inputs with SI,
 *        trigger a system offset calibration, wait for CALS to clear, release
 *        the short and flush. The result lands in that channel's own offset
 *        registers. SI shorts inside the PGA, so this needs the channel's
 *        PGA in the path — see MSP1_CH0_PGA.
 *****************************************************************************/
static bool s_calibrateZero(msp1AdcChannel_t channel)
{
  if (!s_selectChannel(channel))
  {
    log_w("Zero calibration: channel %u select timed out", (unsigned)channel);
    return false;
  }

  if (!s_regUpdate(MSP1_NAU7802_REG_I2C_CTRL, MSP1_I2C_CTRL_SI, MSP1_I2C_CTRL_SI))
  {
    log_w("Zero calibration: could not short the inputs (REG 0x11 SI)");
    return false;
  }

  bool bOk = s_regUpdate(MSP1_NAU7802_REG_CTRL2,
                         (uint8_t)(MSP1_CTRL2_CALMOD_MASK | MSP1_CTRL2_CALS),
                         (uint8_t)(MSP1_CTRL2_CALMOD_SYSTEM_OFFSET | MSP1_CTRL2_CALS));
  if (!bOk)
  {
    log_w("Zero calibration: could not start the calibration (CTRL2)");
  }
  else
  {
    /* Polled, not DRDY-driven: no conversion is published while it runs. */
    uint32_t u32Start_ms = millis();
    uint8_t  u8Ctrl2     = 0U;

    do
    {
      if ((millis() - u32Start_ms) > MSP1_ADC_CALIBRATION_TIMEOUT_MS)
      {
        log_w("Zero calibration: timed out waiting for CALS to clear");
        bOk = false;
        break;
      }

      delay(10U);

      if (!s_regRead(MSP1_NAU7802_REG_CTRL2, &u8Ctrl2))
      {
        log_w("Zero calibration: CTRL2 read failed while polling");
        bOk = false;
        break;
      }
    } while ((u8Ctrl2 & MSP1_CTRL2_CALS) != 0U);

    if (bOk && ((u8Ctrl2 & MSP1_CTRL2_CAL_ERR) != 0U))
    {
      log_w("Zero calibration: chip reported CAL_ERR on channel %u", (unsigned)channel);
      bOk = false;
    }
  }

  /* Release unconditionally: left set, every later conversion reads the
   * shorted input instead of the front-end. */
  if (!s_regUpdate(MSP1_NAU7802_REG_I2C_CTRL, MSP1_I2C_CTRL_SI, 0U))
  {
    log_e("Zero calibration: could not clear REG 0x11 SI, inputs left shorted");
    return false;
  }

  (void)s_flushConversions("calibrateZero"); /* inputs just reconnected */

  if (bOk)
  {
    log_i("Zero calibration done on channel %u", (unsigned)channel);
  }

  return bOk;
}

/******************************************************************************
 * @brief Project the heater resistance at the configured target temperature
 *        (s_config.rheatTempMaxC) from the Tamb-reference resistance:
 *          R@Tmax = (rheatTempMaxC − rheatTambRefC) × krOhmPerC + rheatTambOhm
 *        with rheatTambRefC ("rheat_tamb_ref_c" in config_v4.json) the
 *        temperature at which the configured rheatTambOhm ("rheat_tamb_ohm")
 *        is rated. This is the regulation target of
 *        tMsp1_heaterControlStep() (holding it constant holds the element
 *        temperature constant) and the feed-forward R used by
 *        s_currentToDAC().
 *****************************************************************************/
static float s_getRHeatEffective(void)
{
  return ((s_config.rheatTempMaxC - s_config.rheatTambRefC) * s_config.krOhmPerC) + s_config.rheatTambOhm;
}

/******************************************************************************
 * @brief Compute the feed-forward DAC code for a target heater current.
 *
 *        V_DRIVE = I_target_mA × (R14 + R_heat)   [mV]  (mA × Ω = mV)
 *        DAC     = floor(V_DRIVE / VDD_DAC × 4096)
 *        Result is clamped to MSP1_DAC_MAX_SAFE.
 *****************************************************************************/
static uint16_t s_currentToDAC(float current_mA)
{
  float fVdrive_mV = current_mA * (MSP1_HEATER_R14_OHM + s_getRHeatEffective());

  uint32_t u32Code = (uint32_t)((fVdrive_mV / s_config.dacVdd_mV) * (float)MSP1_DAC_STEPS);

  if (u32Code > (uint32_t)MSP1_DAC_MAX_SAFE)
  {
    u32Code = (uint32_t)MSP1_DAC_MAX_SAFE;
  }

  return (uint16_t)u32Code;
}

/******************************************************************************
 * @brief DAC code the soft-start ramp aims at: the one that dissipates
 *        MSP1_HEATER_NOMINAL_POWER_MW at the configured R@Tmax.
 *
 *        I [mA] = sqrt(P [mW] × 1000 / R [Ω]), then s_currentToDAC().
 *
 *        The heater starts cold, at R@Tamb — lower than R@Tmax, so the same
 *        V_DRIVE pulls more current than the nominal point. The seed is
 *        capped so even that first, hardest pull stays under
 *        MSP1_HEATER_MAX_CURRENT_MA: with I ∝ 1/(R14 + R), the cap in
 *        nominal-point terms is I_max × (R14 + R@Tamb) / (R14 + R@Tmax).
 *****************************************************************************/
static uint16_t s_rampSeedDAC(void)
{
  float fR_ohm = s_getRHeatEffective();

  if (fR_ohm <= 0.0f)
  {
    return 0U;
  }

  float fCurrent_mA = sqrtf((MSP1_HEATER_NOMINAL_POWER_MW * 1000.0f) / fR_ohm);
  float fColdCap_mA = MSP1_HEATER_MAX_CURRENT_MA *
                      ((MSP1_HEATER_R14_OHM + s_config.rheatTambOhm) /
                       (MSP1_HEATER_R14_OHM + fR_ohm));

  if (fCurrent_mA > fColdCap_mA)
  {
    fCurrent_mA = fColdCap_mA;
  }

  return s_currentToDAC(fCurrent_mA);
}

/******************************************************************************
 * @brief Blocking open-loop ramp of the heater DAC toward targetCode.
 *
 *        Only ramps upward: each step covers 1/MSP1_HEATER_RAMP_DIVISOR of
 *        the remaining gap, so steps are large and fast at first and shrink
 *        as the target is approached — the DAC always converges from below,
 *        never overshooting past targetCode. A target at or below the
 *        current setpoint is written immediately (no need to ramp a drop).
 *        The final step lands exactly on targetCode, closing the residual
 *        left by the geometric approach.
 *****************************************************************************/
static void s_rampToDAC(uint16_t targetCode)
{
  uint16_t u16Current = s_data.heater.dacSetpoint;

  if (targetCode <= u16Current)
  {
    (void)s_setDAC(targetCode);
    return;
  }

  uint16_t u16Gap  = 0;
  uint16_t u16Step = 0;

  for (uint8_t i = 0U; i < MSP1_HEATER_RAMP_STEPS; i++)
  {
    u16Gap  = targetCode - u16Current;
    u16Step = u16Gap / MSP1_HEATER_RAMP_DIVISOR;

    if (u16Step == 0U)
    {
      break;
    }

    u16Current += u16Step;
    (void)s_setDAC(u16Current);
    delay(s_config.rampStepDelayMs);
  }

  /* Land exactly on target — the geometric approach only asymptotes to it. */
  (void)s_setDAC(targetCode);
}

/******************************************************************************
 * @brief Set the MCP4725 DAC to a raw 12-bit count. Setpoints above
 *        MSP1_DAC_MAX_SAFE are rejected, not clamped; does not write to EEPROM.
 *****************************************************************************/
static mspStatus_t s_setDAC(uint16_t setpoint)
{
  if (!s_data.mcp4725Ready)
  {
    log_e("MCP4725 is not initialised");
    return STATUS_ERR;
  }

  /* Hard ceiling at MSP1_DAC_MAX_SAFE. The 50 mW / 27.53 mA limits are
   * enforced on measurements in tMsp1_measureHeater(), not here. */
  if (setpoint > MSP1_DAC_MAX_SAFE)
  {
    log_e("DAC setpoint %u exceeds safety limit %u — rejected", setpoint, MSP1_DAC_MAX_SAFE);
    return STATUS_ERR;
  }

  /* setVoltage() returns false on I2C failure. Do NOT update dacSetpoint in
   * that case: heater.dacSetpoint is used elsewhere (V_DRIVE reconstruction,
   * fault/soft-start decisions) as ground truth for what the physical DAC
   * output is — trusting an unconfirmed write would desync software state
   * from hardware. */
  if (!s_mcp4725.setVoltage(setpoint, false, 100000U))
  {
    log_e("MCP4725 I2C write failed (setpoint %u) — DAC output unchanged, still %u",
          setpoint, s_data.heater.dacSetpoint);
    return STATUS_ERR;
  }

  vMspOs_takeDataAccessMutex();
  s_data.heater.dacSetpoint = setpoint;
  vMspOs_giveDataAccessMutex();

  log_d("MCP4725 output set to %u / %u", setpoint, MSP1_DAC_MAX_SAFE);

  return STATUS_OK;
}

/******************************************************************************
 * @brief Rno2 (Ω) from an averaged CH0 voltage (mV). Shared by
 *        tMsp1_computeRno2() (finalises into s_data.no2) and
 *        tMsp1_peekRno2Ring() (read-only, for display), so the two can't drift.
 *        Published unclamped — ADC clipping is reported through
 *        s_data.sensor.saturated instead.
 *****************************************************************************/
static float s_avgToRno2Ohm(float avgVoltage_mV)
{
  return (avgVoltage_mV * MSP1_NO2_GAIN_OHM) / 1000.0f;
}

/******************************************************************************
 * @brief Initialise the NAU7802 24-bit ADC.
 *****************************************************************************/
static mspStatus_t s_initADC(void)
{
  /* NAU7802 has no address pins: 0x2A is the only possible address (and the
   * Adafruit driver hardcodes it), so there is no address range to scan. */
  if (!s_nau7802.begin())
  {
    log_e("NAU7802 not found on I2C bus (addr=0x%02X)", MSP1_NAU7802_I2C_ADDR);
    return STATUS_ERR;
  }

  /* LDO/AVDD from the resolved config index — the same source every raw->mV
   * conversion reads (s_avdd_mV()), so the supply and the ADC math agree.
   * EXTERNAL switches the internal LDO off: AVDD is fed on the pin and the
   * conversion math uses the user-configured avdd_ext_mv. */
  if (s_ldoIndex == MSP1_LDO_INDEX_EXTERNAL)
  {
    s_nau7802.setLDO(NAU7802_EXTERNAL);
    log_i("LDO off — external AVDD, configured as %.0f mV", s_avdd_mV());
  }
  else
  {
    s_nau7802.setLDO(MSP1_LDO_TABLE[s_ldoIndex].ldo);
    log_i("LDO/AVDD set to %.0f mV (index %u)", MSP1_LDO_TABLE[s_ldoIndex].mv, s_ldoIndex);
  }

  s_nau7802.setGain(MSP1_NAU7802_GAIN);
  s_nau7802.setRate(MSP1_NAU7802_SPS);

  /* PGA output cap is only valid for single-channel operation and this driver
   * alternates CH0/CH1 — keep it off (also the reset default; set explicitly
   * so a library default change can't silently turn it on). */
  (void)s_nau7802.setPGACap(false);

  /* PGA bypass and ADC_VCM are not set here: they belong to the per-channel
   * profile that s_selectChannel applies on every switch (MSP1_ADC_PROFILE).
   * The bypass is only legal at GAIN=1: see the static_assert by MSP1_CH0_PGA. */

  /* Zero calibration, one channel at a time — each lands in that channel's
   * own offset registers. Non-fatal: a failure just leaves that channel at
   * the zero offset reset() programmed this boot. The DAC is already parked
   * at 0 V (see tMsp1_init). */
  if (!s_calibrateZero(MSP1_ADC_CH_HEATER))
  {
    log_w("NAU7802: heater channel zero calibration failed, running uncalibrated");
  }

#if (MSP1_CH0_PGA != 0)
  /* Only with the PGA in the path: see MSP1_CH0_PGA. */
  if (!s_calibrateZero(MSP1_ADC_CH_SENSOR))
  {
    log_w("NAU7802: sensor channel zero calibration failed, running uncalibrated");
  }
#endif

  /* Normal operation starts on the sensor channel. */
  if (!s_selectChannel(MSP1_ADC_CH_SENSOR))
  {
    log_e("NAU7802 init: initial channel select timed out");
    return STATUS_ERR;
  }

  /* The CH0 front end changes what its numbers mean, so it goes in the boot
   * log rather than being something you infer from a build flag. */
  log_i("NAU7802 initialised (GAIN=%.0f, 10 SPS, CH1 differential/PGA in path, "
        "CH0 %s, full scale +/-%.0f mV)",
        MSP1_PGA_GAIN,
#if (MSP1_CH0_PGA != 0)
        "PGA in path/ADC_VCM normal",
#else
        "PGA bypassed/ADC_VCM near REFN",
#endif
        s_adcFullScale_mV(MSP1_ADC_CH_SENSOR));

  s_data.nau7802Ready = true;

  return STATUS_OK;
}

/******************************************************************************
 * @brief Initialise the MCP4725 12-bit DAC and drive its output to zero.
 *****************************************************************************/
static mspStatus_t s_initDAC(void)
{
  /* MCP4725 address depends on the A0 pin and factory-programmed A1:A2 —
   * probe the whole 0x60..0x67 range and keep the first address that ACKs.
   * Raw zero-length write probe: calling begin() per address would leak one
   * Adafruit_I2CDevice per failed attempt. */
  uint8_t u8Addr = 0U;

  for (uint8_t a = MSP1_MCP4725_I2C_ADDR_MIN; a <= MSP1_MCP4725_I2C_ADDR_MAX; a++)
  {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0U)
    {
      u8Addr = a;
      break;
    }
  }

  if ((u8Addr == 0U) || !s_mcp4725.begin(u8Addr))
  {
    log_e("MCP4725 not found on I2C bus (scanned 0x%02X..0x%02X)",
          MSP1_MCP4725_I2C_ADDR_MIN, MSP1_MCP4725_I2C_ADDR_MAX);
    return STATUS_ERR;
  }

  s_mcp4725.setVoltage(0U, false, 100000U);
  s_data.heater.dacSetpoint = 0U;
  s_data.mcp4725Ready = true;

  log_i("MCP4725 initialised at I2C addr 0x%02X (output=0)", u8Addr);

  return STATUS_OK;
}

/* ---- public API ---- */

/******************************************************************************
 * tMsp1_init
 *****************************************************************************/
mspStatus_t tMsp1_init(void)
{
  mspStatus_t tResult = STATUS_OK;

  (void)memset(&s_data, 0, sizeof(s_data));

  log_i("Initialising MSP1 peripherals (MICS-2714 NO2)...");

  /* DAC first, output at 0 V: the ADC zero calibration in s_initADC() is only
   * meaningful with the heater drive quiet. */
  if (s_initDAC() != STATUS_OK)
  {
    log_w("MCP4725 init failed - heater DAC unavailable, and the ADC zero "
          "calibration below cannot assume the heater drive is at 0 V");
    tResult = STATUS_ERR;
  }

  if (s_initADC() != STATUS_OK)
  {
    log_w("NAU7802 init failed — sensor and heater ADC unavailable");
    tResult = STATUS_ERR;
  }

  return tResult;
}

/******************************************************************************
 * tMsp1_setConfig
 *****************************************************************************/
void tMsp1_setConfig(const msp1Config_t *cfg)
{
  if (cfg == NULL)
  {
    return;
  }

  s_config = *cfg;

  /* Tamb reference: a value outside the sane range — or not below Tmax, which
   * would make the target rise zero or negative and stall the control loop —
   * falls back to the compile-time default. Snapped into s_config so
   * tMsp1_getConfig()/save never persist an invalid one. */
  if ((s_config.rheatTambRefC < MSP1_HEATER_TAMB_REF_MIN_C) ||
      (s_config.rheatTambRefC > MSP1_HEATER_TAMB_REF_MAX_C) ||
      (s_config.rheatTambRefC >= s_config.rheatTempMaxC))
  {
    log_w("MSP1 config: Tamb ref %.1f C invalid (range [%.0f, %.0f], must be < Tmax %.1f C) — using %.1f C",
          s_config.rheatTambRefC, MSP1_HEATER_TAMB_REF_MIN_C, MSP1_HEATER_TAMB_REF_MAX_C,
          s_config.rheatTempMaxC, MSP1_HEATER_TAMB_REF_C);
    s_config.rheatTambRefC = MSP1_HEATER_TAMB_REF_C;
  }

  /* Resolve the LDO index, then snap s_config.ldoIndex to the resolved value
   * so tMsp1_getConfig()/save persist a valid index. */
  s_applyLdoIndex(s_config.ldoIndex);
  s_config.ldoIndex = s_ldoIndex;

  /* External AVDD and DAC VDD: out-of-range (per the device datasheets) falls
   * back to the compile-time default — both feed voltage math (raw->mV, DAC
   * code computation), so a garbage value must not survive into it. */
  if ((s_config.avddExt_mV < MSP1_AVDD_EXT_MIN_MV) || (s_config.avddExt_mV > MSP1_AVDD_EXT_MAX_MV))
  {
    log_w("MSP1 config: avdd_ext_mv %.0f out of range [%.0f, %.0f] — using %.0f mV",
          s_config.avddExt_mV, MSP1_AVDD_EXT_MIN_MV, MSP1_AVDD_EXT_MAX_MV, MSP1_AVDD_EXT_DEFAULT_MV);
    s_config.avddExt_mV = MSP1_AVDD_EXT_DEFAULT_MV;
  }
  if ((s_config.dacVdd_mV < MSP1_DAC_VDD_MIN_MV) || (s_config.dacVdd_mV > MSP1_DAC_VDD_MAX_MV))
  {
    log_w("MSP1 config: dac_vdd_mv %.0f out of range [%.0f, %.0f] — using %.0f mV",
          s_config.dacVdd_mV, MSP1_DAC_VDD_MIN_MV, MSP1_DAC_VDD_MAX_MV, MSP1_DAC_VDD_DEFAULT_MV);
    s_config.dacVdd_mV = MSP1_DAC_VDD_DEFAULT_MV;
  }

  /* Soft-start ramp pacing: only an upper bound — the ramp blocks the calling
   * task for MSP1_HEATER_RAMP_STEPS × this delay, so cap the total stall. */
  if (s_config.rampStepDelayMs > MSP1_HEATER_RAMP_DELAY_MAX_MS)
  {
    log_w("MSP1 config: ramp_step_delay_ms %u above max %u — using %u ms",
          s_config.rampStepDelayMs, MSP1_HEATER_RAMP_DELAY_MAX_MS, MSP1_HEATER_RAMP_DELAY_DEFAULT_MS);
    s_config.rampStepDelayMs = MSP1_HEATER_RAMP_DELAY_DEFAULT_MS;
  }

  log_i("MSP1 config: R0=%.0f ohm  Tmax=%.1f C  Rtamb=%.1f ohm @ %.1f C  Kr=%.4f ohm/C  -> R_heat(Tmax)=%.2f ohm",
        s_config.r0No2_ohm, s_config.rheatTempMaxC, s_config.rheatTambOhm, s_config.rheatTambRefC,
        s_config.krOhmPerC, s_getRHeatEffective());
  log_i("MSP1 AVDD: %s -> %.0f mV (full scale: CH0 +/-%.0f mV, CH1 +/-%.0f mV)  DAC VDD: %.0f mV",
        (s_ldoIndex == MSP1_LDO_INDEX_EXTERNAL) ? "EXTERNAL (LDO off)" : "internal LDO",
        s_avdd_mV(),
        s_adcFullScale_mV(MSP1_ADC_CH_SENSOR),
        s_adcFullScale_mV(MSP1_ADC_CH_HEATER),
        s_config.dacVdd_mV);
}

/******************************************************************************
 * tMsp1_getConfig
 *****************************************************************************/
void tMsp1_getConfig(msp1Config_t *out)
{
  if (out == NULL)
  {
    return;
  }

  *out = s_config;
}

/******************************************************************************
 * tMsp1_accumulateSensor
 *****************************************************************************/
mspStatus_t tMsp1_accumulateSensor(void)
{
  int32_t s32Raw = 0;

  if (!s_data.nau7802Ready)
  {
    return STATUS_ERR;
  }

  if (!s_selectChannel(MSP1_ADC_CH_SENSOR))
  {
    return STATUS_ERR;
  }

  if (!s_waitConversion("accumulateSensor"))
  {
    return STATUS_ERR;
  }

  s32Raw = s_nau7802.read();

  float fRaw_mV = s_rawToMv((float)s32Raw, MSP1_ADC_CH_SENSOR);
  /* Positive saturation: raw at or within 100 counts of the highest code */
  bool bSaturated = (s32Raw >= (MSP1_ADC_MAX_CODE - 100));

  vMspOs_takeDataAccessMutex();
  s_data.sensor.rawSum += (int64_t)s32Raw;
  s_data.sensor.sampleCount++;
  s_data.sensor.lastRaw_mV = fRaw_mV;
  s_data.sensor.saturated  = bSaturated;
  uint32_t u32Count = s_data.sensor.sampleCount;
  vMspOs_giveDataAccessMutex();

  /* Display window: zero-floored, the display has no use for a negative. */
  s_ringPush(&s_ch0Ring, (fRaw_mV < 0.0f) ? 0.0f : fRaw_mV);
  float fAvgCh0_mV  = s_ringAverage(&s_ch0Ring);
  float fAvgVr14_mV = s_ringAverage(&s_ch1Ring);
  vMspOs_takeDataAccessMutex();
  s_dispAvgCh0_mV  = fAvgCh0_mV;
  s_dispAvgVr14_mV = fAvgVr14_mV;
  s_dispAvgValid   = true;
  vMspOs_giveDataAccessMutex();

  log_d("Sensor sample #%lu raw=%ld%s  V=%.1f mV  win-avg: CH0=%.1f mV V_R14=%.1f mV",
        u32Count, s32Raw, bSaturated ? " [SAT]" : "", fRaw_mV, fAvgCh0_mV, fAvgVr14_mV);

  return STATUS_OK;
}

/******************************************************************************
 * tMsp1_computeSensorAverage
 *****************************************************************************/
mspStatus_t tMsp1_computeSensorAverage(void)
{
  if (s_data.sensor.sampleCount == 0U)
  {
    log_w("computeSensorAverage called with no accumulated samples");
    return STATUS_ERR;
  }

  vMspOs_takeDataAccessMutex();
  float fAvgRaw = (float)s_data.sensor.rawSum / (float)s_data.sensor.sampleCount;
  float fAvg_mV = s_rawToMv(fAvgRaw, MSP1_ADC_CH_SENSOR);

  /* Rno2 cannot be negative, but the floor belongs on the average, not on the
   * counts: a zero input dithers either side of 0 and flooring per sample
   * would discard the negative half and bias the mean up. */
  if (fAvg_mV < 0.0f)
  {
    fAvg_mV = 0.0f;
  }
  uint32_t u32Count = s_data.sensor.sampleCount;
  s_data.sensor.avgVoltage_mV = fAvg_mV;
  s_data.sensor.dataReady = true;
  /* Reset accumulator for next integration window */
  s_data.sensor.rawSum = 0;
  s_data.sensor.sampleCount = 0U;
  vMspOs_giveDataAccessMutex();

  log_i("Sensor average: %.3f mV  (%lu samples)", fAvg_mV, u32Count);

  return STATUS_OK;
}

/******************************************************************************
 * tMsp1_getDisplayAverages
 *****************************************************************************/
mspStatus_t tMsp1_getDisplayAverages(float *out_ch0_mV, float *out_vR14_mV, float *out_current_mA)
{
  vMspOs_takeDataAccessMutex();
  bool  bValid    = s_dispAvgValid;
  float fCh0_mV   = s_dispAvgCh0_mV;
  float fVr14_mV  = s_dispAvgVr14_mV;
  vMspOs_giveDataAccessMutex();

  if (!bValid)
  {
    return STATUS_ERR;
  }

  if (out_ch0_mV != NULL)
  {
    *out_ch0_mV = fCh0_mV;
  }
  if (out_vR14_mV != NULL)
  {
    *out_vR14_mV = fVr14_mV;
  }
  if (out_current_mA != NULL)
  {
    *out_current_mA = fVr14_mV / MSP1_HEATER_R14_OHM;
  }

  return STATUS_OK;
}

/******************************************************************************
 * tMsp1_computeRno2
 *****************************************************************************/
mspStatus_t tMsp1_computeRno2(void)
{
  if (!s_data.sensor.dataReady)
  {
    log_w("computeRno2: sensor average not ready — call computeSensorAverage first");
    return STATUS_ERR;
  }

  float fRno2_ohm = s_avgToRno2Ohm(s_data.sensor.avgVoltage_mV);

  vMspOs_takeDataAccessMutex();
  s_data.no2.vDiff_mV = s_data.sensor.avgVoltage_mV;
  s_data.no2.rno2_ohm = fRno2_ohm;
  s_data.no2.dataReady = true;
  vMspOs_giveDataAccessMutex();

  return STATUS_OK;
}

/******************************************************************************
 * tMsp1_peekRno2Ring
 *
 * Live Rno2 from the published sliding-window CH0 average (last
 * MSP1_DISPLAY_AVG_SAMPLES samples, refreshed on every accumulateSensor()
 * call), so the result only remembers the last ~MSP1_DISPLAY_AVG_SAMPLES
 * loop() passes instead of the whole transmission window.
 *****************************************************************************/
mspStatus_t tMsp1_peekRno2Ring(float *out_rno2_ohm)
{
  if (out_rno2_ohm == NULL)
  {
    return STATUS_ERR;
  }

  vMspOs_takeDataAccessMutex();
  bool  bValid  = s_dispAvgValid;
  float fCh0_mV = s_dispAvgCh0_mV;
  vMspOs_giveDataAccessMutex();

  if (!bValid)
  {
    return STATUS_ERR;
  }

  *out_rno2_ohm = s_avgToRno2Ohm(fCh0_mV);

  return STATUS_OK;
}

/******************************************************************************
 * tMsp1_peekRno2Ratio
 *****************************************************************************/
mspStatus_t tMsp1_peekRno2Ratio(float *out_ratio)
{
  if (out_ratio == NULL)
  {
    return STATUS_ERR;
  }

  float fRno2_ohm = 0.0f;
  if (tMsp1_peekRno2Ring(&fRno2_ohm) != STATUS_OK)
  {
    return STATUS_ERR;
  }

  if (s_config.r0No2_ohm <= 0.0f)
  {
    return STATUS_ERR;
  }

  *out_ratio = fRno2_ohm / s_config.r0No2_ohm;

  return STATUS_OK;
}

/******************************************************************************
 * tMsp1_measureHeater
 *
 *   CH1 (DRIVE − HEAT) -> V_R14 (differential drop across R14 = 50 Ω)
 *   V_DRIVE = dacSetpoint / 4096 × VDD_DAC
 *   I_heat  = V_R14  / R14           (mA)
 *   V_heat  = V_DRIVE − V_R14        (mV)
 *   P_heat  = V_heat × I_heat / 1000 (mW)
 *   R_heat  = V_heat / I_heat        (Ω)  — feedback for the control loop
 *****************************************************************************/
mspStatus_t tMsp1_measureHeater(void)
{
  int32_t s32Raw = 0;

  if (!s_data.nau7802Ready || !s_data.mcp4725Ready)
  {
    return STATUS_ERR;
  }

  if (!s_selectChannel(MSP1_ADC_CH_HEATER))
  {
    return STATUS_ERR;
  }

  if (!s_waitConversion("measureHeater"))
  {
    return STATUS_ERR;
  }

  s32Raw = s_nau7802.read();

  float fVr14_mV = s_rawToMv((float)s32Raw, MSP1_ADC_CH_HEATER);

  /* Clamp noise-floor negatives to zero */
  if (fVr14_mV < 0.0f)
  {
    fVr14_mV = 0.0f;
  }

  /* V_DRIVE reconstructed from DAC setpoint using the configured DAC supply */
  float fVdrive_mV = ((float)s_data.heater.dacSetpoint / (float)MSP1_DAC_STEPS) * s_config.dacVdd_mV;

  /* Derived heater quantities (computed into locals, published as one group)
   *   I  [mA]  = V_R14 [mV] / R14 [Ω]
   *   Vh [mV]  = V_DRIVE [mV] − V_R14 [mV]
   *   P  [mW]  = Vh [mV] × I [mA] / 1000          (mV·mA = µW, /1000 -> mW)
   *   Rh [Ω]   = Vh [mV] / I [mA] */
  bool  bNoCurrent  = (fVr14_mV < MSP1_HEATER_MIN_VR14_MV);
  float fCurrent_mA = 0.0f;
  float fVheater_mV = 0.0f;
  float fPower_mW   = 0.0f;
  float fRheat_ohm  = 0.0f;

  if (!bNoCurrent)
  {
    fCurrent_mA = fVr14_mV / MSP1_HEATER_R14_OHM;
    fVheater_mV = fVdrive_mV - fVr14_mV;
    fPower_mW   = (fVheater_mV * fCurrent_mA) / 1000.0f;
    fRheat_ohm  = fVheater_mV / fCurrent_mA;
  }

  /* Publish the measured and derived quantities as one coherent group */
  vMspOs_takeDataAccessMutex();
  s_data.heater.vR14_mV    = fVr14_mV;
  s_data.heater.vHeater_mV = fVheater_mV;
  s_data.heater.current_mA = fCurrent_mA;
  s_data.heater.rHeat_ohm  = fRheat_ohm;
  s_data.heater.power_mW   = fPower_mW;
  vMspOs_giveDataAccessMutex();

  /* Display ring: CH1 reading (averaged alongside CH0, see accumulateSensor) */
  s_ringPush(&s_ch1Ring, fVr14_mV);

  if (bNoCurrent)
  {
    /* No current flowing — derived quantities are undefined (left at zero) */
    return STATUS_OK;
  }

  log_d("Heater: V_R14=%.1f mV  V_DRIVE=%.1f mV  Vh=%.1f mV  I=%.2f mA  P=%.2f mW  R=%.2f ohm (target %.2f)",
        fVr14_mV, fVdrive_mV, fVheater_mV, fCurrent_mA, fPower_mW, fRheat_ohm, s_getRHeatEffective());

  /* Safety: two independent continuous limiters. Neither faults nor zeroes the
   * DAC — each scales dacSetpoint down by a ratio computed directly from THIS
   * measurement. For a resistive heater both V_heat and V_R14 are proportional
   * to V_DRIVE (hence to the DAC code) at a given R_heat, so:
   *   I = V_R14 / R14                    ∝ DAC code   (linear)
   *   P = V_heat × I                     ∝ DAC code²  (quadratic)
   * Scaling the *measured* code by (limit / measured) — sqrt for power, linear
   * for current — lands close to the limit whatever the R_heat estimate, and
   * always yields a code below the current setpoint when over limit.
   *
   * The DAC write is done outside the lock — s_setDAC() takes the mutex
   * itself, so holding it here would deadlock. */
  bool bPowerOver   = (fPower_mW > MSP1_HEATER_MAX_POWER_MW);
  bool bCurrentOver = (fCurrent_mA > MSP1_HEATER_MAX_CURRENT_MA);

  if (bPowerOver || bCurrentOver)
  {
    uint16_t u16LimitCode = s_data.heater.dacSetpoint;

    if (bPowerOver)
    {
      float fScale = sqrtf(MSP1_HEATER_MAX_POWER_MW / fPower_mW);
      uint16_t u16PowerLimitCode = (uint16_t)((float)s_data.heater.dacSetpoint * fScale);

      log_w("Heater power %.2f mW exceeds limit %.1f mW — scaling DAC %u->%u",
            fPower_mW, MSP1_HEATER_MAX_POWER_MW, s_data.heater.dacSetpoint, u16PowerLimitCode);

      if (u16PowerLimitCode < u16LimitCode)
      {
        u16LimitCode = u16PowerLimitCode;
      }
    }

    if (bCurrentOver)
    {
      float fScale = MSP1_HEATER_MAX_CURRENT_MA / fCurrent_mA;
      uint16_t u16CurrentLimitCode = (uint16_t)((float)s_data.heater.dacSetpoint * fScale);

      log_w("Heater current %.2f mA exceeds limit %.2f mA - scaling DAC %u->%u",
            fCurrent_mA, MSP1_HEATER_MAX_CURRENT_MA, s_data.heater.dacSetpoint, u16CurrentLimitCode);

      if (u16CurrentLimitCode < u16LimitCode)
      {
        u16LimitCode = u16CurrentLimitCode;
      }
    }

    (void)s_setDAC(u16LimitCode);
  }

  return STATUS_OK;
}

/******************************************************************************
 * tMsp1_heaterControlStep  (constant-R_heat controller)
 *
 *   Target: R@Tmax = (rheatTempMaxC − Tamb) × krOhmPerC + rheatTambOhm,
 *           entirely from config (s_getRHeatEffective()).
 *   Scale:  DAC_new = dacSetpoint × sqrt((R@Tmax − R@Tamb) / (R_meas − R@Tamb))
 *   Clamp:  predicted I ≤ MSP1_HEATER_MAX_CURRENT_MA, predicted P ≤
 *           MSP1_HEATER_MAX_POWER_MW, DAC ∈ [0, MSP1_DAC_MAX_SAFE],
 *           slew-limited to ±MSP1_HEATER_PI_MAX_STEP.
 *
 *   heater.rHeat_ohm (V_heat / I, from the last tMsp1_measureHeater()) is the
 *   feedback variable: heater resistance is the image of element temperature,
 *   so holding it at R@Tmax is what keeps the MICS-2714 at its operating
 *   temperature — constant current does not, since the same current gives a
 *   different temperature as ambient and R drift.
 *
 *   The law needs no tuned gain: the resistance rise above ambient is
 *   proportional to dissipated power (R_meas − R@Tamb = Kr·Rth·P) and P ∝
 *   DAC-code² at a given R_heat, so the unknown thermal resistance Rth
 *   cancels in the ratio of the two rises.
 *****************************************************************************/
mspStatus_t tMsp1_heaterControlStep(void)
{
  if (!s_data.nau7802Ready || !s_data.mcp4725Ready)
  {
    return STATUS_ERR;
  }

  float fRtarget_ohm  = s_getRHeatEffective();
  float fTargetRise   = fRtarget_ohm - s_config.rheatTambOhm;

  if (fTargetRise <= 0.0f)
  {
    log_e("heaterControlStep: configured R@%.0fC (%.2f ohm) not above R@Tamb (%.2f ohm)",
          s_config.rheatTempMaxC, fRtarget_ohm, s_config.rheatTambOhm);
    return STATUS_ERR;
  }

  /* Soft-start: first activation from a stopped heater — ramp quickly in
   * open loop (fast, decelerating steps) instead of jumping straight to the
   * feed-forward DAC code. Skip the correction this cycle; the next call
   * picks it up once heater.rHeat_ohm reflects the new setpoint. This is the
   * only place the current feed-forward is still used — there is no valid
   * measurement to scale from yet. */
  if ((s_data.heater.dacSetpoint == 0U) || (s_data.heater.current_mA <= 0.0f))
  {
    uint16_t u16FfCode = s_rampSeedDAC();
    log_d("Heater ctrl: soft-start ramp to DAC %u (%.0f mW at R@Tmax) before closed loop",
          u16FfCode, MSP1_HEATER_NOMINAL_POWER_MW);
    s_rampToDAC(u16FfCode);
    vTaskDelay(pdMS_TO_TICKS(MSP1_HEATER_CONTROL_LOOP_DELAY_MS));
    return STATUS_OK;
  }

  /* Measured rise above the ambient-rated resistance. Floored so a cold or
   * noisy reading cannot produce an unbounded ratio (the slew limit below
   * bounds what actually reaches the DAC in any case). */
  float fMeasRise = s_data.heater.rHeat_ohm - s_config.rheatTambOhm;

  if (fMeasRise < MSP1_HEATER_MIN_RISE_OHM)
  {
    fMeasRise = MSP1_HEATER_MIN_RISE_OHM;
  }

  float fScale = sqrtf(fTargetRise / fMeasRise);
  float fNewSetpoint = (float)s_data.heater.dacSetpoint * fScale;

  /* Never request more than the safety ceilings allow, predicted from THIS
   * measurement (I ∝ DAC code, P ∝ DAC code²). Without this the loop would
   * push the DAC back up every cycle against the limiters in
   * tMsp1_measureHeater() whenever the R model asks for more than 50 mW /
   * 27.53 mA — the two would ping-pong instead of settling. */
  float fMaxByCurrent = (float)s_data.heater.dacSetpoint *
                        (MSP1_HEATER_MAX_CURRENT_MA / s_data.heater.current_mA);

  if (fNewSetpoint > fMaxByCurrent)
  {
    fNewSetpoint = fMaxByCurrent;
  }

  if (s_data.heater.power_mW > 0.0f)
  {
    float fMaxByPower = (float)s_data.heater.dacSetpoint *
                        sqrtf(MSP1_HEATER_MAX_POWER_MW / s_data.heater.power_mW);

    if (fNewSetpoint > fMaxByPower)
    {
      fNewSetpoint = fMaxByPower;
    }
  }

  /* Clamp to [0, DAC_MAX_SAFE] */
  int32_t s32New = (int32_t)fNewSetpoint;
  if (s32New < 0)
  {
    s32New = 0;
  }
  else if (s32New > (int32_t)MSP1_DAC_MAX_SAFE)
  {
    s32New = (int32_t)MSP1_DAC_MAX_SAFE;
  }

  /* Slew-rate limit: cap the per-step DAC change. A single stale or noisy
   * reading (e.g. taken right after the soft-start ramp, before the
   * NAU7802's ~100 ms conversion has caught up with the new DAC level) can
   * otherwise produce an extreme scale ratio and swing the output straight
   * to an extreme in one step. Real corrections still converge within a
   * handful of calls; only single-sample spikes are absorbed. */
  int32_t s32Delta = s32New - (int32_t)s_data.heater.dacSetpoint;
  if (s32Delta > (int32_t)MSP1_HEATER_PI_MAX_STEP)
  {
    s32New = (int32_t)s_data.heater.dacSetpoint + (int32_t)MSP1_HEATER_PI_MAX_STEP;
  }
  else if (s32Delta < -(int32_t)MSP1_HEATER_PI_MAX_STEP)
  {
    s32New = (int32_t)s_data.heater.dacSetpoint - (int32_t)MSP1_HEATER_PI_MAX_STEP;
  }

  log_d("Heater ctrl: R target=%.2f ohm  meas=%.2f ohm  I=%.2f mA  scale=%.4f  DAC %u->%ld",
        fRtarget_ohm, s_data.heater.rHeat_ohm, s_data.heater.current_mA, fScale,
        s_data.heater.dacSetpoint, s32New);

  mspStatus_t tResult = s_setDAC((uint16_t)s32New);

  /* Pace successive control-loop iterations, regardless of call site: gives
   * each correction time to show up in the next measured current before
   * another step reacts to it. */
  vTaskDelay(pdMS_TO_TICKS(MSP1_HEATER_CONTROL_LOOP_DELAY_MS));

  return tResult;
}

/******************************************************************************
 * bMsp1_isReady
 *****************************************************************************/
bool bMsp1_isReady(void)
{
  vMspOs_takeDataAccessMutex();
  bool bReady = s_data.nau7802Ready && s_data.mcp4725Ready;
  vMspOs_giveDataAccessMutex();

  return bReady;
}

/******************************************************************************
 * tMsp1_getDataCopy
 *****************************************************************************/
void tMsp1_getDataCopy(msp1Data_t *out)
{
  if (out == NULL)
  {
    return;
  }

  vMspOs_takeDataAccessMutex();
  *out = s_data;
  vMspOs_giveDataAccessMutex();
}

/******************************************************************************
 * vMsp1_printStatus
 *****************************************************************************/
void vMsp1_printStatus(void)
{
  log_i("=== MSP1 status (MICS-2714 NO2) ===");

  log_i("  NAU7802 ready    : %s", s_data.nau7802Ready ? "YES" : "NO");
  log_i("  MCP4725 ready    : %s", s_data.mcp4725Ready ? "YES" : "NO");

  log_i("  [sensor CH1 — accumulation]");
  log_i("    samples accum  : %lu", s_data.sensor.sampleCount);
  log_i("    avg voltage    : %.3f mV", s_data.sensor.avgVoltage_mV);
  log_i("    data ready     : %s", s_data.sensor.dataReady ? "YES" : "NO");

  log_i("  [NO2 derived]");
  log_i("    V_diff         : %.3f mV", s_data.no2.vDiff_mV);
  log_i("    Rno2        : %.0f ohm", s_data.no2.rno2_ohm);
  log_i("    data ready     : %s", s_data.no2.dataReady ? "YES" : "NO");

  log_i("  [heater CH2 + DAC]");
  log_i("    DAC setpoint   : %u / %u", s_data.heater.dacSetpoint, MSP1_DAC_MAX_SAFE);
  log_i("    V_R14          : %.1f mV", s_data.heater.vR14_mV);
  log_i("    V_heater       : %.1f mV", s_data.heater.vHeater_mV);
  log_i("    current        : %.2f mA", s_data.heater.current_mA);
  log_i("    R_heat         : %.2f ohm (target %.2f @ %.0f C)",
        s_data.heater.rHeat_ohm, s_getRHeatEffective(), s_config.rheatTempMaxC);
  log_i("    power (theor.) : %.2f mW", s_data.heater.power_mW);
}

//****** EOF ******
