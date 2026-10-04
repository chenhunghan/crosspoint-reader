// CrossPoint simulator (native): a scripted in-process bridge that speaks
// docs/protocol.md v1, so snapshot scenarios run without a network. Same
// cast as the browser's demo bridge (sim/web/demo-bridge.js), simplified.
#include <ArduinoJson.h>
#include <Logging.h>
#include <WebSocketsClient.h>

#include <map>
#include <string>
#include <vector>

#include "SimSocket.h"
#include "native_fake_bridge.h"

namespace {

struct FakeSession {
  std::string sid, agent, title, cwd, state, req;
  std::vector<std::string> lines;
};

struct Pending {
  unsigned long at;
  std::string frame;  // empty = "deliver open"
};

struct Conn {
  WebSocketsClient* owner = nullptr;
  bool open = false;
  bool authed = false;
  std::string subscribed;
  std::vector<Pending> pending;
};

std::map<int, Conn> conns;
int nextId = 1;
int seq = 0;
std::vector<FakeSession> sessions;
std::vector<std::string> sentLog;

void resetSessions() {
  sessions = {
      {"s1", "claude", "api: fix auth", "~/src/api", "blocked", "r7",
       {"> run the auth tests and fix what fails", "", "* Reading src/auth/session.ts", "* Reading test/auth.spec.ts",
        "", "The session cookie is not refreshed on", "token rotation. I'll run the suite:", "",
        "Bash(npm test -- --watch=false)"}},
      {"s2", "codex", "web: dark mode", "~/src/web", "working", "",
       {"> add a dark mode toggle to settings", "", "- Updated src/theme.ts (+42 -3)",
        "- Updated src/Settings.tsx (+18 -1)", "", "Running npm run build..."}},
      {"s3", "claude", "infra: terraform", "~/src/infra", "idle", "",
       {"> plan the staging changes", "", "Plan: 2 to add, 1 to change, 0 to destroy.", "", "Waiting for input."}},
  };
}

FakeSession* find(const std::string& sid) {
  for (auto& s : sessions) {
    if (s.sid == sid) return &s;
  }
  return nullptr;
}

std::string sessionsFrame() {
  JsonDocument doc;
  doc["t"] = "sessions";
  JsonArray items = doc["items"].to<JsonArray>();
  for (const auto& s : sessions) {
    JsonObject o = items.add<JsonObject>();
    o["sid"] = s.sid;
    o["agent"] = s.agent;
    o["title"] = s.title;
    o["cwd"] = s.cwd;
    o["state"] = s.state;
    o["since"] = 1791100000;
    if (!s.req.empty()) o["req"] = s.req;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string screenFrame(const FakeSession& s) {
  JsonDocument doc;
  doc["t"] = "screen";
  doc["sid"] = s.sid;
  doc["seq"] = ++seq;
  JsonArray lines = doc["lines"].to<JsonArray>();
  for (const auto& l : s.lines) lines.add(l);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string permFrame(const FakeSession& s) {
  JsonDocument doc;
  doc["t"] = "perm";
  doc["sid"] = s.sid;
  doc["req"] = s.req;
  doc["title"] = "Bash command";
  JsonArray detail = doc["detail"].to<JsonArray>();
  detail.add("npm test -- --watch=false");
  detail.add("Run the auth test suite once");
  JsonArray options = doc["options"].to<JsonArray>();
  const char* labels[] = {"Yes", "Yes, and don't ask again for npm test", "No, and tell Claude what to do"};
  for (int i = 0; i < 3; ++i) {
    JsonObject o = options.add<JsonObject>();
    o["k"] = std::to_string(i + 1);
    o["label"] = labels[i];
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

void queue(Conn& c, const std::string& frame, const unsigned long delayMs = 20) {
  c.pending.push_back({millis() + delayMs, frame});
}

void broadcast(const std::string& frame, const unsigned long delayMs = 20) {
  for (auto& [id, c] : conns) {
    if (c.authed) queue(c, frame, delayMs);
  }
}

void handle(Conn& c, const std::string& text) {
  JsonDocument doc;
  if (deserializeJson(doc, text)) return;
  const std::string t = doc["t"] | "";
  if (t == "hello") {
    const std::string token = doc["token"] | "";
    if (token == "bad") {
      queue(c, R"({"t":"err","code":"auth","msg":"bad token"})");
      return;
    }
    c.authed = true;
    queue(c, R"({"t":"ok","host":"sim-native","v":1})");
    queue(c, sessionsFrame());
    for (auto& s : sessions) {
      if (s.state == "blocked") queue(c, permFrame(s));
    }
  } else if (!c.authed) {
    return;
  } else if (t == "list") {
    queue(c, sessionsFrame());
  } else if (t == "ping") {
    queue(c, R"({"t":"pong"})");
  } else if (t == "subscribe") {
    c.subscribed = doc["sid"] | "";
    if (FakeSession* s = find(c.subscribed)) {
      queue(c, screenFrame(*s), 50);
      if (s->state == "blocked") queue(c, permFrame(*s), 60);
    }
  } else if (t == "decide") {
    FakeSession* s = find(doc["sid"] | "");
    const std::string req = doc["req"] | "";
    if (!s || s->req != req) {
      queue(c, R"({"t":"err","code":"stale_req","msg":"no such request"})");
      return;
    }
    const std::string choice = doc["choice"] | "";
    s->req.clear();
    s->state = "working";
    s->lines.push_back(choice == "3" ? "  (denied)" : "  (approved)");
    s->lines.push_back("> npm test -- --watch=false");
    s->lines.push_back("  PASS test/auth.spec.ts (14 tests)");
    broadcast(R"({"t":"perm_closed","sid":")" + s->sid + R"(","req":")" + req + "\"}");
    broadcast(sessionsFrame(), 40);
    queue(c, screenFrame(*s), 60);
  } else if (t == "input") {
    if (FakeSession* s = find(doc["sid"] | "")) {
      s->lines.push_back(std::string("> ") + (doc["text"] | ""));
      s->state = "working";
      queue(c, screenFrame(*s), 50);
      broadcast(sessionsFrame(), 60);
    }
  } else if (t == "keys") {
    if (FakeSession* s = find(doc["sid"] | "")) {
      s->lines.push_back(std::string("[key ") + (doc["keys"][0] | "?") + "]");
      queue(c, screenFrame(*s), 50);
    }
  }
}

}  // namespace

namespace sim {
void fakeBridgeReset() {
  conns.clear();
  resetSessions();
}

void fakeBridgePump() {
  const unsigned long now = millis();
  for (auto& [id, c] : conns) {
    for (size_t i = 0; i < c.pending.size();) {
      if (c.pending[i].at > now) {
        ++i;
        continue;
      }
      const std::string frame = c.pending[i].frame;
      c.pending.erase(c.pending.begin() + static_cast<long>(i));
      if (frame.empty()) {
        c.open = true;
        c.owner->transportOpened();
      } else if (c.open) {
        sentLog.push_back("<- " + frame);
        c.owner->transportMessage(frame.data(), frame.size());
      }
    }
  }
}
}  // namespace sim

int simSocketOpen(const char* url, WebSocketsClient* owner) {
  if (sessions.empty()) resetSessions();
  const int id = nextId++;
  Conn& c = conns[id];
  c.owner = owner;
  LOG_INF("FAKE", "connect %s (socket %d)", url, id);
  c.pending.push_back({millis() + 30, std::string()});
  return id;
}

bool simSocketSend(const int id, const char* data, const size_t length) {
  auto it = conns.find(id);
  if (it == conns.end() || !it->second.open) return false;
  const std::string text(data, length);
  LOG_DBG("FAKE", "-> %s", text.c_str());
  handle(it->second, text);
  return true;
}

void simSocketClose(const int id) { conns.erase(id); }
