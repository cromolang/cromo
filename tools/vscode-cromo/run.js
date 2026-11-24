// Run, Debug and Test for the programs of Cromo: the buttons of the title of the
// editor, the lines above main and above a test, and the status bar. A program runs
// with `cromo run` in the panel of the terminal. It is debugged by the debugger of the
// C/C++ extension of Microsoft, on the executable that `cromo build -O0` makes, with
// the files of debug/ that show a string, a list and a map of Cromo as their values.
const vscode = require('vscode');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFile } = require('child_process');

const DEBUGGER = 'ms-vscode.cpptools';

// `i32 main(` at the start of a line, and `[[test]]` with the name of its function.
const MAIN = /^[ \t]*(?:public[ \t]+)?i32[ \t]+main[ \t]*\(/m;
const TEST = /^[ \t]*\[\[test\]\][ \t]*(?:\r?\n[ \t]*)?void[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(/gm;

// The folder of cromo.mod above a file, or ''.
function projectOf(file) {
    let folder = path.dirname(file);
    for (;;) {
        if (fs.existsSync(path.join(folder, 'cromo.mod'))) return folder;
        const parent = path.dirname(folder);
        if (parent === folder) return '';
        folder = parent;
    }
}

// The .cro file a command is about: the one given, as by a line above main, or the
// one of the editor.
function fileOf(uri) {
    if (uri && uri.fsPath) return uri.fsPath;
    const editor = vscode.window.activeTextEditor;
    if (editor && editor.document.languageId === 'cromo' && editor.document.uri.scheme === 'file') return editor.document.uri.fsPath;
    return '';
}

function hasMain(file) {
    const open = vscode.workspace.textDocuments.find((document) => document.uri.fsPath === file);
    try {
        return MAIN.test(open ? open.getText() : fs.readFileSync(file, 'utf8'));
    } catch (error) {
        return false;
    }
}

// Runs cromo with its arguments in the panel of the terminal; the exit code of cromo.
function runCromo(findCromo, label, args, folder) {
    const task = new vscode.Task({ type: 'cromo', label }, vscode.TaskScope.Workspace, label, 'cromo',
        new vscode.ProcessExecution(findCromo(), args, { cwd: folder }), []);
    task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true };
    return new Promise((resolve) => {
        let started = null;
        const ended = vscode.tasks.onDidEndTaskProcess((event) => {
            if (event.execution !== started) return;
            ended.dispose();
            resolve(event.exitCode);
        });
        vscode.tasks.executeTask(task).then((execution) => { started = execution; }, () => {
            ended.dispose();
            resolve(-1);
        });
    });
}

async function save() {
    await vscode.workspace.saveAll(false);
}

// What Run and Debug are about: the file of the editor when it has main, else the
// project of cromo.mod it is in.
function target(uri) {
    const file = fileOf(uri);
    if (!file) {
        vscode.window.showInformationMessage('Open a .cro file of the program to run.');
        return null;
    }
    const project = projectOf(file);
    if (hasMain(file)) return { file, folder: project || path.dirname(file) };
    if (project) return { file: '', folder: project };
    vscode.window.showInformationMessage(`${path.basename(file)} has no main: open the file of the program.`);
    return null;
}

async function run(findCromo, uri) {
    const what = target(uri);
    if (!what) return;
    await save();
    runCromo(findCromo, what.file ? `run ${path.basename(what.file)}` : 'run', what.file ? ['run', what.file] : ['run'], what.folder);
}

async function test(findCromo, uri, name) {
    const file = fileOf(uri);
    if (!file) return;
    await save();
    const args = ['test', file];
    if (name) args.push(name);
    runCromo(findCromo, name ? `test ${name}` : `test ${path.basename(file)}`, args, projectOf(file) || path.dirname(file));
}

// The debugger of the C/C++ extension, or an offer to install it.
async function needDebugger() {
    if (vscode.extensions.getExtension(DEBUGGER)) return true;
    const install = 'Install';
    const answer = await vscode.window.showInformationMessage(
        'Debugging a program of Cromo uses the debugger of the C/C++ extension of Microsoft, which is not installed.', install);
    if (answer === install) await vscode.commands.executeCommand('workbench.extensions.installExtension', DEBUGGER);
    return !!vscode.extensions.getExtension(DEBUGGER);
}

// Builds the program of a file without optimizations, so that every line and every
// variable is there, and starts the debugger on it. `tests` builds the executable of
// cromo test, which runs the test whose number is its argument.
async function debugProgram(context, findCromo, file, folder, tests, args) {
    const output = path.join(os.tmpdir(), 'cromo-debug');
    fs.mkdirSync(output, { recursive: true });
    const windows = process.platform === 'win32';
    const program = path.join(output, path.basename(file, '.cro') + (tests ? '-tests' : '') + (windows ? '.exe' : ''));
    const build = ['build', '-O0'].concat(tests ? ['--tests'] : [], [file, '-o', program]);
    const code = await runCromo(findCromo, `build ${path.basename(file)}`, build, folder);
    if (code !== 0) return;
    const name = `Cromo: ${path.basename(file)}`;
    const configuration = windows ? {
        name, type: 'cppvsdbg', request: 'launch', program, args, cwd: folder, console: 'integratedTerminal',
        visualizerFile: path.join(context.extensionPath, 'debug', 'cromo.natvis'),
    } : {
        name, type: 'cppdbg', request: 'launch', program, args, cwd: folder, MIMode: 'gdb',
        setupCommands: [
            { text: '-enable-pretty-printing', ignoreFailures: true },
            { text: `source ${path.join(context.extensionPath, 'debug', 'cromo_gdb.py')}`, ignoreFailures: true },
        ],
    };
    await vscode.debug.startDebugging(vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file)), configuration);
}

