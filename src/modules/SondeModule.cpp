#include "SondeModule.h"

#ifdef HAS_SONDE

#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Throttle.h"
#include <algorithm>
#include <math.h>
#include <string.h>

SondeModule *sondeModule;

#ifndef SONDE_INTERVAL_SECS
#define SONDE_INTERVAL_SECS (30 * 60)
#endif
#ifndef SONDE_SIMULATE
#define SONDE_SIMULATE 0
#endif
#ifndef SONDE_BRUSH
#define SONDE_BRUSH 1
#endif
#ifndef SONDE_POWER_PIN
#define SONDE_POWER_PIN -1
#endif
// 0 = no weather station on the bus.
#ifndef WX_MODBUS_ADDR
#define WX_MODBUS_ADDR 0
#endif

static constexpr int32_t FIRST_CYCLE_DELAY_MS = 45 * 1000;
static constexpr int32_t WARMUP_MS = 2000;         // Y4000 answers ~1 s after power-up
static constexpr int32_t STABILIZE_MS = 50 * 1000; // EnviroDIY ModularSensors Y4000 stabilisation time
static constexpr uint32_t REPLY_TIMEOUT_MS = 1000;
static constexpr int32_t REPLY_POLL_MS = 50;
static constexpr int32_t RETRY_GAP_MS = 500;
static constexpr int32_t STEP_MS = 10;
static constexpr uint8_t MAX_ATTEMPTS = 3;
static constexpr uint32_t ERROR_REPORT_INTERVAL_MS = 5 * 60 * 1000;
// DFRobot asks for > 200 ms between SEN0657 polls; at 200 ms it mostly answered with a fixed "not ready" pattern.
static constexpr int32_t WX_GAP_MS = 1000;
static constexpr uint8_t WX_MAX_ATTEMPTS = 2;
// Intervals shorter than a power-up + stabilisation cycle keep the sonde powered and skip the brush between reads.
static constexpr bool CONTINUOUS = SONDE_INTERVAL_SECS * 1000 < WARMUP_MS + STABILIZE_MS;

static constexpr uint8_t FN_READ_HOLDING = 0x03;
static constexpr uint8_t FN_WRITE_MULTIPLE = 0x10;
// EnviroDIY YosemitechModbus: the sonde's 8 floats start at 0x2601 (the manual's 0x2600 is wrong).
static constexpr uint16_t REG_VALUES = 0x2601;
static constexpr uint16_t REG_STATUS = 0x0800;
static constexpr uint16_t REG_BRUSH = 0x2F00;
static constexpr size_t VALUES_REPLY_LEN = 5 + 8 * 4; // addr, fn, byte count, 8 floats, crc16
static constexpr size_t STATUS_REPLY_LEN = 5 + 2;

// DFRobot SEN0657: one block read 0x01F4..0x0201 covers every measurement (big-endian 16-bit registers).
// One register per request: on this unit at 9600 baud, multi-register reads usually come back as a constant
// "not ready" pattern (0x32D0, 0xF000, ...) while single-register reads are reliable.
static constexpr uint16_t WX_REGS[] = {
    0x01F4, // wind speed, m/s x10
    0x01F7, // wind direction, degrees (0 = north, clockwise)
    0x01F8, // humidity, %RH x10
    0x01F9, // temperature, degC x10 (signed)
    0x01FD, // pressure, 0.1 kPa = hPa
    0x01FE, // light, high word
    0x01FF, // light, low word
    0x0201, // rain, mm x10, accumulated since power-up or zeroing
};
static constexpr uint8_t WX_READ_COUNT = sizeof(WX_REGS) / sizeof(WX_REGS[0]);

enum ValueIndex : uint8_t { DO_MGL, TURBIDITY_NTU, COND_MS_CM, PH, TEMP_C, ORP_MV, CHL_UG_L, BGA };

static uint16_t modbusCrc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

// The Y4000 reports -9999 (or garbage) for probes that are not fitted.
static bool usable(float v)
{
    return isfinite(v) && v > -9000.0f && fabsf(v) < 1.0e6f;
}

namespace
{
struct JsonWriter {
    char *buf;
    size_t cap;
    size_t len = 0;
    bool ok = true;

    JsonWriter(char *b, size_t c) : buf(b), cap(c) { raw("{"); }

