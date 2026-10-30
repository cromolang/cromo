// The client of `cromo lsp` (D170), written without packages from npm so that
// the extension is built without Node.js (D178). It sends the open .cro documents
// to the server, shows its diagnostics, and asks it for hover, definition, the
// symbols of a file, the members after a dot and formatting (D171, D179, D164).
const vscode = require('vscode');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');

// The cromo to run: the setting cromo.path; without it, the one built in a folder
// of the workspace (build/cromo.exe, while developing cromo), else cromo from PATH.
function findCromo() {
    const configured = vscode.workspace.getConfiguration('cromo').get('path');
    if (configured) return configured;
    const name = process.platform === 'win32' ? 'cromo.exe' : 'cromo';
    for (const folder of vscode.workspace.workspaceFolders || []) {
        const built = path.join(folder.uri.fsPath, 'build', name);
        if (fs.existsSync(built)) return built;
    }
    return 'cromo';
}

// On Windows a running .exe cannot be replaced, and build.ps1 could not write
// cromo.exe again while VS Code is open: the server runs from a copy, in a folder
// of the temporary folder named after the size and the date of cromo.exe, next to
// a junction lib to the library of the original, which cromo reads (D237).
function runnableCopy(executable) {
    if (process.platform !== 'win32' || !path.isAbsolute(executable) || !fs.existsSync(executable)) return executable;
    const home = path.dirname(executable);
    const library = [path.join(home, '..', 'lib'), path.join(home, 'lib')].find((folder) =>
        fs.existsSync(path.join(folder, 'core.cro')));
    if (!library) return executable;
    const stat = fs.statSync(executable);
    const name = `cromo-lsp-${stat.size}-${Math.floor(stat.mtimeMs)}`;
    const folder = path.join(os.tmpdir(), name);
    const copy = path.join(folder, 'cromo.exe');
    try {
        if (!fs.existsSync(copy)) {
            fs.mkdirSync(folder, { recursive: true });
            const link = path.join(folder, 'lib');
            if (!fs.existsSync(link)) fs.symlinkSync(path.resolve(library), link, 'junction');
            fs.copyFileSync(executable, copy);
        }
    } catch (error) {
        return executable;
    }
    // The copies of the cromo.exe of before, if no server uses them any more: the
    // junction is removed by itself, so that the library is never touched.
    for (const old of fs.readdirSync(os.tmpdir())) {
        if (!old.startsWith('cromo-lsp-') || old === name) continue;
        const other = path.join(os.tmpdir(), old);
        if (old.endsWith('.exe')) {  // a copy of the extensions before 0.8.0
            fs.unlink(other, () => {});
            continue;
        }
        try {
            fs.unlinkSync(path.join(other, 'cromo.exe'));
            fs.rmdirSync(path.join(other, 'lib'));
            fs.rmdirSync(other);
        } catch (error) {
            // a server still runs it
        }
    }
    return copy;
}

class Server {
    constructor(output, diagnostics) {
        this.output = output;
        this.diagnostics = diagnostics;
        this.process = null;
        this.ready = null;
        this.warned = false;  // the error about a missing cromo is shown once
        this.nextId = 1;
        this.pending = new Map();
        this.buffer = Buffer.alloc(0);
    }

