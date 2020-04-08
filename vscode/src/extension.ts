import * as vscode from 'vscode';
import * as vscodelc from 'vscode-languageclient';

/**
 * Method to get workspace configuration option
 * @param option name of the option (e.g. for ekam.path should be path)
 * @param defaultValue default value to return if option is not set
 */
function getConfig<T>(option: string, defaultValue?: any): T {
    const config = vscode.workspace.getConfiguration('ekam');
    return config.get<T>(option, defaultValue);
}

/**
 *  this method is called when your extension is activate
 *  your extension is activated the very first time the command is executed
 */
export function activate(context: vscode.ExtensionContext) {
    const syncFileEvents = getConfig<boolean>('syncFileEvents', true);

    const options: vscodelc.Executable = {
        command: getConfig<string>('path'),
        args: getConfig<string[]>('arguments')
    };
    const serverOptions: vscodelc.ServerOptions = options;

    const clientOptions: vscodelc.LanguageClientOptions = {
      documentSelector: [{ scheme: 'file' }]
    };

    const ekamClient = new vscodelc.LanguageClient('Ekam Language Server', serverOptions, clientOptions);
    console.log('Ekam Language Server is now active!');

    const disposable = ekamClient.start();
    context.subscriptions.push(disposable);
}
