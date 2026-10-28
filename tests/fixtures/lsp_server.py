#!/usr/bin/env python3
"""Deterministic stdio LSP peer, including duplex pressure and deliberate crashes."""
import json
import os
import sys
import time


def emit(message, fragment=False):
    body = json.dumps(message, ensure_ascii=False).encode()
    frame = b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body
    if fragment:
        # Deliberately cut headers and multibyte characters at arbitrary boundaries.
        for at in range(0, len(frame), 17):
            sys.stdout.buffer.write(frame[at:at + 17])
            sys.stdout.buffer.flush()
    else:
        sys.stdout.buffer.write(frame)
        sys.stdout.buffer.flush()


def reply(request, result):
    emit({"jsonrpc": "2.0", "id": request["id"], "result": result}, fragment=True)


def offset(text, position):
    lines = text.splitlines(keepends=True)
    prefix = "".join(lines[:position["line"]])
    line = lines[position["line"]] if position["line"] < len(lines) else ""
    return len(prefix) + len(line.encode("utf-16-le")[:position["character"] * 2].decode("utf-16-le"))


if "--stall" in sys.argv:
    time.sleep(60)

documents = {}
held = None
shutdown = False
while True:
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            sys.exit(0)
        if line == b"\r\n":
            break
        key, value = line.decode("ascii").split(":", 1)
        headers[key.lower()] = value.strip()
    request = json.loads(sys.stdin.buffer.read(int(headers["content-length"])))
    method = request.get("method", "")
    params = request.get("params") or {}
    if method == "initialize":
        reply(request, {"capabilities": {"positionEncoding": "utf-16", "textDocumentSync": {
            "openClose": True, "change": 2, "save": {"includeText": True}}}})
    elif method == "initialized":
        if "--flood" in sys.argv:
            sys.stderr.buffer.write(b"server stderr " * 32768)
            sys.stderr.buffer.flush()
            emit({"jsonrpc": "2.0", "method": "test/flood", "params": {"text": "🌍" * 65536}})
    elif method == "textDocument/didOpen":
        document = params["textDocument"]
        documents[document["uri"]] = document.copy()
    elif method == "textDocument/didChange":
        document = documents[params["textDocument"]["uri"]]
        for change in params["contentChanges"]:
            text = document["text"]
            if "range" in change:
                start = offset(text, change["range"]["start"])
                end = offset(text, change["range"]["end"])
                document["text"] = text[:start] + change["text"] + text[end:]
            else:
                document["text"] = change["text"]
        document["version"] = params["textDocument"]["version"]
    elif method == "textDocument/didClose":
        documents.pop(params["textDocument"]["uri"], None)
    elif method == "test/documents":
        reply(request, {uri: {"version": doc["version"], "length": len(doc["text"].encode()),
                              "prefix": doc["text"][:16]} for uri, doc in documents.items()})
    elif method == "test/echo":
        reply(request, params)
    elif method == "test/hold":
        held = request
    elif method == "$/cancelRequest":
        if held and params["id"] == held["id"]:
            reply(held, {"late": True})
            held = None
    elif method == "test/crash":
        os._exit(17)
    elif method == "shutdown":
        shutdown = True
        reply(request, None)
    elif method == "exit":
        sys.exit(0 if shutdown else 1)
    elif "id" in request:
        emit({"jsonrpc": "2.0", "id": request["id"], "error": {"code": -32601, "message": method}})