    void raw(const char *s)
    {
        size_t n = strlen(s);
        if (!ok || len + n + 1 > cap) {
            ok = false;
            return;
        }
        memcpy(buf + len, s, n);
        len += n;
        buf[len] = '\0';
    }

    void key(const char *k)
    {
        raw(len > 1 ? ",\"" : "\"");
        raw(k);
        raw("\":");
    }

    void integer(const char *k, uint32_t v)
    {
        char tmp[12];
        snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)v);
        key(k);
        raw(tmp);
    }

    void str(const char *k, const char *v)
    {
        key(k);
        raw("\"");
        raw(v);
        raw("\"");
    }

    // Integer-only fixed-point formatting so the output doesn't depend on newlib float printf support.
    void fixed(const char *k, int32_t scaled, uint8_t decimals)
    {
        char tmp[16];
        char *p = tmp + sizeof(tmp);
        *--p = '\0';
        uint32_t mag = scaled < 0 ? (uint32_t)(-(int64_t)scaled) : (uint32_t)scaled;
        for (uint8_t i = 0; i < decimals; i++) {
            *--p = '0' + mag % 10;
            mag /= 10;
        }
        if (decimals)
            *--p = '.';
        do {
            *--p = '0' + mag % 10;
            mag /= 10;
        } while (mag);
        if (scaled < 0)
            *--p = '-';
        key(k);
        raw(p);
    }

    void number(const char *k, float v, uint8_t decimals)
    {
        if (!usable(v))
            return;
        static const float scale[] = {1.0f, 10.0f, 100.0f, 1000.0f};
        fixed(k, (int32_t)lroundf(v * scale[decimals]), decimals);
    }

    size_t finish()
    {
        raw("}");
        return ok ? len : 0;
    }
};

void addPosition(JsonWriter &j)
{
    if (localPosition.latitude_i != 0 || localPosition.longitude_i != 0) {
        j.fixed("lat", localPosition.latitude_i / 10, 6);
        j.fixed("lon", localPosition.longitude_i / 10, 6);
    }
}
} // namespace

SondeModule::SondeModule() : SinglePortModule("sonde", meshtastic_PortNum_PRIVATE_APP), concurrency::OSThread("Sonde") {}

void SondeModule::powerSonde(bool on)
{
#if SONDE_POWER_PIN >= 0
    digitalWrite(SONDE_POWER_PIN, on ? HIGH : LOW);
#endif
    // The UART is only open while the sonde is powered so the UARTE doesn't hold HFCLK on between cycles.
    if (on)
        RS485_SERIAL.begin(RS485_BAUD);
    else
        RS485_SERIAL.end();
    powered = on;
}

void SondeModule::sendFrame(uint8_t *frame, size_t lenWithoutCrc)
{
    uint16_t crc = modbusCrc(frame, lenWithoutCrc);
    frame[lenWithoutCrc] = crc & 0xFF;
    frame[lenWithoutCrc + 1] = crc >> 8;

    while (RS485_SERIAL.available())
        RS485_SERIAL.read();
    RS485_SERIAL.write(frame, lenWithoutCrc + 2);
    RS485_SERIAL.flush();
    // Discard any transceiver echo; a Modbus slave must stay silent for 3.5 chars (~4 ms) before replying.
    delay(2);
    while (RS485_SERIAL.available())
        RS485_SERIAL.read();

    rxLen = 0;
    requestSentMs = millis();
}

void SondeModule::requestRegisters(uint8_t addr, uint16_t start, uint16_t count)
{
    uint8_t frame[8] = {addr, FN_READ_HOLDING, (uint8_t)(start >> 8), (uint8_t)start, (uint8_t)(count >> 8), (uint8_t)count};
    sendFrame(frame, 6);
}

void SondeModule::requestBrush()
{
    uint8_t frame[9] = {SONDE_MODBUS_ADDR, FN_WRITE_MULTIPLE, (uint8_t)(REG_BRUSH >> 8), (uint8_t)REG_BRUSH, 0x00, 0x00, 0x00};
    sendFrame(frame, 7);
}