    // Starts cromo lsp the first time it is needed, and again if it stopped.
    start() {
        if (this.ready) return this.ready;
        const executable = findCromo();
        this.watch(executable);
        const child = spawn(runnableCopy(executable), ['lsp']);
        this.process = child;
        child.stdout.on('data', (data) => this.receive(data));
        child.stderr.on('data', (data) => this.output.append(data.toString('utf8')));
        // The events of a process that has been replaced are ignored.
        child.on('error', (error) => {
            if (this.process !== child) return;
            this.output.appendLine(`cannot run ${executable}: ${error.message}`);
            if (!this.warned) {
                this.warned = true;
                vscode.window
                    .showErrorMessage(`Cromo: cannot run ${executable}. Set cromo.path to the path of cromo.exe.`, 'Open Settings')
                    .then((choice) => {
                        if (choice) vscode.commands.executeCommand('workbench.action.openSettings', 'cromo.path');
                    });
            }
            this.stop();
        });
        child.on('exit', (code) => {
            if (this.process !== child) return;
            this.output.appendLine(`cromo lsp stopped with exit code ${code}`);
            this.stop();
        });
        const folder = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0];
        this.ready = this.request('initialize', {
            processId: process.pid,
            rootUri: folder ? folder.uri.toString() : null,
            capabilities: {},
        }).then(() => {
            this.notify('initialized', {});
            for (const document of vscode.workspace.textDocuments) this.open(document);
        });
        return this.ready;
    }

    // A new cromo.exe, built again, restarts the server from a new copy.
    watch(executable) {
        if (this.watched === executable) return;
        if (this.watched) fs.unwatchFile(this.watched);
        this.watched = path.isAbsolute(executable) ? executable : null;
        if (!this.watched) return;
        fs.watchFile(this.watched, { interval: 2000 }, (now, before) => {
            if (now.mtimeMs !== before.mtimeMs && now.size > 0 && this.process) {
                this.output.appendLine(`${executable} changed: cromo lsp starts again`);
                this.restart();
            }
        });
    }

    // When cromo.path or cromo.exe change: a new server, with every open document.
    restart() {
        const running = this.process;
        this.stop();
        this.warned = false;
        if (running) running.kill();
        this.start();
    }

    stop() {
        for (const { resolve } of this.pending.values()) resolve(null);
        this.pending.clear();
        this.process = null;
        this.ready = null;
        this.buffer = Buffer.alloc(0);
    }

    // With the setting cromo.trace, every message goes to the output panel.
    trace(direction, message) {
        if (!vscode.workspace.getConfiguration('cromo').get('trace')) return;
        const text = JSON.stringify(message);
        this.output.appendLine(`${direction} ${text.length > 400 ? text.slice(0, 400) + '...' : text}`);
    }

    send(message) {
        if (!this.process) return;
        this.trace('->', message);
        const body = Buffer.from(JSON.stringify(message), 'utf8');
        this.process.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
        this.process.stdin.write(body);
    }

    request(method, params) {
        if (!this.process) return Promise.resolve(null);  // cromo could not start
        const id = this.nextId++;
        return new Promise((resolve) => {
            this.pending.set(id, { resolve });
            this.send({ jsonrpc: '2.0', id, method, params });
        });
    }

    notify(method, params) {
        this.send({ jsonrpc: '2.0', method, params });
    }

    // The messages arrive in pieces: each is a header with its length, then the JSON.
    receive(data) {
        this.buffer = Buffer.concat([this.buffer, data]);
        for (;;) {
            const end = this.buffer.indexOf('\r\n\r\n');
            if (end < 0) return;
            const match = /Content-Length: *(\d+)/i.exec(this.buffer.slice(0, end).toString('ascii'));
            const length = match ? Number(match[1]) : 0;
            if (this.buffer.length < end + 4 + length) return;
            const body = this.buffer.slice(end + 4, end + 4 + length).toString('utf8');
            this.buffer = this.buffer.slice(end + 4 + length);
            this.handle(JSON.parse(body));
        }
    }

    handle(message) {
        this.trace('<-', message);
        if (message.id !== undefined && this.pending.has(message.id)) {
            const { resolve } = this.pending.get(message.id);
            this.pending.delete(message.id);
            if (message.error) this.output.appendLine(message.error.message);
            resolve(message.result === undefined ? null : message.result);
        } else if (message.method === 'textDocument/publishDiagnostics') {
            const items = message.params.diagnostics.map((item) => {
                const diagnostic = new vscode.Diagnostic(toRange(item.range), item.message,
                    item.severity === 2 ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
                diagnostic.source = 'cromo';
                return diagnostic;
            });
            this.diagnostics.set(vscode.Uri.parse(message.params.uri), items);
        }
    }

    open(document) {
        if (document.languageId !== 'cromo' || document.uri.scheme !== 'file') return;
        this.notify('textDocument/didOpen', {
            textDocument: { uri: document.uri.toString(), languageId: 'cromo', version: document.version, text: document.getText() },
        });
    }
}

