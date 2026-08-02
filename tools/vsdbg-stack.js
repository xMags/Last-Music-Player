const { spawn } = require("child_process");
const os = require("os");
const path = require("path");

const pid = Number(process.argv[2]);
if (!pid) {
  console.error("Usage: node tools/vsdbg-stack.js <pid>");
  process.exit(2);
}

const programFiles = process.env.ProgramFiles || "C:\\Program Files";
const vsdbg = process.env.VSDBG_PATH || path.join(
  programFiles,
  "Microsoft Visual Studio",
  "18",
  "Professional",
  "Common7",
  "IDE",
  "vsdbg",
  "vsdbg.exe",
);
const logPath = path.join(os.tmpdir(), "vsdbg-lmp.log");

function dapMessage(obj) {
  const body = JSON.stringify(obj);
  return `Content-Length: ${Buffer.byteLength(body, "utf8")}\r\n\r\n${body}`;
}

function wait(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function main() {
  const child = spawn(vsdbg, ["--interpreter=vscode", `--engineLogging=${logPath}`], {
    stdio: ["pipe", "pipe", "pipe"],
  });

  let stdout = Buffer.alloc(0);
  let stderr = "";
  const messages = [];
  let seq = 1;

  function sendProtocol(obj) {
    child.stdin.write(dapMessage(obj));
  }

  function sendResponse(request, body = {}) {
    sendProtocol({
      seq: seq++,
      type: "response",
      request_seq: request.seq,
      success: true,
      command: request.command,
      body,
    });
  }

  function parseMessages() {
    while (true) {
      const headerEnd = stdout.indexOf(Buffer.from("\r\n\r\n"));
      if (headerEnd < 0) {
        return;
      }

      const header = stdout.subarray(0, headerEnd).toString("utf8");
      const match = /Content-Length: (\d+)/i.exec(header);
      if (!match) {
        return;
      }

      const length = Number(match[1]);
      const bodyStart = headerEnd + 4;
      if (stdout.length < bodyStart + length) {
        return;
      }

      const body = stdout.subarray(bodyStart, bodyStart + length).toString("utf8");
      stdout = stdout.subarray(bodyStart + length);
      try {
        const message = JSON.parse(body);
        messages.push(message);
        if (message.type === "request" && message.command === "handshake") {
          sendResponse(message);
        }
      } catch (error) {
        messages.push({ parseError: String(error), body });
      }
    }
  }

  child.stdout.on("data", (chunk) => {
    stdout = Buffer.concat([stdout, chunk]);
    parseMessages();
  });
  child.stderr.on("data", (chunk) => {
    stderr += chunk.toString("utf8");
  });

  function send(command, args = {}) {
    sendProtocol({ seq: seq++, type: "request", command, arguments: args });
  }

  send("initialize", {
    clientID: "codex",
    clientName: "Codex",
    adapterID: "cppvsdbg",
    pathFormat: "path",
    linesStartAt1: true,
    columnsStartAt1: true,
    supportsVariableType: true,
    supportsVariablePaging: true,
    supportsRunInTerminalRequest: false,
  });
  await wait(1200);

  send("attach", { processId: pid, type: "cppvsdbg", request: "attach" });
  await wait(5000);

  send("configurationDone");
  await wait(1000);

  send("pause", { threadId: 0 });
  await wait(1000);

  send("threads");
  await wait(1200);

  const threadResponse = [...messages].reverse().find((m) => m.type === "response" && m.command === "threads");
  const threads = threadResponse && threadResponse.body ? threadResponse.body.threads || [] : [];
  for (const thread of threads.slice(0, 8)) {
    send("stackTrace", { threadId: thread.id, startFrame: 0, levels: 32 });
    await wait(500);
  }

  await wait(1500);
  send("disconnect", { terminateDebuggee: false });
  await wait(500);
  child.kill();

  console.log(JSON.stringify({ stderr, messages }, null, 2));
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : String(error));
  process.exit(1);
});
