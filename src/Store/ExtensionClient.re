open Oni_Core;
open Oni_Core.Utility;
open Oni_Model;

module Log = (val Log.withNamespace("Oni2.Extension.ClientStore"));

module LanguageFeatures = Feature_LanguageSupport.LanguageFeatures;

module ExtensionDocumentSymbolProvider = {
  let create =
      (
        id,
        selector,
        _label, // TODO: What to do with label?
        client,
        buffer,
      ) => {
    ProviderUtility.runIfSelectorPasses(~buffer, ~selector, () => {
      Exthost.Request.LanguageFeatures.provideDocumentSymbols(
        ~handle=id,
        ~resource=Buffer.getUri(buffer),
        client,
      )
    });
  };
};

let create = (~config, ~extensions, ~setup: Setup.t) => {
  let (stream, dispatch) = Isolinear.Stream.create();

  let extensionInfo =
    extensions |> List.map(Exthost.Extension.InitData.Extension.ofScanResult);

  let onRegisterDocumentSymbolProvider = (handle, selector, label, client) => {
    let id = "exthost." ++ string_of_int(handle);
    let documentSymbolProvider =
      ExtensionDocumentSymbolProvider.create(handle, selector, label, client);

    dispatch(
      Actions.LanguageFeature(
        LanguageFeatures.DocumentSymbolProviderAvailable(
          id,
          documentSymbolProvider,
        ),
      ),
    );
  };
  open Exthost;
  open Exthost.Extension;
  open Exthost.Msg;

  let maybeClientRef = ref(None);

  let withClient = f =>
    switch (maybeClientRef^) {
    | None => Log.warn("Warning - withClient does not have a client")
    | Some(client) => f(client)
    };

  let handler: Msg.t => Lwt.t(Reply.t) =
    msg => {
      switch (msg) {
      | DownloadService(msg) => Middleware.download(msg)
      | FileSystem(msg) => Middleware.filesystem(msg)
      | SCM(msg) =>
        Feature_SCM.handleExtensionMessage(
          ~dispatch=msg => dispatch(Actions.SCM(msg)),
          msg,
        );
        Lwt.return(Reply.okEmpty);

      | Storage(msg) =>
        let (promise, resolver) = Lwt.task();

        let storageMsg = Feature_Extensions.Msg.storage(~resolver, msg);
        dispatch(Extensions(storageMsg));

        promise;

      | Configuration(RemoveConfigurationOption({key, _})) =>
        dispatch(
          Actions.ConfigurationTransform(
            "configuration.json",
            ConfigurationTransformer.removeField(key),
          ),
        );
        Lwt.return(Reply.okEmpty);

      | Configuration(UpdateConfigurationOption({key, value, _})) =>
        dispatch(
          Actions.ConfigurationTransform(
            "configuration.json",
            ConfigurationTransformer.setField(key, value),
          ),
        );
        Lwt.return(Reply.okEmpty);

      | LanguageFeatures(
          RegisterDocumentSymbolProvider({handle, selector, label}),
        ) =>
        withClient(
          onRegisterDocumentSymbolProvider(handle, selector, label),
        );
        Lwt.return(Reply.okEmpty);

      | Diagnostics(diagnosticMsg) =>
        dispatch(
          Actions.Diagnostics(
            Feature_Diagnostics.Msg.exthost(diagnosticMsg),
          ),
        );
        Lwt.return(Reply.okEmpty);

      | DocumentContentProvider(documentContentProviderMsg) =>
        dispatch(
          Actions.SCM(
            Feature_SCM.Msg.documentContentProvider(
              documentContentProviderMsg,
            ),
          ),
        );

        Lwt.return(Reply.okEmpty);

      | Decorations(decorationsMsg) =>
        dispatch(
          Decorations(Feature_Decorations.Msg.exthost(decorationsMsg)),
        );
        Lwt.return(Reply.okEmpty);

      | Documents(documentsMsg) =>
        let (promise, resolver) = Lwt.task();
        dispatch(
          Actions.Exthost(
            Feature_Exthost.Msg.document(documentsMsg, resolver),
          ),
        );
        promise;

      | ExtensionService(extMsg) =>
        Log.infof(m => m("ExtensionService: %s", Exthost.Msg.show(msg)));
        dispatch(
          Actions.Extensions(Feature_Extensions.Msg.exthost(extMsg)),
        );
        Lwt.return(Reply.okEmpty);

      | LanguageFeatures(
          RegisterSignatureHelpProvider({handle, selector, metadata}),
        ) =>
        dispatch(
          Actions.SignatureHelp(
            Feature_SignatureHelp.ProviderRegistered({
              handle,
              selector,
              metadata,
            }),
          ),
        );
        Lwt.return(Reply.okEmpty);

      | Languages(msg) =>
        let (promise, resolver) = Lwt.task();

        let languagesMsg = Feature_Extensions.Msg.languages(~resolver, msg);
        dispatch(Extensions(languagesMsg));

        promise;

      | LanguageFeatures(msg) =>
        dispatch(
          Actions.LanguageSupport(Feature_LanguageSupport.Msg.exthost(msg)),
        );
        Lwt.return(Reply.okEmpty);

      | MessageService(msg) =>
        Feature_Messages.Msg.exthost(
          ~dispatch=msg => dispatch(Actions.Messages(msg)),
          msg,
        )
        |> Lwt.map(
             fun
             | None => Reply.okEmpty
             | Some(handle) =>
               Reply.okJson(Exthost.Message.handleToJson(handle)),
           )

      | QuickOpen(msg) =>
        switch (msg) {
        | QuickOpen.Show({instance, _}) =>
          let (promise, resolver) = Lwt.task();
          dispatch(
            QuickmenuShow(
              Extension({id: instance, hasItems: false, resolver}),
            ),
          );

          promise |> Lwt.map(handle => Reply.okJson(`Int(handle)));
        | QuickOpen.SetItems({instance, items}) =>
          dispatch(QuickmenuUpdateExtensionItems({id: instance, items}));
          Lwt.return(Reply.okEmpty);
        | msg =>
          // TODO: Additional quick open messages
          Log.warnf(m =>
            m(
              "Unhandled QuickOpen message: %s",
              Exthost.Msg.QuickOpen.show_msg(msg),
            )
          );
          Lwt.return(Reply.okEmpty);
        }

      | StatusBar(
          SetEntry({
            id,
            label,
            alignment,
            priority,
            color,
            command,
            tooltip,
            _,
          }),
        ) =>
        let command =
          command |> Option.map(({id, _}: Exthost.Command.t) => id);
        dispatch(
          Actions.StatusBar(
            Feature_StatusBar.ItemAdded(
              Feature_StatusBar.Item.create(
                ~command?,
                ~color?,
                ~tooltip?,
                ~id,
                ~label,
                ~alignment,
                ~priority,
                (),
              ),
            ),
          ),
        );
        Lwt.return(Reply.okEmpty);

      | StatusBar(Dispose({id})) =>
        dispatch(Actions.StatusBar(ItemDisposed(id |> string_of_int)));
        Lwt.return(Reply.okEmpty);

      | TerminalService(msg) =>
        Service_Terminal.handleExtensionMessage(msg);
        Lwt.return(Reply.okEmpty);
      | Window(OpenUri({uri})) =>
        Service_OS.Api.openURL(uri |> Oni_Core.Uri.toString)
          ? Lwt.return(Reply.okEmpty)
          : Lwt.return(Reply.error("Unable to open URI"))
      | Window(GetWindowVisibility) =>
        Lwt.return(Reply.okJson(`Bool(true)))
      | Workspace(StartFileSearch({includePattern, excludePattern, _})) =>
        Service_OS.Api.glob(
          ~includeFiles=?includePattern,
          ~excludeDirectories=?excludePattern,
          // TODO: Pull from store
          Sys.getcwd(),
        )
        |> Lwt.map(paths =>
             Reply.okJson(
               `List(
                 paths
                 |> List.map(str =>
                      Oni_Core.Uri.fromPath(str) |> Oni_Core.Uri.to_yojson
                    ),
               ),
             )
           )
      | unhandledMsg =>
        Log.warnf(m =>
          m("Unhandled message: %s", Exthost.Msg.show(unhandledMsg))
        );
        Lwt.return(Reply.okEmpty);
      };
    };

  let parentPid = Luv.Pid.getpid();
  let name = Printf.sprintf("exthost-client-%s", parentPid |> string_of_int);
  let namedPipe = name |> NamedPipe.create;
  let pipeStr = NamedPipe.toString(namedPipe);

  let tempDir = Filename.get_temp_dir_name();

  let logsLocation = tempDir |> Uri.fromPath;
  let logFile =
    Filename.temp_file(~temp_dir=tempDir, "onivim2", "exthost.log")
    |> Uri.fromPath;

  let initData =
    InitData.create(
      ~version="1.46.0", // TODO: How to keep in sync with bundled version?
      ~parentPid,
      ~logsLocation,
      ~logFile,
      ~logLevel=0,
      extensionInfo,
    );

  let onError = err => {
    Log.error(err);
  };

  let client =
    Exthost.Client.start(
      ~initialConfiguration=
        Feature_Configuration.toExtensionConfiguration(
          config,
          extensions,
          setup,
        ),
      ~namedPipe,
      ~initData,
      ~handler,
      ~onError,
      (),
    );

  let env = Luv.Env.environ() |> Result.get_ok;
  let environment = [
    (
      "AMD_ENTRYPOINT",
      "vs/workbench/services/extensions/node/extensionHostProcess",
    ),
    ("VSCODE_IPC_HOOK_EXTHOST", pipeStr),
    ("VSCODE_PARENT_PID", parentPid |> string_of_int),
    ...env,
  ];

  let nodePath = Setup.(setup.nodePath);
  let extHostScriptPath = Setup.getNodeExtensionHostPath(setup);

  let on_exit = (_, ~exit_status: int64, ~term_signal) => {
    Log.infof(m =>
      m(
        "Extension host process exited with exit status: %Ld and signal: %d",
        exit_status,
        term_signal,
      )
    );
  };

  let redirect =
    if (Timber.App.isEnabled()) {
      [
        Luv.Process.inherit_fd(
          ~fd=Luv.Process.stdin,
          ~from_parent_fd=Luv.Process.stdin,
          (),
        ),
        Luv.Process.inherit_fd(
          ~fd=Luv.Process.stdout,
          ~from_parent_fd=Luv.Process.stderr,
          (),
        ),
        Luv.Process.inherit_fd(
          ~fd=Luv.Process.stderr,
          ~from_parent_fd=Luv.Process.stderr,
          (),
        ),
      ];
    } else {
      [];
    };

  let _process: Luv.Process.t =
    LuvEx.Process.spawn(
      ~environment,
      ~on_exit,
      ~redirect,
      nodePath,
      [nodePath, extHostScriptPath],
    )
    // TODO: More robust error handling
    |> Result.get_ok;

  client |> Result.iter(c => maybeClientRef := Some(c));

  (client, stream);
};