function toRange(range) {
    return new vscode.Range(range.start.line, range.start.character, range.end.line, range.end.character);
}

function activate(context) {
    const output = vscode.window.createOutputChannel('Cromo');
    const diagnostics = vscode.languages.createDiagnosticCollection('cromo');
    const server = new Server(output, diagnostics);
    const selector = { language: 'cromo', scheme: 'file' };
    const where = (document, position) => ({
        textDocument: { uri: document.uri.toString() },
        position: { line: position.line, character: position.character },
    });
    const ask = async (method, params) => {
        await server.start();
        return server.request(method, params);
    };

    context.subscriptions.push(
        output,
        diagnostics,
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration('cromo.path')) server.restart();
        }),
        vscode.workspace.onDidOpenTextDocument((document) => {
            if (server.ready) server.open(document);
            else if (document.languageId === 'cromo') server.start();
        }),
        vscode.workspace.onDidChangeTextDocument((event) => {
            const document = event.document;
            if (!server.ready || document.languageId !== 'cromo' || document.uri.scheme !== 'file') return;
            server.notify('textDocument/didChange', {
                textDocument: { uri: document.uri.toString(), version: document.version },
                contentChanges: [{ text: document.getText() }],
            });
        }),
        vscode.workspace.onDidCloseTextDocument((document) => {
            if (!server.ready || document.languageId !== 'cromo') return;
            server.notify('textDocument/didClose', { textDocument: { uri: document.uri.toString() } });
        }),
        vscode.languages.registerHoverProvider(selector, {
            async provideHover(document, position) {
                const result = await ask('textDocument/hover', where(document, position));
                if (!result) return null;
                return new vscode.Hover(new vscode.MarkdownString(result.contents.value), result.range && toRange(result.range));
            },
        }),
        vscode.languages.registerDefinitionProvider(selector, {
            async provideDefinition(document, position) {
                const result = await ask('textDocument/definition', where(document, position));
                if (!result) return null;
                return new vscode.Location(vscode.Uri.parse(result.uri), toRange(result.range));
            },
        }),
        vscode.languages.registerDocumentSymbolProvider(selector, {
            async provideDocumentSymbols(document) {
                const result = await ask('textDocument/documentSymbol', { textDocument: { uri: document.uri.toString() } });
                const convert = (item) => {
                    const symbol = new vscode.DocumentSymbol(item.name, item.detail || '', item.kind - 1,
                        toRange(item.range), toRange(item.selectionRange));
                    symbol.children = (item.children || []).map(convert);
                    return symbol;
                };
                return (result || []).map(convert);
            },
        }),
        vscode.languages.registerCompletionItemProvider(selector, {
            async provideCompletionItems(document, position) {
                const result = await ask('textDocument/completion', where(document, position));
                return (result || []).map((item) => {
                    const completion = new vscode.CompletionItem(item.label, item.kind - 1);
                    completion.detail = item.detail;
                    return completion;
                });
            },
        }, '.'),  // D179: the members after a dot
        vscode.languages.registerDocumentFormattingEditProvider(selector, {
            async provideDocumentFormattingEdits(document) {
                const result = await ask('textDocument/formatting', { textDocument: { uri: document.uri.toString() } });
                return (result || []).map((edit) => vscode.TextEdit.replace(toRange(edit.range), edit.newText));
            },
        }),
        {
            dispose: () => {
                const running = server.process;
                server.process = null;  // no message: it is stopped on purpose
                if (running) running.kill();
                if (server.watched) fs.unwatchFile(server.watched);
            },
        },
    );

    if (vscode.workspace.textDocuments.some((document) => document.languageId === 'cromo')) server.start();
}

function deactivate() {}

module.exports = { activate, deactivate };
