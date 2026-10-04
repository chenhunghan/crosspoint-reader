#if AGENTMUX

#include "BridgeClient.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "activities/RenderLock.h"

namespace agentmux {

namespace {

constexpr uint32_t PING_INTERVAL_MS = 15000;
constexpr uint32_t RX_TIMEOUT_MS = 45000;
constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 10000;
constexpr uint32_t BACKOFF_MIN_MS = 1000;
constexpr uint32_t BACKOFF_MAX_MS = 30000;
constexpr uint32_t PAUSE_GAP_MS = 5000;

// Screen frames carry up to 40 lines; keep their JSON pool out of internal RAM.
class PsramJsonAllocator final : public ArduinoJson::Allocator {
 public:
  void* allocate(size_t size) override {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(size);
  }
  void deallocate(void* ptr) override { free(ptr); }
  void* reallocate(void* ptr, size_t newSize) override {
    void* p = heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : realloc(ptr, newSize);
  }
};

PsramJsonAllocator jsonAllocator;

// Copies src into dst (capacity cap), never splitting a UTF-8 sequence.
void copyUtf8(char* dst, const size_t cap, const char* src) {
  if (cap == 0) return;
  if (!src) src = "";
  size_t n = strnlen(src, cap - 1);
  if (src[n] != '\0') {
    // Truncated: back off continuation bytes so the cut lands on a boundary.
    while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0) == 0x80) --n;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

SessionState parseState(const char* s) {
  if (!s) return SessionState::Unknown;
  if (strcmp(s, "working") == 0) return SessionState::Working;
  if (strcmp(s, "blocked") == 0) return SessionState::Blocked;
  if (strcmp(s, "idle") == 0) return SessionState::Idle;
  if (strcmp(s, "exited") == 0) return SessionState::Exited;
  return SessionState::Unknown;
}

}  // namespace

BridgeClient::~BridgeClient() {
  stop();
  // ~WebSocketsClient disconnects again; keep that from calling back into us.
  ws.onEvent(nullptr);
}

bool BridgeClient::init() {
  if (modelPtr) return true;
  modelPsram = HalMemory::allocatePsram(sizeof(Model));
  if (modelPsram) {
    modelPtr = reinterpret_cast<Model*>(modelPsram.get());
  } else {
    modelInternal = makeUniqueNoThrow<Model>();
    if (!modelInternal) {
      LOG_ERR("AMUX", "OOM: model (%u bytes)", static_cast<unsigned>(sizeof(Model)));
      return false;
    }
    modelPtr = modelInternal.get();
  }
  memset(modelPtr, 0, sizeof(Model));
  LOG_DBG("AMUX", "Model %u bytes in %s", static_cast<unsigned>(sizeof(Model)), modelPsram ? "PSRAM" : "DRAM");
  return true;
}

void BridgeClient::begin(const char* host, const uint16_t port, const char* tok, const uint16_t c, const uint16_t r) {
  if (!modelPtr) return;
  if (started) stop();
  copyUtf8(token, sizeof(token), tok);
  cols = c;
  rows = r;
  modelPtr->lastError[0] = '\0';
  modelPtr->bridgeHost[0] = '\0';

  LOG_INF("AMUX", "Connecting to ws://%s:%u/device (%ux%u)", host, port, cols, rows);
  ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) { onWsEvent(type, payload, length); });
  resetBackoff();
  // Empty protocol: no Sec-WebSocket-Protocol header for the bridge to reject.
  ws.begin(host, port, "/device", "");
  started = true;
  setLink(LinkState::Connecting);
}

void BridgeClient::stop() {
  if (started) {
    LOG_INF("AMUX", "Disconnecting");
    ws.disconnect();
  }
  started = false;
  if (linkState != LinkState::AuthFailed) setLink(LinkState::Stopped);
}

void BridgeClient::resetBackoff() {
  backoffMs = BACKOFF_MIN_MS;
  backoffSince = millis();
  ws.setReconnectInterval(backoffMs);
}

void BridgeClient::setLink(const LinkState state) {
  if (linkState == state) return;
  linkState = state;
  dirty |= DIRTY_LINK;
}

uint8_t BridgeClient::consumeDirty() {
  const uint8_t d = dirty;
  dirty = 0;
  return d;
}

