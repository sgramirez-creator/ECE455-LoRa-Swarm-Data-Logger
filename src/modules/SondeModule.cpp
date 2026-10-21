#include "SondeModule.h"

#ifdef HAS_SONDE

#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Throttle.h"
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

static constexpr int32_t FIRST_CYCLE_DELAY_MS = 45 * 1000;
static constexpr int32_t WARMUP_MS = 2000;         // Y4000 answers ~1 s after power-up
static constexpr int32_t STABILIZE_MS = 50 * 1000; // EnviroDIY ModularSensors Y4000 stabilisation time
static constexpr uint32_t REPLY_TIMEOUT_MS = 1000;
static constexpr int32_t REPLY_POLL_MS = 50;
static constexpr int32_t RETRY_GAP_MS = 500;
static constexpr int32_t STEP_MS = 10;
static constexpr uint8_t MAX_ATTEMPTS = 3;

static constexpr uint8_t FN_READ_HOLDING = 0x03;
static constexpr uint8_t FN_WRITE_MULTIPLE = 0x10;
// EnviroDIY YosemitechModbus: the sonde's 8 floats start at 0x2601 (the manual's 0x2600 is wrong).
static constexpr uint16_t REG_VALUES = 0x2601;
static constexpr uint16_t REG_STATUS = 0x0800;
static constexpr uint16_t REG_BRUSH = 0x2F00;
static constexpr size_t VALUES_REPLY_LEN = 5 + 8 * 4; // addr, fn, byte count, 8 floats, crc16
static constexpr size_t STATUS_REPLY_LEN = 5 + 2;

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
} // namespace

SondeModule::SondeModule() : SinglePortModule("sonde", meshtastic_PortNum_PRIVATE_APP), concurrency::OSThread("Sonde") {}

void SondeModule::powerSonde(bool on)
{
#if SONDE_POWER_PIN >= 0
    digitalWrite(SONDE_POWER_PIN, on ? HIGH : LOW);
#endif
    // The UART is only open while the sonde is powered so the UARTE doesn't hold HFCLK on between cycles.
    if (on)
        SONDE_SERIAL.begin(SONDE_BAUD);
    else
        SONDE_SERIAL.end();
}

void SondeModule::sendFrame(uint8_t *frame, size_t lenWithoutCrc)
{
    uint16_t crc = modbusCrc(frame, lenWithoutCrc);
    frame[lenWithoutCrc] = crc & 0xFF;
    frame[lenWithoutCrc + 1] = crc >> 8;

    while (SONDE_SERIAL.available())
        SONDE_SERIAL.read();
    SONDE_SERIAL.write(frame, lenWithoutCrc + 2);
    SONDE_SERIAL.flush();
    // Discard any transceiver echo; a Modbus slave must stay silent for 3.5 chars (~4 ms) before replying.
    delay(2);
    while (SONDE_SERIAL.available())
        SONDE_SERIAL.read();

    rxLen = 0;
    requestSentMs = millis();
}

void SondeModule::requestRegisters(uint16_t start, uint16_t count)
{
    uint8_t frame[8] = {SONDE_MODBUS_ADDR, FN_READ_HOLDING,       (uint8_t)(start >> 8),
                        (uint8_t)start,    (uint8_t)(count >> 8), (uint8_t)count};
    sendFrame(frame, 6);
}

void SondeModule::requestBrush()
{
    uint8_t frame[9] = {SONDE_MODBUS_ADDR, FN_WRITE_MULTIPLE, (uint8_t)(REG_BRUSH >> 8), (uint8_t)REG_BRUSH, 0x00, 0x00, 0x00};
    sendFrame(frame, 7);
}

SondeModule::Reply SondeModule::pollReply(size_t expectedLen)
{
    while (SONDE_SERIAL.available() && rxLen < sizeof(rx))
        rx[rxLen++] = SONDE_SERIAL.read();

    // Resync on our slave address in case the transceiver produced noise at power-up.
    size_t skip = 0;
    while (skip < rxLen && rx[skip] != SONDE_MODBUS_ADDR)
        skip++;
    if (skip) {
        memmove(rx, rx + skip, rxLen - skip);
        rxLen -= skip;
    }

    if (rxLen >= 5 && (rx[1] & 0x80)) {
        LOG_WARN("Sonde: Modbus exception 0x%02x", rx[2]);
        return Reply::Failed;
    }
    if (rxLen >= expectedLen) {
        uint16_t crc = modbusCrc(rx, expectedLen - 2);
        bool crcOk = rx[expectedLen - 2] == (crc & 0xFF) && rx[expectedLen - 1] == (crc >> 8);
        bool shapeOk = rx[1] == FN_READ_HOLDING && rx[2] == expectedLen - 5;
        if (crcOk && shapeOk)
            return Reply::Ok;
        LOG_WARN("Sonde: bad reply (crc ok=%d, shape ok=%d, %u bytes)", crcOk, shapeOk, rxLen);
        return Reply::Failed;
    }
    if (Throttle::isWithinTimespanMs(requestSentMs, REPLY_TIMEOUT_MS))
        return Reply::Pending;

    LOG_WARN("Sonde: no reply from Modbus addr %u (%u bytes)", SONDE_MODBUS_ADDR, rxLen);
    return Reply::Failed;
}

