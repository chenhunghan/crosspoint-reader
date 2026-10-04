// Built-in demo bridge for the CrossPoint simulator.
//
// Implements the device side of docs/protocol.md (v1) entirely in the page,
// behind the same surface as a browser WebSocket (onopen / onmessage /
// onclose / send / close / readyState), so the firmware's BridgeClient talks
// to it exactly as it would to `agentmux daemon`.
//
// Cast: s1 claude is blocked on a Bash permission prompt, s2 codex is busy
// building, s3 claude is idle. Decisions, replies and Esc/Enter keys change
// state the way a real agent would, a little slower than real time so the
// e-ink redraw throttling is visible.
(function (global) {
  "use strict";

  const now = () => Math.floor(Date.now() / 1000);

  function makeWorld() {
    return {
      seq: 0,
      reqCounter: 7,
      sessions: [
        {
          sid: "s1",
          agent: "claude",
          title: "api: fix auth",
          cwd: "~/src/api",
          state: "blocked",
          since: now() - 95,
          req: "r7",
          perm: {
            title: "Bash command",
            detail: ["npm test -- --watch=false", "Run the auth test suite once"],
            options: [
              { k: "1", label: "Yes" },
              { k: "2", label: "Yes, and don't ask again for npm test" },
              { k: "3", label: "No, and tell Claude what to do" },
            ],
            onYes: [
              "  PASS test/auth.spec.ts (14 tests)",
              "  PASS test/session.spec.ts (6 tests)",
              "",
              "All auth tests pass. The refresh fix is in",
              "src/auth/session.ts (rotateToken now re-sets",
              "the cookie). Anything else?",
            ],
          },
          lines: [
            "> run the auth tests and fix what fails",
            "",
            "* Reading src/auth/session.ts",
            "* Reading test/auth.spec.ts",
            "",
            "The session cookie is not refreshed on",
            "token rotation. I'll patch rotateToken()",
            "and run the suite:",
            "",
            "* Edit src/auth/session.ts (+4 -1)",
            "",
            "Bash(npm test -- --watch=false)",
          ],
        },
        {
          sid: "s2",
          agent: "codex",
          title: "web: dark mode",
          cwd: "~/src/web",
          state: "working",
          since: now() - 40,
          req: "",
          lines: [
            "> add a dark mode toggle to settings",
            "",
            "- Updated src/theme.ts (+42 -3)",
            "- Updated src/Settings.tsx (+18 -1)",
            "",
            "$ npm run build",
          ],
          buildStep: 0,
        },
        {
          sid: "s3",
          agent: "claude",
          title: "infra: terraform",
          cwd: "~/src/infra",
          state: "idle",
          since: now() - 600,
          req: "",
          lines: [
            "> plan the staging changes",
            "",
            "* Bash(terraform plan -out=staging.plan)",
            "",
            "Plan: 2 to add, 1 to change, 0 to destroy.",
            "  + aws_s3_bucket.assets_staging",
            "  + aws_cloudfront_distribution.cdn",
            "  ~ aws_iam_role.deploy",
            "",
            "Want me to apply it?",
          ],
        },
      ],
    };
  }

  const BUILD_LINES = [
    "vite v5.4.2 building for production...",
    "transforming (212) src/components/Toggle.tsx",
    "transforming (488) node_modules/react-dom",
    "rendering chunks (3)...",
    "dist/index.html          0.46 kB",
    "dist/assets/index.css   11.80 kB",
    "dist/assets/index.js   148.31 kB",
    "built in 2.41s",
    "",
    "Dark mode toggle added; it follows the OS",
    "setting until changed. Build is green.",
  ];

  class DemoBridgeSocket {
    constructor(url, opts) {
      this.url = url;
      this.readyState = 0; // CONNECTING
      this.onopen = null;
      this.onmessage = null;
      this.onclose = null;
      this.onerror = null;
      this.world = (opts && opts.world) || DemoBridgeSocket.sharedWorld();
      this.subscribed = "";
      this.authed = false;
      this.timers = new Set();
      this.lastScreenAt = 0;
      this.screenTimer = null;
      this.later(120, () => {
        this.readyState = 1;
        if (this.onopen) this.onopen({});
      });
    }

    static sharedWorld() {
      if (!DemoBridgeSocket.world) DemoBridgeSocket.world = makeWorld();
      return DemoBridgeSocket.world;
    }

    static reset() {
      DemoBridgeSocket.world = makeWorld();
    }

    later(ms, fn) {
      const t = setTimeout(() => {
        this.timers.delete(t);
        if (this.readyState === 1 || this.readyState === 0) fn();
      }, ms);
      this.timers.add(t);
    }

    emit(obj) {
      if (this.readyState !== 1) return;
      const data = JSON.stringify(obj);
      if (this.onmessage) this.onmessage({ data });
    }

    close() {
      if (this.readyState === 3) return;
      this.readyState = 3;
      for (const t of this.timers) clearTimeout(t);
      this.timers.clear();
      if (this.onclose) this.onclose({ code: 1000, reason: "" });
    }

    find(sid) {
      return this.world.sessions.find((s) => s.sid === sid);
    }

    sessionsFrame() {
      return {
        t: "sessions",
        items: this.world.sessions.map((s) => {
          const item = { sid: s.sid, agent: s.agent, title: s.title, cwd: s.cwd, state: s.state, since: s.since };
          if (s.req) item.req = s.req;
          return item;
        }),
      };
    }

    permFrame(s) {
      return {
        t: "perm",
        sid: s.sid,
        req: s.req,
        title: s.perm.title,
        detail: s.perm.detail,
        options: s.perm.options,
      };
    }

    // Screen frames: at most one per 2 s per subscription (protocol.md).
    pushScreen(force) {
      const s = this.find(this.subscribed);
      if (!s) return;
      const send = () => {
        this.screenTimer = null;
        this.lastScreenAt = Date.now();
        this.world.seq += 1;
        this.emit({ t: "screen", sid: s.sid, seq: this.world.seq, lines: s.lines.slice(-40) });
      };
      const wait = force ? 0 : Math.max(0, 2000 - (Date.now() - this.lastScreenAt));
      if (wait === 0) {
        send();
      } else if (!this.screenTimer) {
        this.screenTimer = setTimeout(send, wait);
        this.timers.add(this.screenTimer);
      }
    }

    setState(s, state) {
      s.state = state;
      s.since = now();
      if (state !== "blocked") s.req = "";
      this.emit({ t: "status", sid: s.sid, state, since: s.since });
      this.emit(this.sessionsFrame());
    }

    say(s, lines, done) {
      // Agent "types" a few lines, then settles.
      let i = 0;
      const step = () => {
        if (i >= lines.length) {
          if (done) done();
          return;
        }
        s.lines.push(lines[i++]);
        if (this.subscribed === s.sid) this.pushScreen(false);
        this.later(700, step);
      };
      this.later(600, step);
    }

    tickBuild() {
      const s = this.find("s2");
      if (!s || s.state !== "working") return;
      if (s.buildStep < BUILD_LINES.length) {
        s.lines.push(BUILD_LINES[s.buildStep++]);
        if (this.subscribed === "s2") this.pushScreen(false);
        this.later(2500, () => this.tickBuild());
      } else {
        this.setState(s, "idle");
      }
    }

    scheduleNextPermission(s) {
      // Keep the demo lively: some time after finishing, s1 asks again.
      this.later(15000, () => {
        if (s.state !== "idle") return;
        this.world.reqCounter += 1;
        s.req = "r" + this.world.reqCounter;
        s.perm = {
          title: "Edit file",
          detail: ["src/auth/session.ts", "@@ -41,3 +41,6 @@ rotateToken()", "+  res.cookie(SESSION, next, COOKIE_OPTS);"],
          options: [
            { k: "1", label: "Yes" },
            { k: "2", label: "Yes, allow all edits this session" },
            { k: "3", label: "No, and tell Claude what to do" },
          ],
          onYes: ["* Edit src/auth/session.ts (+3 -0)", "", "Done. The cookie is re-set on every rotation."],
        };
        s.lines.push("", "I'll also re-set the cookie on refresh:", "", "Edit(src/auth/session.ts)");
        this.setState(s, "blocked");
        this.emit(this.permFrame(s));
        if (this.subscribed === s.sid) this.pushScreen(true);
      });
    }

    send(text) {
      if (this.readyState !== 1) throw new Error("demo bridge: socket not open");
      let msg;
      try {
        msg = JSON.parse(text);
      } catch (e) {
        this.later(10, () => this.emit({ t: "err", code: "bad_request", msg: "not JSON" }));
        return;
      }
      this.later(40, () => this.handle(msg));
    }

    handle(msg) {
      const w = this.world;
      if (msg.t === "hello") {
        if (!msg.token || msg.token === "bad") {
          this.emit({ t: "err", code: "auth", msg: "wrong pairing token (demo: any token but 'bad')" });
          this.later(50, () => this.close());
          return;
        }
        this.authed = true;
        this.emit({ t: "ok", host: "demo-bridge", v: 1 });
        this.emit(this.sessionsFrame());
        for (const s of w.sessions) if (s.state === "blocked") this.emit(this.permFrame(s));
        this.later(2500, () => this.tickBuild());
        return;
      }
      if (!this.authed) {
        this.emit({ t: "err", code: "auth", msg: "hello first" });
        return;
      }
      const s = msg.sid !== undefined ? this.find(msg.sid) : null;
      switch (msg.t) {
        case "ping":
          this.emit({ t: "pong" });
          break;
        case "list":
          this.emit(this.sessionsFrame());
          break;
        case "subscribe":
          this.subscribed = msg.sid || "";
          if (s) {
            this.pushScreen(true);
            if (s.state === "blocked") this.emit(this.permFrame(s));
          }
          break;
        case "decide": {
          if (!s) return this.emit({ t: "err", code: "no_session", msg: msg.sid });
          if (!s.req || s.req !== msg.req) return this.emit({ t: "err", code: "stale_req", msg: msg.req });
          const option = s.perm.options.find((o) => o.k === msg.choice);
          this.emit({ t: "perm_closed", sid: s.sid, req: s.req });
          const yes = msg.choice === "1" || msg.choice === "2";
          s.lines.push("  " + (option ? option.label : msg.choice));
          this.setState(s, yes ? "working" : "idle");
          if (yes) {
            this.say(s, s.perm.onYes, () => {
              this.setState(s, "idle");
              this.scheduleNextPermission(s);
            });
          } else {
            s.lines.push("", "Okay - what should I do instead?");
            this.pushScreen(false);
          }
          break;
        }
        case "input": {
          if (!s) return this.emit({ t: "err", code: "no_session", msg: msg.sid });
          s.lines.push("", "> " + String(msg.text || ""));
          this.pushScreen(true);
          if (msg.submit) {
            this.setState(s, "working");
            this.say(s, ["", "* Thinking...", "Got it: \"" + String(msg.text || "").slice(0, 30) + "\".", "Done."], () =>
              this.setState(s, "idle"),
            );
          }
          break;
        }
        case "keys": {
          if (!s) return this.emit({ t: "err", code: "no_session", msg: msg.sid });
          const keys = Array.isArray(msg.keys) ? msg.keys : [];
          for (const k of keys) {
            if (k === "esc") {
              s.lines.push("  [Esc] Interrupted by user");
              if (s.state === "working") this.setState(s, "idle");
            } else if (k === "enter") {
              s.lines.push("");
            } else {
              s.lines.push("  [" + k + "]");
            }
          }
          this.pushScreen(false);
          break;
        }
        default:
          this.emit({ t: "err", code: "bad_request", msg: "unknown t=" + msg.t });
      }
    }
  }

  global.DemoBridgeSocket = DemoBridgeSocket;
})(typeof window !== "undefined" ? window : globalThis);