void BridgeClient::loop() {
  if (!started) return;
  const unsigned long entered = millis();
  if (lastLoopMs != 0 && entered - lastLoopMs > PAUSE_GAP_MS) {
    // Not polled while a keyboard sat on top; don't count that as silence.
    lastRxMs = lastPingMs = handshakeSince = entered;
  }
  lastLoopMs = entered;
  ws.loop();
  if (linkState == LinkState::AuthFailed) {
    // Rejected token: stop reconnecting until the user edits it.
    ws.disconnect();
    started = false;
    return;
  }

  const unsigned long now = millis();
  switch (linkState) {
    case LinkState::Connecting:
      // The library retries on its own every reconnect interval; widen that
      // interval the longer the bridge stays unreachable.
      if (now - backoffSince >= backoffMs && backoffMs < BACKOFF_MAX_MS) {
        backoffMs = std::min<uint32_t>(backoffMs * 2, BACKOFF_MAX_MS);
        backoffSince = now;
        ws.setReconnectInterval(backoffMs);
      }
      break;
    case LinkState::Handshake:
      if (now - handshakeSince > HANDSHAKE_TIMEOUT_MS) {
        LOG_ERR("AMUX", "No ok after hello; reconnecting");
        ws.disconnect();
      }
      break;
    case LinkState::Online:
      if (now - lastRxMs > RX_TIMEOUT_MS) {
        LOG_ERR("AMUX", "Bridge silent for %lu ms; reconnecting", now - lastRxMs);
        ws.disconnect();
      } else if (now - lastPingMs >= PING_INTERVAL_MS) {
        lastPingMs = now;
        sendTx(snprintf(tx, sizeof(tx), "{\"t\":\"ping\"}"));
      }
      break;
    default:
      break;
  }
}

void BridgeClient::onWsEvent(const WStype_t type, const uint8_t* payload, const size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      LOG_INF("AMUX", "Socket open");
      handshakeSince = lastRxMs = millis();
      setLink(LinkState::Handshake);
      sendHello();
      break;
    case WStype_DISCONNECTED:
      LOG_INF("AMUX", "Socket closed");
      if (linkState != LinkState::AuthFailed && linkState != LinkState::Stopped) {
        setLink(LinkState::Connecting);
        backoffSince = millis();
      }
      break;
    case WStype_TEXT:
      lastRxMs = millis();
      handleFrame(payload, length);
      break;
    case WStype_ERROR:
      LOG_ERR("AMUX", "Socket error");
      break;
    default:
      break;
  }
}

bool BridgeClient::sendTx(const size_t len) {
  if (len == 0 || len >= sizeof(tx)) {
    LOG_ERR("AMUX", "Frame too large (%u)", static_cast<unsigned>(len));
    return false;
  }
  if (linkState != LinkState::Online && linkState != LinkState::Handshake) return false;
  return ws.sendTXT(tx, len);
}

void BridgeClient::sendHello() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char dev[24];
  snprintf(dev, sizeof(dev), "metalio-%02x%02x", mac[4], mac[5]);

  JsonDocument doc(&jsonAllocator);
  doc["t"] = "hello";
  doc["v"] = 1;
  doc["dev"] = dev;
  doc["token"] = token;
  doc["cols"] = cols;
  doc["rows"] = rows;
  sendTx(serializeJson(doc, tx, sizeof(tx)));
  LOG_INF("AMUX", "hello dev=%s cols=%u rows=%u", dev, cols, rows);
}

void BridgeClient::sendSubscribe() {
  JsonDocument doc(&jsonAllocator);
  doc["t"] = "subscribe";
  doc["sid"] = subscribedSid;
  sendTx(serializeJson(doc, tx, sizeof(tx)));
}

void BridgeClient::subscribe(const char* sid) {
  copyUtf8(subscribedSid, sizeof(subscribedSid), sid);
  if (subscribedSid[0] != '\0' && modelPtr) {
    // Drop the previous session's frame so it never flashes under the new one.
    // Callers with a sid run unlocked (onEnter); unsubscribing ("") runs from
    // onExit with the render lock already held, so it must not lock here.
    RenderLock lock;
    copyUtf8(modelPtr->screen.sid, sizeof(modelPtr->screen.sid), subscribedSid);
    modelPtr->screen.lineCount = 0;
    modelPtr->screen.seq = 0;
    dirty |= DIRTY_SCREEN;
  }
  if (linkState == LinkState::Online) sendSubscribe();
}

bool BridgeClient::requestList() {
  if (linkState != LinkState::Online) return false;
  return sendTx(snprintf(tx, sizeof(tx), "{\"t\":\"list\"}"));
}

bool BridgeClient::decide(const char* sid, const char* req, const char* choice) {
  if (linkState != LinkState::Online) return false;
  JsonDocument doc(&jsonAllocator);
  doc["t"] = "decide";
  doc["sid"] = sid;
  doc["req"] = req;
  doc["choice"] = choice;
  if (!sendTx(serializeJson(doc, tx, sizeof(tx)))) return false;
  LOG_INF("AMUX", "decide sid=%s req=%s choice=%s", sid, req, choice);
  // Hide the dialog right away; perm_closed follows from the bridge.
  RenderLock lock;
  if (Perm* perm = permSlotFor(sid, false)) {
    if (strcmp(perm->req, req) == 0) perm->active = false;
  }
  dirty |= DIRTY_PERM;
  return true;
}