void SondeModule::decodeValues()
{
    // Floats arrive little-endian, the same byte order as the nRF52.
    for (uint8_t i = 0; i < VALUE_COUNT; i++)
        memcpy(&values[i], &rx[3 + 4 * i], sizeof(float));
    valuesOk = true;
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

    if (localPosition.latitude_i != 0 || localPosition.longitude_i != 0) {
        j.fixed("lat", localPosition.latitude_i / 10, 6);
        j.fixed("lon", localPosition.longitude_i / 10, 6);
    }
    return j.finish();
}

void SondeModule::sendReport()
{
    char json[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
    size_t len = buildJson(json, sizeof(json), false);
    if (len == 0)
        len = buildJson(json, sizeof(json), true);
    if (len == 0) {
        LOG_ERROR("Sonde: report exceeds %d bytes, dropped", meshtastic_Constants_DATA_PAYLOAD_LEN);
        return;
    }
    LOG_INFO("Sonde: %s", json);

    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = NODENUM_BROADCAST;
    p->want_ack = false;
    p->decoded.payload.size = len;
    memcpy(p->decoded.payload.bytes, json, len);
    // ccToPhone so a receiver plugged into this node over USB also gets its own readings.
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    seq++;
}

int32_t SondeModule::msUntilNextCycle()
{
    // Align to wall-clock boundaries (e.g. :00 and :30) once time is known, so every node samples together.
    uint32_t now = getValidTime(RTCQualityDevice);
    if (now == 0)
        return SONDE_INTERVAL_SECS * 1000;
    return (int32_t)(SONDE_INTERVAL_SECS - now % SONDE_INTERVAL_SECS) * 1000;
}

int32_t SondeModule::runOnce()
{
    switch (step) {
    case Step::Boot:
#if SONDE_POWER_PIN >= 0
        pinMode(SONDE_POWER_PIN, OUTPUT);
        digitalWrite(SONDE_POWER_PIN, LOW);
#endif
        LOG_INFO("Sonde: Y4000 at Modbus addr %u, every %us%s", SONDE_MODBUS_ADDR, SONDE_INTERVAL_SECS,
                 SONDE_SIMULATE ? " (SIMULATED)" : "");
        step = Step::Idle;
        return FIRST_CYCLE_DELAY_MS;

    case Step::Idle:
        valuesOk = false;
        statusOk = false;
        attempts = 0;
        if (SONDE_SIMULATE) {
            simulateValues();
            step = Step::Report;
            return STEP_MS;
        }
        LOG_INFO("Sonde: power on");
        powerSonde(true);
        step = Step::Brush;
        return WARMUP_MS;

    case Step::Brush:
        if (SONDE_BRUSH)
            requestBrush();
        step = Step::RequestValues;
        return STABILIZE_MS;

    case Step::RequestValues:
        requestRegisters(REG_VALUES, VALUE_COUNT * 2);
        step = Step::AwaitValues;
        return REPLY_POLL_MS;

    case Step::AwaitValues:
        switch (pollReply(VALUES_REPLY_LEN)) {
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
            step = Step::Report;
            return STEP_MS;
        }
        break;

    case Step::RequestStatus:
        requestRegisters(REG_STATUS, 1);
        step = Step::AwaitStatus;
        return REPLY_POLL_MS;

    case Step::AwaitStatus: {
        Reply r = pollReply(STATUS_REPLY_LEN);
        if (r == Reply::Pending)
            return REPLY_POLL_MS;
        if (r == Reply::Ok) {
            status = rx[3];
            statusOk = true;
        }
        step = Step::Report;
        return STEP_MS;
    }

    case Step::Report:
        if (!SONDE_SIMULATE) {
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
