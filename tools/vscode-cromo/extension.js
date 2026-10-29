// Formats .cro files with `cromo fmt -` (D164): the text of the document goes to
// the standard input, and the formatted text comes back on the standard output.
// With a syntax error cromo fmt changes nothing, and the errors go to the
// "Cromo" output panel.
const vscode = require('vscode');
const { spawn } = require('child_process');

function activate(context) {
    const output = vscode.window.createOutputChannel('Cromo');

    const formatter = {
        provideDocumentFormattingEdits(document) {
            const executable = vscode.workspace.getConfiguration('cromo').get('path') || 'cromo';
            const text = document.getText();
            return new Promise((resolve) => {
                const child = spawn(executable, ['fmt', '-']);
                const stdout = [];
                const stderr = [];
                child.stdout.on('data', (data) => stdout.push(data));
                child.stderr.on('data', (data) => stderr.push(data));
                child.on('error', (error) => {
                    output.appendLine(`cannot run ${executable}: ${error.message} (see the setting cromo.path)`);
                    resolve([]);
                });
                child.on('close', (code) => {
                    if (code !== 0) {
                        output.appendLine(Buffer.concat(stderr).toString('utf8'));
                        vscode.window.setStatusBarMessage('cromo fmt: the file has errors', 5000);
                        resolve([]);
                        return;
                    }
                    const formatted = Buffer.concat(stdout).toString('utf8');
                    if (formatted === text) {
                        resolve([]);
                        return;
                    }
                    const all = new vscode.Range(document.positionAt(0), document.positionAt(text.length));
                    resolve([vscode.TextEdit.replace(all, formatted)]);
                });
                child.stdin.end(text, 'utf8');
            });
        },
    };

    context.subscriptions.push(output, vscode.languages.registerDocumentFormattingEditProvider('cromo', formatter));
}

function deactivate() {}

module.exports = { activate, deactivate };