bool BridgeClient::sendInput(const char* sid, const char* text, const bool submit) {
  if (linkState != LinkState::Online) return false;
  JsonDocument doc(&jsonAllocator);
  doc["t"] = "input";
  doc["sid"] = sid;
  doc["text"] = text;
  doc["submit"] = submit;
  return sendTx(serializeJson(doc, tx, sizeof(tx)));
}

bool BridgeClient::sendKey(const char* sid, const char* key) {
  if (linkState != LinkState::Online) return false;
  JsonDocument doc(&jsonAllocator);
  doc["t"] = "keys";
  doc["sid"] = sid;
  doc["keys"].add(key);
  return sendTx(serializeJson(doc, tx, sizeof(tx)));
}

const Session* BridgeClient::findSession(const char* sid) const {
  if (!modelPtr || !sid) return nullptr;
  for (int i = 0; i < modelPtr->sessionCount; ++i) {
    if (strcmp(modelPtr->sessions[i].sid, sid) == 0) return &modelPtr->sessions[i];
  }
  return nullptr;
}

const Perm* BridgeClient::findPerm(const char* sid) const {
  if (!modelPtr || !sid) return nullptr;
  for (const Perm& perm : modelPtr->perms) {
    if (perm.active && strcmp(perm.sid, sid) == 0) return &perm;
  }
  return nullptr;
}

Perm* BridgeClient::permSlotFor(const char* sid, const bool create) {
  Perm* freeSlot = nullptr;
  for (Perm& perm : modelPtr->perms) {
    if (perm.sid[0] != '\0' && strcmp(perm.sid, sid) == 0) return &perm;
    if (!freeSlot && (!perm.active || perm.sid[0] == '\0')) freeSlot = &perm;
  }
  if (!create) return nullptr;
  if (!freeSlot) freeSlot = &modelPtr->perms[0];  // more dialogs than sessions: recycle
  memset(freeSlot, 0, sizeof(Perm));
  copyUtf8(freeSlot->sid, sizeof(freeSlot->sid), sid);
  return freeSlot;
}

