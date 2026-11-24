// The Cromo Guide inside Visual Studio Code: the pages of cromolang.org, in a panel
// next to the editor, in the language of the setting cromo.guideLanguage, or in that of
// Visual Studio Code when the guide has it, else in English. The pages are those of
// the site, so a new language of the guide needs no new version of the extension.
const vscode = require('vscode');
const https = require('https');

const SITE = 'https://cromolang.org/book/';

let panel = null;

// Whether the site has the guide in a language: its folder answers.
function hasLanguage(language) {
    return new Promise((resolve) => {
        const request = https.request(SITE + language + '/', { method: 'HEAD', timeout: 5000 }, (response) => {
            response.resume();
            resolve(response.statusCode === 200);
        });
        request.on('timeout', () => request.destroy());
        request.on('error', () => resolve(false));
        request.end();
    });
}

// The language of the guide to show: `it`, `en`, ...
async function guideLanguage() {
    const chosen = String(vscode.workspace.getConfiguration('cromo').get('guideLanguage') || '').trim().toLowerCase();
    const wanted = chosen || vscode.env.language.toLowerCase().split('-')[0];
    if (!/^[a-z]{2,3}$/.test(wanted) || wanted === 'en') return 'en';
    return await hasLanguage(wanted) ? wanted : 'en';
}

// A page that is only the guide of the site, as large as the panel.
function page(address) {
    return `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; frame-src https://cromolang.org; style-src 'unsafe-inline'">
<style>
html, body { height: 100%; margin: 0; padding: 0; overflow: hidden; }
iframe { border: 0; width: 100%; height: 100%; background: #fff; }
</style>
</head>
<body><iframe src="${address}" title="The Cromo Guide"></iframe></body>
</html>`;
}

async function open() {
    const address = SITE + await guideLanguage() + '/';
    if (!panel) {
        panel = vscode.window.createWebviewPanel('cromoGuide', 'The Cromo Guide', vscode.ViewColumn.Beside, {
            enableScripts: true,  // of the pages of the guide, inside their frame
            retainContextWhenHidden: true,
        });
        panel.onDidDispose(() => { panel = null; });
    }
    if (panel.address !== address) {
        panel.address = address;
        panel.webview.html = page(address);
    }
    panel.reveal();
}

function register(context) {
    context.subscriptions.push(
        vscode.commands.registerCommand('cromo.guide', open),
        // The guide in the browser, as when the panel has no network to show.
        vscode.commands.registerCommand('cromo.guideInBrowser', async () => {
            vscode.env.openExternal(vscode.Uri.parse(SITE + await guideLanguage() + '/'));
        }),
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (panel && event.affectsConfiguration('cromo.guideLanguage')) open();
        }),
    );
}

module.exports = { register };
