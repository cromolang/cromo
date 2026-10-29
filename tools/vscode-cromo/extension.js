// The client of `cromo lsp` (D170), written without packages from npm so that
// the extension is built without Node.js (D178). It sends the open .cro documents
// to the server, shows its diagnostics, and asks it for hover, definition, the
// symbols of a file and formatting (D171, D164).
const vscode = require('vscode');
const { spawn } = require('child_process');

class Server {
    constructor(output, diagnostics) {
        this.output = output;
        this.diagnostics = diagnostics;
        this.process = null;
        this.ready = null;
        this.nextId = 1;
        this.pending = new Map();
        this.buffer = Buffer.alloc(0);
    }

    // Starts cromo lsp the first time it is needed, and again if it stopped.
    start() {
        if (this.ready) return this.ready;
        const executable = vscode.workspace.getConfiguration('cromo').get('path') || 'cromo';
        this.process = spawn(executable, ['lsp']);
        this.process.stdout.on('data', (data) => this.receive(data));
        this.process.stderr.on('data', (data) => this.output.append(data.toString('utf8')));
        this.process.on('error', (error) => {
            this.output.appendLine(`cannot run ${executable}: ${error.message} (see the setting cromo.path)`);
            this.stop();
        });
        this.process.on('exit', (code) => {
            if (this.process) this.output.appendLine(`cromo lsp stopped with exit code ${code}`);
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

    stop() {
        for (const { resolve } of this.pending.values()) resolve(null);
        this.pending.clear();
        this.process = null;
        this.ready = null;
        this.buffer = Buffer.alloc(0);
    }

    send(message) {
        if (!this.process) return;
        const body = Buffer.from(JSON.stringify(message), 'utf8');
        this.process.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
        this.process.stdin.write(body);
    }

    request(method, params) {
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
            },
        },
    );

    if (vscode.workspace.textDocuments.some((document) => document.languageId === 'cromo')) server.start();
}

function deactivate() {}

module.exports = { activate, deactivate };