void BridgeClient::handleFrame(const uint8_t* payload, const size_t length) {
  JsonDocument doc(&jsonAllocator);
  const DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    LOG_ERR("AMUX", "Bad frame (%s), %u bytes", error.c_str(), static_cast<unsigned>(length));
    return;
  }
  const char* t = doc["t"] | "";
  bool needSubscribe = false;
  bool needList = false;

  {
    // The render task reads the model under the render lock.
    RenderLock lock;
    Model& m = *modelPtr;

    if (strcmp(t, "ok") == 0) {
      copyUtf8(m.bridgeHost, sizeof(m.bridgeHost), doc["host"] | "");
      m.lastError[0] = '\0';
      setLink(LinkState::Online);
      backoffMs = BACKOFF_MIN_MS;
      ws.setReconnectInterval(backoffMs);
      lastPingMs = millis();
      needSubscribe = subscribedSid[0] != '\0';
      LOG_INF("AMUX", "Online (bridge %s)", m.bridgeHost);
    } else if (strcmp(t, "sessions") == 0) {
      const JsonArrayConst items = doc["items"].as<JsonArrayConst>();
      uint8_t count = 0;
      for (const JsonObjectConst item : items) {
        if (count >= MAX_SESSIONS) break;
        Session& s = m.sessions[count++];
        copyUtf8(s.sid, sizeof(s.sid), item["sid"] | "");
        copyUtf8(s.agent, sizeof(s.agent), item["agent"] | "");
        copyUtf8(s.title, sizeof(s.title), item["title"] | "");
        copyUtf8(s.cwd, sizeof(s.cwd), item["cwd"] | "");
        copyUtf8(s.req, sizeof(s.req), item["req"] | "");
        s.since = item["since"] | 0u;
        s.state = parseState(item["state"] | "");
      }
      m.sessionCount = count;
      // Dialogs of sessions that left the list are gone too.
      for (Perm& perm : m.perms) {
        if (perm.sid[0] == '\0') continue;
        bool present = false;
        for (int i = 0; i < count; ++i) {
          if (strcmp(m.sessions[i].sid, perm.sid) == 0) present = true;
        }
        if (!present) {
          memset(&perm, 0, sizeof(Perm));
          dirty |= DIRTY_PERM;
        }
      }
      dirty |= DIRTY_SESSIONS;
    } else if (strcmp(t, "status") == 0) {
      const char* sid = doc["sid"] | "";
      Session* s = const_cast<Session*>(findSession(sid));
      if (s) {
        s->state = parseState(doc["state"] | "");
        s->since = doc["since"] | s->since;
        if (s->state != SessionState::Blocked) {
          if (Perm* perm = permSlotFor(sid, false)) {
            perm->active = false;
            dirty |= DIRTY_PERM;
          }
        }
        dirty |= DIRTY_SESSIONS;
      } else {
        needList = true;
      }
    } else if (strcmp(t, "screen") == 0) {
      const char* sid = doc["sid"] | "";
      if (subscribedSid[0] != '\0' && strcmp(sid, subscribedSid) == 0) {
        const JsonArrayConst lines = doc["lines"].as<JsonArrayConst>();
        const size_t total = lines.size();
        const size_t skip = total > MAX_SCREEN_LINES ? total - MAX_SCREEN_LINES : 0;
        size_t index = 0;
        uint8_t count = 0;
        for (const JsonVariantConst line : lines) {
          if (index++ < skip) continue;
          copyUtf8(m.screen.lines[count++], LINE_BYTES, line | "");
        }
        copyUtf8(m.screen.sid, sizeof(m.screen.sid), sid);
        m.screen.lineCount = count;
        m.screen.seq = doc["seq"] | 0u;
        dirty |= DIRTY_SCREEN;
      }
    } else if (strcmp(t, "perm") == 0) {
      const char* sid = doc["sid"] | "";
      if (Perm* perm = permSlotFor(sid, true)) {
        perm->active = true;
        copyUtf8(perm->req, sizeof(perm->req), doc["req"] | "");
        copyUtf8(perm->title, sizeof(perm->title), doc["title"] | "");
        perm->detailCount = 0;
        for (const JsonVariantConst line : doc["detail"].as<JsonArrayConst>()) {
          if (perm->detailCount >= MAX_PERM_DETAIL) break;
          copyUtf8(perm->detail[perm->detailCount++], LINE_BYTES, line | "");
        }
        perm->optionCount = 0;
        for (const JsonObjectConst option : doc["options"].as<JsonArrayConst>()) {
          if (perm->optionCount >= MAX_PERM_OPTIONS) break;
          PermOption& o = perm->options[perm->optionCount++];
          copyUtf8(o.key, sizeof(o.key), option["k"] | "");
          copyUtf8(o.label, sizeof(o.label), option["label"] | "");
        }
        if (Session* s = const_cast<Session*>(findSession(sid))) {
          s->state = SessionState::Blocked;
          copyUtf8(s->req, sizeof(s->req), perm->req);
          dirty |= DIRTY_SESSIONS;
        }
        LOG_INF("AMUX", "perm sid=%s req=%s options=%u", sid, perm->req, perm->optionCount);
        dirty |= DIRTY_PERM;
      }
    } else if (strcmp(t, "perm_closed") == 0) {
      const char* req = doc["req"] | "";
      if (Perm* perm = permSlotFor(doc["sid"] | "", false)) {
        if (req[0] == '\0' || strcmp(perm->req, req) == 0) {
          perm->active = false;
          dirty |= DIRTY_PERM;
        }
      }
    } else if (strcmp(t, "err") == 0) {
      const char* code = doc["code"] | "";
      snprintf(m.lastError, sizeof(m.lastError), "%s: %s", code, doc["msg"] | "");
      LOG_ERR("AMUX", "Bridge error %s", m.lastError);
      if (strcmp(code, "auth") == 0) setLink(LinkState::AuthFailed);
      dirty |= DIRTY_LINK;
    } else if (strcmp(t, "pong") != 0) {
      LOG_DBG("AMUX", "Ignoring frame t=%s", t);
    }
  }

  if (needSubscribe) sendSubscribe();
  if (needList) requestList();
}

const char* BridgeClient::stateLabel(const SessionState state) {
  switch (state) {
    case SessionState::Blocked:
      return "[BLOCKED]";
    case SessionState::Working:
      return "[WORKING]";
    case SessionState::Idle:
      return "[IDLE]";
    case SessionState::Exited:
      return "[EXITED]";
    case SessionState::Unknown:
    default:
      return "[?]";
  }
}

const char* BridgeClient::linkLabel(const LinkState state) {
  switch (state) {
    case LinkState::Online:
      return "Online";
    case LinkState::Connecting:
      return "Connecting...";
    case LinkState::Handshake:
      return "Pairing...";
    case LinkState::AuthFailed:
      return "Token rejected";
    case LinkState::Stopped:
    default:
      return "Offline";
  }
}

}  // namespace agentmux

#endif  // AGENTMUX
