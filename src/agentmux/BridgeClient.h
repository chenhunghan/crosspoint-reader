#pragma once
#if AGENTMUX

#include <HalMemory.h>
#include <WebSocketsClient.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace agentmux {

enum class SessionState : uint8_t { Unknown, Working, Blocked, Idle, Exited };

enum class LinkState : uint8_t {
  Stopped,     // begin() not called, or stopped after an auth failure
  Connecting,  // waiting for TCP/WebSocket (with backoff)
  Handshake,   // socket open, hello sent, waiting for "ok"
  Online,      // "ok" received
  AuthFailed,  // bridge rejected the token; no reconnects until begin()
};

constexpr int MAX_SESSIONS = 16;
constexpr int MAX_SCREEN_LINES = 40;
constexpr int MAX_COLS = 64;
// A line holds up to MAX_COLS columns; CJK glyphs are 3 UTF-8 bytes but two
// columns wide, so 160 bytes covers any line the bridge wraps to MAX_COLS.
constexpr int LINE_BYTES = 160;
constexpr int MAX_PERM_DETAIL = 8;
constexpr int MAX_PERM_OPTIONS = 6;
constexpr int ID_BYTES = 24;

struct Session {
  char sid[ID_BYTES];
  char agent[16];
  char title[80];
  char cwd[80];
  char req[ID_BYTES];
  uint32_t since;
  SessionState state;
};

struct PermOption {
  char key[8];
  char label[56];
};

struct Perm {
  bool active;
  char sid[ID_BYTES];
  char req[ID_BYTES];
  char title[80];
  uint8_t detailCount;
  char detail[MAX_PERM_DETAIL][LINE_BYTES];
  uint8_t optionCount;
  PermOption options[MAX_PERM_OPTIONS];
};

struct ScreenBuffer {
  char sid[ID_BYTES];
  uint32_t seq;
  uint8_t lineCount;
  char lines[MAX_SCREEN_LINES][LINE_BYTES];
};

// Everything the UI reads. POD so it can live in a raw PSRAM block. Written
// only by the loop task (under RenderLock); read by the render task while it
// holds the render lock, and by the loop task freely.
struct Model {
  uint8_t sessionCount;
  Session sessions[MAX_SESSIONS];
  Perm perms[MAX_SESSIONS];  // slot per pending dialog, matched by sid
  ScreenBuffer screen;       // latest frame of the subscribed session
  char bridgeHost[48];       // from "ok"
  char lastError[96];        // last "err" frame, for the status line
};

// Device side of the agentmux WebSocket protocol (docs/protocol.md, v1).
// Owns the socket and a small model of the bridge state. Not thread-safe by
// itself: call everything from the activity loop task.
class BridgeClient {
 public:
  enum Dirty : uint8_t {
    DIRTY_LINK = 1 << 0,
    DIRTY_SESSIONS = 1 << 1,
    DIRTY_SCREEN = 1 << 2,
    DIRTY_PERM = 1 << 3,
    DIRTY_RUNS = 1 << 4,    // the run records, an action's answer
    DIRTY_REPORT = 1 << 5,  // a requested report arrived
  };

  BridgeClient() = default;
  BridgeClient(const BridgeClient&) = delete;
  BridgeClient& operator=(const BridgeClient&) = delete;
  ~BridgeClient();

  // Allocates the model (PSRAM when available). False on OOM.
  bool init();

  // (Re)connects to ws://host:port/device. cols/rows go into hello.
  void begin(const char* host, uint16_t port, const char* token, uint16_t cols, uint16_t rows);
  void stop();
  void loop();

  LinkState link() const { return linkState; }
  const Model& model() const { return *modelPtr; }
  // Returns and clears the accumulated Dirty bits.
  uint8_t consumeDirty();

  // Remembered and re-sent after reconnects; "" unsubscribes.
  void subscribe(const char* sid);
  bool requestList();
  bool decide(const char* sid, const char* req, const char* choice);
  bool sendInput(const char* sid, const char* text, bool submit);
  bool sendKey(const char* sid, const char* key);

  // The mahler run records the bridge serves (docs/protocol.md, "Runs"): the
  // last `runs` frame as it came, "" until the first; the mahler screen
  // (mahler_ui, ../ui-ffi) reads it. `runsAgeMs` is how long ago it came.
  const std::string& runsFrame() const { return runsJson; }
  unsigned long runsAgeMs() const { return millis() - runsAtMs; }
  // Stop, mark read or reclaim a run; the answer comes as lastAct().
  bool sendAct(const char* name, const char* action);
  // Asks for a run's report; it comes as reportText() with DIRTY_REPORT.
  bool requestReport(const char* name);
  const std::string& lastAct() const { return actMessage; }
  bool lastActOk() const { return actOk; }
  const std::string& reportName() const { return reportFor; }
  const std::string& reportText() const { return reportBody; }

  const Session* findSession(const char* sid) const;
  const Perm* findPerm(const char* sid) const;

  static const char* stateLabel(SessionState state);
  static const char* linkLabel(LinkState state);

 private:
  WebSocketsClient ws;
  HalMemory::PsramBuffer modelPsram;
  std::string runsJson;
  unsigned long runsAtMs = 0;
  std::string actMessage;
  bool actOk = true;
  std::string reportFor;
  std::string reportBody;
  std::unique_ptr<Model> modelInternal;
  Model* modelPtr = nullptr;

  LinkState linkState = LinkState::Stopped;
  uint8_t dirty = 0;
  bool started = false;

  char token[12] = {0};
  char subscribedSid[ID_BYTES] = {0};
  uint16_t cols = 48;
  uint16_t rows = 20;

  uint32_t backoffMs = 0;
  unsigned long backoffSince = 0;
  unsigned long lastRxMs = 0;
  unsigned long lastPingMs = 0;
  unsigned long handshakeSince = 0;
  unsigned long lastLoopMs = 0;

  // Outgoing frame scratch; sized for an input frame carrying a full reply.
  static constexpr size_t TX_BYTES = 1024;
  char tx[TX_BYTES] = {0};

  void onWsEvent(WStype_t type, const uint8_t* payload, size_t length);
  void handleFrame(const uint8_t* payload, size_t length);
  bool sendTx(size_t len);
  void sendHello();
  void sendSubscribe();
  void setLink(LinkState state);
  void resetBackoff();
  Perm* permSlotFor(const char* sid, bool create);
};

}  // namespace agentmux

#endif  // AGENTMUX