async function debug(context, findCromo, uri) {
    const what = target(uri);
    if (!what) return;
    if (!what.file) {
        vscode.window.showInformationMessage('Open the file with the main of the program to debug.');
        return;
    }
    if (!await needDebugger()) return;
    await save();
    await debugProgram(context, findCromo, what.file, what.folder, false, []);
}

// The names of the tests of a file, in the order of their executable, from
// `cromo test --list`; null when this cromo does not have the option.
function listTests(findCromo, file, folder) {
    return new Promise((resolve) => {
        execFile(findCromo(), ['test', '--list', '--quiet', file], { cwd: folder, windowsHide: true }, (error, stdout) => {
            if (error) resolve(null);
            else resolve(String(stdout).split(/\r?\n/).map((line) => line.trim()).filter((line) => line.length > 0));
        });
    });
}

async function debugTest(context, findCromo, uri, name) {
    const file = fileOf(uri);
    if (!file || !name) return;
    if (!await needDebugger()) return;
    await save();
    const folder = projectOf(file) || path.dirname(file);
    const names = await listTests(findCromo, file, folder);
    if (!names) {
        vscode.window.showWarningMessage('Debugging a test needs cromo 0.98.1 or later, and a file that compiles.');
        return;
    }
    // In the file of a module the name has the module in front: geometry.area.
    const index = names.findIndex((listed) => listed === name || listed.endsWith('.' + name));
    if (index < 0) {
        vscode.window.showWarningMessage(`cromo test has no test called ${name} in ${path.basename(file)}.`);
        return;
    }
    await debugProgram(context, findCromo, file, folder, true, [String(index)]);
}

// Run | Debug above main, and Run test | Debug test above each test.
const lenses = {
    provideCodeLenses(document) {
        const text = document.getText();
        const found = [];
        const main = MAIN.exec(text);
        if (main) {
            const range = new vscode.Range(document.positionAt(main.index), document.positionAt(main.index));
            found.push(new vscode.CodeLens(range, { title: '$(play) Run', command: 'cromo.run', arguments: [document.uri] }));
            found.push(new vscode.CodeLens(range, { title: '$(debug-alt) Debug', command: 'cromo.debug', arguments: [document.uri] }));
        }
        TEST.lastIndex = 0;
        for (let match = TEST.exec(text); match; match = TEST.exec(text)) {
            const range = new vscode.Range(document.positionAt(match.index), document.positionAt(match.index));
            found.push(new vscode.CodeLens(range, { title: '$(beaker) Run test', command: 'cromo.test', arguments: [document.uri, match[1]] }));
            found.push(new vscode.CodeLens(range, { title: '$(debug-alt) Debug test', command: 'cromo.debugTest', arguments: [document.uri, match[1]] }));
        }
        return found;
    },
};

function register(context, findCromo) {
    // In the status bar while a file of Cromo is in the editor.
    const items = [
        ['$(play) Run', 'cromo.run', 'Run the program of Cromo'],
        ['$(debug-alt) Debug', 'cromo.debug', 'Debug the program of Cromo'],
        ['$(book) Guide', 'cromo.guide', 'Open the Cromo Guide'],
    ].map(([text, command, tooltip], index) => {
        const item = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100 - index);
        item.text = text;
        item.command = command;
        item.tooltip = tooltip;
        return item;
    });
    const show = () => {
        const editor = vscode.window.activeTextEditor;
        const cromo = !!editor && editor.document.languageId === 'cromo';
        for (const item of items) {
            if (cromo) item.show();
            else item.hide();
        }
    };
    show();
    context.subscriptions.push(
        ...items,
        vscode.window.onDidChangeActiveTextEditor(show),
        vscode.commands.registerCommand('cromo.run', (uri) => run(findCromo, uri)),
        vscode.commands.registerCommand('cromo.debug', (uri) => debug(context, findCromo, uri)),
        vscode.commands.registerCommand('cromo.test', (uri, name) => test(findCromo, uri, name)),
        vscode.commands.registerCommand('cromo.debugTest', (uri, name) => debugTest(context, findCromo, uri, name)),
        vscode.languages.registerCodeLensProvider({ language: 'cromo', scheme: 'file' }, lenses),
    );
}

module.exports = { register };
