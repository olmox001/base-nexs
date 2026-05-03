const vscode = require('vscode');
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');

const SOCKET_PATH = '/tmp/nexsd.sock';

/**
 * @param {vscode.ExtensionContext} context
 */
function activate(context) {
    const diagnosticCollection = vscode.languages.createDiagnosticCollection('nexs');
    let timeout = null;
    let nexsdProcess = null;

    // Start nexsd daemon automatically
    const startDaemon = () => {
        const workspaceFolder = vscode.workspace.workspaceFolders ? vscode.workspace.workspaceFolders[0] : null;
        if (!workspaceFolder) return;

        const nexsdBin = path.join(workspaceFolder.uri.fsPath, 'tools', 'nexsd', 'nexsd');
        
        nexsdProcess = spawn(nexsdBin, [], { 
            cwd: workspaceFolder.uri.fsPath,
            detached: true,
            stdio: 'ignore'
        });
        
        nexsdProcess.unref();
        console.log('nexsd daemon started from extension');
    };

    const stopDaemon = () => {
        const client = net.createConnection(SOCKET_PATH, () => {
            client.write('SHUTDOWN');
            client.end();
        });
        client.on('error', () => {
            // Daemon likely already dead
        });
    };

    startDaemon();

    const symbolMap = new Map();

    const triggerLint = (document) => {
        if (!document.uri.fsPath.endsWith('.nx')) {
            diagnosticCollection.delete(document.uri);
            return;
        }
        if (timeout) clearTimeout(timeout);
        timeout = setTimeout(() => lintWithDaemon(document), 150); 
    };

    const lintWithDaemon = (document) => {
        const client = net.createConnection(SOCKET_PATH, () => {
            const fileName = document.uri.fsPath;
            const content = document.getText();
            const payload = Buffer.concat([
                Buffer.from(fileName),
                Buffer.from([0]),
                Buffer.from(content)
            ]);
            client.write(payload);
            client.end();
        });

        let response = '';
        client.on('data', (data) => response += data.toString());
        client.on('end', () => updateDiagnostics(document, response));
        client.on('error', () => {
            // Fallback to CLI if daemon is down
            lintWithCLI(document);
        });
    };

    const lintWithCLI = (document) => {
        const workspaceFolder = vscode.workspace.getWorkspaceFolder(document.uri);
        if (!workspaceFolder) return;
        const nexsBin = path.join(workspaceFolder.uri.fsPath, 'nexs');
        const child = spawn(nexsBin, ['--lint', '-'], { cwd: workspaceFolder.uri.fsPath });
        let stderr = '';
        child.stderr.on('data', (data) => stderr += data.toString());
        child.on('close', () => updateDiagnostics(document, stderr));
        child.stdin.write(document.getText());
        child.stdin.end();
    };

    const updateDiagnostics = (document, output) => {
        diagnosticCollection.delete(document.uri);
        const parts = output.split('---SYMBOLS---');
        const diagOutput = parts[0];
        const symOutput = parts[1] || '';

        symbolMap.clear();
        const symLines = symOutput.trim().split('\n');
        for (const line of symLines) {
            const [name, file, l, c, p, is_c] = line.split(':');
            if (name) symbolMap.set(name, { file, line: parseInt(l)-1, col: parseInt(c)-1, params: p, is_c: is_c === '1' });
        }

        if (diagOutput.trim() === 'OK') return;
        const diagnostics = {};
        const lines = diagOutput.split('\n');
        for (const line of lines) {
            const match = line.match(/^(.*?):(\d+):(\d+):\s+(warning|error):\s+(.*)$/);
            if (match) {
                const fileMatch = match[1];
                const lineNum = Math.max(0, parseInt(match[2]) - 1);
                const colNum = Math.max(0, parseInt(match[3]) - 1);
                const severity = match[4] === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error;
                const message = match[5];
                const range = new vscode.Range(new vscode.Position(lineNum, colNum), new vscode.Position(lineNum, colNum + 5));
                const targetUri = (fileMatch === 'stdin.nx' || document.uri.fsPath.endsWith(fileMatch)) ? document.uri : vscode.Uri.file(fileMatch);
                const uriStr = targetUri.toString();
                if (!diagnostics[uriStr]) diagnostics[uriStr] = [];
                diagnostics[uriStr].push(new vscode.Diagnostic(range, message, severity));
            }
        }
        for (const uriStr in diagnostics) diagnosticCollection.set(vscode.Uri.parse(uriStr), diagnostics[uriStr]);
    };

    const hoverProvider = vscode.languages.registerHoverProvider('nexs', {
        provideHover(document, position) {
            const range = document.getWordRangeAtPosition(position);
            const word = document.getText(range);
            const sym = symbolMap.get(word);
            if (sym) {
                const type = sym.is_c ? 'Native C Built-in' : 'NeXs Function';
                const fileType = sym.is_c ? 'C Source' : 'NX Script';
                return new vscode.Hover(`**${type}**: \`${word}\`\n\n**Defined in**: \`${path.basename(sym.file)}\` (${fileType})\n\n**Location**: line ${sym.line + 1}`);
            }
            return null;
        }
    });

    const definitionProvider = vscode.languages.registerDefinitionProvider('nexs', {
        provideDefinition(document, position) {
            const range = document.getWordRangeAtPosition(position);
            const word = document.getText(range);
            const sym = symbolMap.get(word);
            if (sym) return new vscode.Location(vscode.Uri.file(sym.file), new vscode.Position(sym.line, sym.col));
            return null;
        }
    });

    context.subscriptions.push(
        vscode.workspace.onDidChangeTextDocument(event => triggerLint(event.document)),
        vscode.workspace.onDidOpenTextDocument(lintDocument => triggerLint(lintDocument)),
        vscode.workspace.onDidSaveTextDocument(lintDocument => triggerLint(lintDocument)),
        diagnosticCollection, hoverProvider, definitionProvider
    );

    context.subscriptions.push({ dispose: stopDaemon });

    if (vscode.window.activeTextEditor) triggerLint(vscode.window.activeTextEditor.document);
}

function deactivate() {}

module.exports = { activate, deactivate };