SondeModule::Reply SondeModule::pollReply(uint8_t addr, size_t expectedLen)
{
    while (RS485_SERIAL.available() && rxLen < sizeof(rx))
        rx[rxLen++] = RS485_SERIAL.read();

    // Resync on the slave address in case the transceiver produced noise at power-up.
    size_t skip = 0;
    while (skip < rxLen && rx[skip] != addr)
        skip++;
    if (skip) {
        memmove(rx, rx + skip, rxLen - skip);
        rxLen -= skip;
    }

    if (rxLen >= 5 && (rx[1] & 0x80)) {
        LOG_WARN("RS485: Modbus exception 0x%02x from addr %u", rx[2], addr);
        return Reply::Failed;
    }
    if (rxLen >= expectedLen) {
        uint16_t crc = modbusCrc(rx, expectedLen - 2);
        bool crcOk = rx[expectedLen - 2] == (crc & 0xFF) && rx[expectedLen - 1] == (crc >> 8);
        bool shapeOk = rx[1] == FN_READ_HOLDING && rx[2] == expectedLen - 5;
        if (crcOk && shapeOk)
            return Reply::Ok;
        LOG_WARN("RS485: bad reply from addr %u (crc ok=%d, shape ok=%d, %u bytes)", addr, crcOk, shapeOk, rxLen);
        return Reply::Failed;
    }
    if (Throttle::isWithinTimespanMs(requestSentMs, REPLY_TIMEOUT_MS))
        return Reply::Pending;

    LOG_WARN("RS485: no reply from Modbus addr %u (%u bytes)", addr, rxLen);
    return Reply::Failed;
}

void SondeModule::decodeValues()
{
    // Floats arrive little-endian, the same byte order as the nRF52.
    for (uint8_t i = 0; i < VALUE_COUNT; i++)
        memcpy(&values[i], &rx[3 + 4 * i], sizeof(float));
    valuesOk = true;
}

void SondeModule::decodeWeather()
{
    const uint16_t raw = (uint16_t)((rx[3] << 8) | rx[4]);
    LOG_DEBUG("RS485: weather reg 0x%04x = 0x%04x (%u)", WX_REGS[weatherPart], raw, raw);
    // Physically impossible values are dropped (left NaN) so a stray "not ready" word never reaches the mesh.
    auto keep = [](float &field, float value, float low, float high) {
        if (value >= low && value <= high)
            field = value;
    };
    switch (weatherPart) {
    case 0:
        keep(weather.windSpeed, raw / 10.0f, 0, 75);
        break;
    case 1:
        keep(weather.windDir, raw, 0, 360);
        break;
    case 2:
        keep(weather.humidity, raw / 10.0f, 0, 100);
        break;
    case 3:
        keep(weather.airTemp, (int16_t)raw / 10.0f, -50, 70);
        break;
    case 4:
        keep(weather.pressureHpa, raw, 300, 1100);
        break;
    case 5:
        luxHigh = raw;
        luxHighOk = true;
        return;
    case 6:
        if (luxHighOk)
            keep(weather.lux, (float)(((uint32_t)luxHigh << 16) | raw), 0, 250000);
        break;
    case 7:
        keep(weather.rainMm, raw / 10.0f, 0, 1000);
        break;
    }
    weatherOk = weatherOk || !isnan(weather.windSpeed) || !isnan(weather.windDir) || !isnan(weather.humidity) ||
                !isnan(weather.airTemp) || !isnan(weather.pressureHpa) || !isnan(weather.lux) || !isnan(weather.rainMm);
}

void SondeModule::simulateValues()
{
    auto jitter = [](float base, float span) { return base + span * (float)random(-1000, 1001) / 1000.0f; };
    values[DO_MGL] = jitter(8.0f, 0.5f);
    values[TURBIDITY_NTU] = jitter(2.2f, 0.3f);
    values[COND_MS_CM] = jitter(0.41f, 0.01f);
    values[PH] = jitter(7.2f, 0.15f);
    values[TEMP_C] = jitter(19.0f, 1.0f);
    values[ORP_MV] = jitter(210.0f, 20.0f);
    values[CHL_UG_L] = jitter(1.5f, 0.5f);
    values[BGA] = jitter(300.0f, 100.0f);
    valuesOk = true;
    if (WX_MODBUS_ADDR) {
        weather = {jitter(3.0f, 2.0f),        jitter(180.0f, 90.0f), jitter(15.0f, 2.0f),
                   jitter(60.0f, 10.0f),      jitter(1013.0f, 5.0f), 0.0f,
                   jitter(20000.0f, 10000.0f)};
        weatherOk = true;
    }
}

size_t SondeModule::buildJson(char *buf, size_t cap, bool compact)
{
    JsonWriter j(buf, cap);
    j.integer("node_id", nodeDB->getNodeNum());
    j.integer("seq", seq);
    uint32_t now = getValidTime(RTCQualityDevice);
    if (now)
        j.integer("t", now);
    if (SONDE_SIMULATE)
        j.integer("sim", 1);

    if (valuesOk) {
        j.number("water_temp", values[TEMP_C], 2);
        j.number("ph", values[PH], 2);
        // The dashboard works in µS/cm; the Y4000 reports mS/cm.
        if (usable(values[COND_MS_CM]))
            j.number("conductivity", values[COND_MS_CM] * 1000.0f, 1);
        j.number("dissolved_oxygen", values[DO_MGL], 2);
        j.number("turbidity", values[TURBIDITY_NTU], 2);
        if (!compact) {
            j.number("orp", values[ORP_MV], 1);
            j.number("chl", values[CHL_UG_L], 2);
            j.number("bga", values[BGA], 0);
            if (statusOk)
                j.integer("status", status);
        }
    } else {
        j.str("err", "no_reply");
    }

    addPosition(j);
    return j.finish();
}

size_t SondeModule::buildWeatherJson(char *buf, size_t cap)
{
    JsonWriter j(buf, cap);
    j.integer("node_id", nodeDB->getNodeNum());
    j.str("type", "wx");
    j.integer("seq", weatherSeq);
    uint32_t now = getValidTime(RTCQualityDevice);
    if (now)
        j.integer("t", now);
    if (SONDE_SIMULATE)
        j.integer("sim", 1);
    j.number("wind_speed", weather.windSpeed, 1);
    j.number("wind_dir", weather.windDir, 0);
    j.number("air_temp", weather.airTemp, 1);
    j.number("humidity", weather.humidity, 1);
    j.number("pressure", weather.pressureHpa, 0);
    j.number("lux", weather.lux, 0);
    j.number("rain", weather.rainMm, 1);
    addPosition(j);
    return j.finish();
}

void SondeModule::sendJson(const char *json, size_t len)
{
    LOG_INFO("RS485: %s", json);
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = NODENUM_BROADCAST;
    p->want_ack = false;
    p->decoded.payload.size = len;
    memcpy(p->decoded.payload.bytes, json, len);
    // ccToPhone so a receiver plugged into this node over USB also gets its own readings.
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

void SondeModule::sendReport()
{
    char json[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];

    // A node with no sonde attached (e.g. a relay) shouldn't flood the mesh with error reports.
    if (valuesOk || !errorReported || !Throttle::isWithinTimespanMs(lastErrorReportMs, ERROR_REPORT_INTERVAL_MS)) {
        size_t len = buildJson(json, sizeof(json), false);
        if (len == 0)
            len = buildJson(json, sizeof(json), true);
        if (len) {
            sendJson(json, len);
            seq++;
        } else {
            LOG_ERROR("Sonde: report exceeds %d bytes, dropped", meshtastic_Constants_DATA_PAYLOAD_LEN);
        }
        errorReported = !valuesOk;
        lastErrorReportMs = millis();
    } else {
        LOG_DEBUG("Sonde: still no reply, error report suppressed");
    }

    if (weatherOk) {
        size_t len = buildWeatherJson(json, sizeof(json));
        if (len) {
            sendJson(json, len);
            weatherSeq++;
        }
    }
}

int32_t SondeModule::msUntilNextCycle()
{
    const uint64_t intervalMs = (uint64_t)SONDE_INTERVAL_SECS * 1000;
    uint32_t now = getValidTime(RTCQualityDevice);
    if (now == 0)
        return (int32_t)intervalMs;

    // Align to wall-clock boundaries (e.g. :00 and :30) so the swarm samples together, offset per node by a fixed
    // hash of its ID so nodes on the same boundary don't all transmit at once.
    const uint64_t spreadMs = std::min<uint64_t>(intervalMs / 2, 60 * 1000);
    const uint64_t offsetMs = spreadMs ? ((uint64_t)nodeDB->getNodeNum() * 2654435761u) % spreadMs : 0;
    const uint64_t nowMs = (uint64_t)now * 1000;
    uint64_t target = ((nowMs + intervalMs - offsetMs) / intervalMs) * intervalMs + offsetMs;
    // `now` only has 1 s resolution, so make sure we never re-fire the slot we just ran.
    if (target < nowMs + 2000)
        target += intervalMs;
    return (int32_t)(target - nowMs);
}

int32_t SondeModule::runOnce()
{
    switch (step) {
    case Step::Boot:
#if SONDE_POWER_PIN >= 0
        pinMode(SONDE_POWER_PIN, OUTPUT);
        digitalWrite(SONDE_POWER_PIN, LOW);
#endif
        LOG_INFO("RS485: Y4000 sonde at Modbus addr %u, SEN0657 weather station at addr %u (0 = none), every %us%s",
                 SONDE_MODBUS_ADDR, WX_MODBUS_ADDR, SONDE_INTERVAL_SECS, SONDE_SIMULATE ? " (SIMULATED)" : "");
        step = Step::Idle;
        return FIRST_CYCLE_DELAY_MS;

    case Step::Idle:
        valuesOk = false;
        statusOk = false;
        weatherOk = false;
        weatherPart = 0;
        luxHighOk = false;
        weather = {NAN, NAN, NAN, NAN, NAN, NAN, NAN};
        attempts = 0;
        if (SONDE_SIMULATE) {
            simulateValues();
            step = Step::Report;
            return STEP_MS;
        }
        if (powered) {
            step = Step::RequestValues;
            return STEP_MS;
        }
        LOG_INFO("Sonde: power on%s", CONTINUOUS ? " (continuous mode, stays powered)" : "");
        powerSonde(true);
        step = Step::Brush;
        return WARMUP_MS;

    case Step::Brush:
        if (SONDE_BRUSH)
            requestBrush();
        step = Step::RequestValues;
        return STABILIZE_MS;

    case Step::RequestValues:
        requestRegisters(SONDE_MODBUS_ADDR, REG_VALUES, VALUE_COUNT * 2);
        step = Step::AwaitValues;
        return REPLY_POLL_MS;

    case Step::AwaitValues:
        switch (pollReply(SONDE_MODBUS_ADDR, VALUES_REPLY_LEN)) {
        case Reply::Pending:
            return REPLY_POLL_MS;
        case Reply::Ok:
            decodeValues();
            step = Step::RequestStatus;
            return STEP_MS;
        case Reply::Failed:
            if (++attempts < MAX_ATTEMPTS) {
                step = Step::RequestValues;
                return RETRY_GAP_MS;
            }
            attempts = 0;
            step = WX_MODBUS_ADDR ? Step::RequestWeather : Step::Report;
            return WX_MODBUS_ADDR ? WX_GAP_MS : STEP_MS;
        }
        break;

    case Step::RequestStatus:
        requestRegisters(SONDE_MODBUS_ADDR, REG_STATUS, 1);
        step = Step::AwaitStatus;
        return REPLY_POLL_MS;

    case Step::AwaitStatus: {
        Reply r = pollReply(SONDE_MODBUS_ADDR, STATUS_REPLY_LEN);
        if (r == Reply::Pending)
            return REPLY_POLL_MS;
        if (r == Reply::Ok) {
            status = rx[3];
            statusOk = true;
        }
        attempts = 0;
        step = WX_MODBUS_ADDR ? Step::RequestWeather : Step::Report;
        return WX_MODBUS_ADDR ? WX_GAP_MS : STEP_MS;
    }

    case Step::RequestWeather:
        requestRegisters(WX_MODBUS_ADDR, WX_REGS[weatherPart], 1);
        step = Step::AwaitWeather;
        return REPLY_POLL_MS;

    case Step::AwaitWeather: {
        Reply r = pollReply(WX_MODBUS_ADDR, 5 + 2);
        if (r == Reply::Pending)
            return REPLY_POLL_MS;
        if (r == Reply::Failed && ++attempts < WX_MAX_ATTEMPTS) {
            step = Step::RequestWeather;
            return WX_GAP_MS;
        }
        if (r == Reply::Ok)
            decodeWeather();
        attempts = 0;
        // A part that never answered leaves its fields NaN, which the JSON writer omits.
        step = ++weatherPart < WX_READ_COUNT ? Step::RequestWeather : Step::Report;
        return step == Step::Report ? STEP_MS : WX_GAP_MS;
    }

    case Step::Report:
        if (!SONDE_SIMULATE && !CONTINUOUS) {
            powerSonde(false);
            LOG_INFO("Sonde: power off");
        }
        sendReport();
        step = Step::Idle;
        return msUntilNextCycle();
    }
    return STEP_MS;
}

#endif
