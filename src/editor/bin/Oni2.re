/*
 * Oni2.re
 *
 * This is the entry point for launching the editor.
 */

open Revery;
open Revery.Core;
open Revery.UI;

open Rench;

open Oni_UI;
open Oni_Neovim;

module Core = Oni_Core;

/* The 'main' function for our app */
let init = app => {
  let w =
    App.createWindow(
      ~createOptions={
        ...Window.defaultCreateOptions,
        vsync: false,
        maximized: false,
      },
      app,
      "Oni2",
    );

  let render = () => {
    let state: Core.State.t = App.getState(app);
    GlobalContext.set({
      notifySizeChanged: (~width, ~height, ()) =>
        App.dispatch(
          app,
          Core.Actions.SetEditorSize(
            Core.Types.EditorSize.create(
              ~pixelWidth=width,
              ~pixelHeight=height,
              (),
            ),
          ),
        ),
    });
    prerr_endline(
      "[DEBUG - STATE] Mode: "
      ++ Core.Types.Mode.show(state.mode)
      ++ " editor font measured width: "
      ++ string_of_int(state.editorFont.measuredWidth)
      ++ " editor font measured height: "
      ++ string_of_int(state.editorFont.measuredHeight),
    );
    <Root state />;
  };

  UI.start(w, render);

  let initVimPath =
    Revery_Core.Environment.getExecutingDirectory() ++ "init.vim";
  Core.Log.debug("initVimPath: " ++ initVimPath);

  let {neovimPath, _}: Oni_Core.Setup.t = Oni_Core.Setup.init();

  let nvim =
    NeovimProcess.start(~neovimPath, ~args=[|"-u", initVimPath, "--embed"|]);
  let msgpackTransport =
    MsgpackTransport.make(
      ~onData=nvim.stdout.onData,
      ~write=nvim.stdin.write,
      (),
    );

  let nvimApi: NeovimApi.t = NeovimApi.make(msgpackTransport);
  let neovimProtocol: NeovimProtocol.t = NeovimProtocol.make(nvimApi);

  neovimProtocol.uiAttach();

  let parseContextMap = map =>
    Core.(
      Types.(
        List.fold_left(
          (accum, item) =>
            switch (item) {
            | (Msgpck.String("bufferFullPath"), Msgpck.String(value)) => {
                ...accum,
                filepath: value,
              }
            | (Msgpck.String("bufferNumber"), Msgpck.Int(bufNum)) => {
                ...accum,
                id: bufNum,
              }
            | (Msgpck.String("modified"), Msgpck.Int(modified)) => {
                ...accum,
                modified: modified == 1,
              }
            | (Msgpck.String("buftype"), Msgpck.String(buftype)) => {
                ...accum,
                buftype: getBufType(buftype),
              }
            | (Msgpck.String("filetype"), Msgpck.String(filetype)) => {
                ...accum,
                filetype,
              }
            | (Msgpck.String("hidden"), Msgpck.Bool(hidden)) => {
                ...accum,
                hidden,
              }
            | _ => accum
            },
          {
            filepath: "",
            id: 0,
            buftype: Empty,
            filetype: "",
            modified: false,
            hidden: false,
          },
          map,
        )
      )
    );

  /**
     Enrich Buffers

     Neovim allows us to get a list of all buffer ids
     we then use the IDs of the buffers to request more
     metadata about each buffer using "atomic calls"
   */
  let enrichBuffers = ((atomicCalls, buffers)) => {
    let rsp =
      nvimApi.requestSync(
        "nvim_call_atomic",
        Msgpck.List([Msgpck.List(atomicCalls)]),
      );
    switch (rsp) {
    | Msgpck.List(result) =>
      /**
      The last argument in an atomic call is either NIL or an
      array of three items  a three-element array with the zero-based
      index of the call which resulted in an error, the error type
      and the error message. If an error occurred,
      the values from all preceding calls will still be returned.
     */
      (
        switch (List.rev(result)) {
        | [] => []
        | [_errors, ...responseItems] =>
          switch (responseItems) {
          | [Msgpck.List(items)] =>
            List.fold_left2(
              (accum, buf, bufferContext) =>
                switch (bufferContext) {
                | Msgpck.Map(context) => [
                    parseContextMap(context),
                    ...accum,
                  ]
                | _ => [buf, ...accum]
                },
              [],
              buffers,
              items,
            )
          | _ => []
          }
        }
      )
    | _ => []
    };
  };

  let constructMetadataCalls = id => [
    Msgpck.List([
      Msgpck.String("nvim_call_function"),
      Msgpck.List([
        Msgpck.String("OniGetBufferContext"),
        Msgpck.List([Msgpck.Int(id)]),
      ]),
    ]),
  ];

  let getFullBufferList = () => {
    let bufs = nvimApi.requestSync("nvim_list_bufs", Msgpck.List([]));
    (
      switch (bufs) {
      | Msgpck.List(handles) =>
        List.fold_left(
          ((calls, bufs), buffer) =>
            switch (Core.Utility.convertNeovimExtType(buffer)) {
            | Some((_, id)) =>
              let newCalls =
                constructMetadataCalls(id) |> List.append(calls) |> List.rev;

              let newBuffers =
                [
                  Core.Types.{
                    id,
                    modified: false,
                    hidden: false,
                    buftype: Empty,
                    filetype: "",
                    filepath: "[No Name]",
                  },
                  ...bufs,
                ]
                |> List.rev;

              (newCalls, newBuffers);
            | None => (calls, bufs)
            },
          ([], []),
          handles,
        )
      | _ => ([], [])
      }
    )
    |> enrichBuffers;
  };

  let bufferAttach = bufferId =>
    ignore(
      nvimApi.requestSync(
        "nvim_buf_attach",
        Msgpck.List([
          Msgpck.Int(bufferId),
          Msgpck.Bool(true),
          Msgpck.Map([]),
        ]),
      ),
    );

  let setFont = (fontFamily, fontSize) =>
    Fontkit.fk_new_face(
      Revery.Core.Environment.getExecutingDirectory() ++ fontFamily,
      fontSize,
      font => {
        open Oni_Core.Actions;
        open Oni_Core.Types;

        /* Measure text */
        let shapedText = Fontkit.fk_shape(font, "H");
        let firstShape = shapedText[0];
        let glyph = Fontkit.renderGlyph(font, firstShape.glyphId);

        let metrics = Fontkit.fk_get_metrics(font);
        let actualHeight =
          int_of_float(
            float_of_int(fontSize)
            *. float_of_int(metrics.height)
            /. float_of_int(metrics.unitsPerEm),
          );

        /* Set editor text based on measurements */
        App.dispatch(
          app,
          SetEditorFont(
            EditorFont.create(
              ~fontFile=fontFamily,
              ~fontSize,
              ~measuredWidth=glyph.advance / 64,
              ~measuredHeight=actualHeight,
              (),
            ),
          ),
        );
      },
      _ => prerr_endline("setFont: Failed to load font " ++ fontFamily),
    );

  setFont("FiraCode-Regular.ttf", 14);

  let _ =
    Event.subscribe(
      w.onKeyPress,
      event => {
        let c = event.character;
        neovimProtocol.input(c);
      },
    );

  let _ =
    Event.subscribe(
      w.onKeyDown,
      event => {
        let _ =
          switch (event.key, event.shiftKey, event.ctrlKey) {
          | (Key.KEY_TAB, true, _) =>
            ignore(neovimProtocol.input("<S-TAB>"))
          | (Key.KEY_BACKSPACE, _, _) =>
            ignore(neovimProtocol.input("<BS>"))
          | (Key.KEY_ENTER, _, _) => ignore(neovimProtocol.input("<CR>"))
          | (Key.KEY_ESCAPE, _, _) => ignore(neovimProtocol.input("<ESC>"))
          | (Key.KEY_TAB, _, _) => ignore(neovimProtocol.input("<TAB>"))
          | (Key.KEY_RIGHT_SHIFT, _, _)
          | (Key.KEY_LEFT_SHIFT, _, _) =>
            ignore(neovimProtocol.input("<SHIFT>"))
          | (Key.KEY_UP, _, _) => ignore(neovimProtocol.input("<UP>"))
          | (Key.KEY_LEFT, _, _) => ignore(neovimProtocol.input("<LEFT>"))
          | (Key.KEY_RIGHT, _, _) => ignore(neovimProtocol.input("<RIGHT>"))
          | (Key.KEY_DOWN, _, _) => ignore(neovimProtocol.input("<DOWN>"))
          | _ => ()
          };
        ();
      },
    );

  let _ = Tick.interval(_ => nvimApi.pump(), Seconds(0.));

  let _ =
    Event.subscribe(nvimApi.onNotification, n =>
      prerr_endline(
        "Raw Notification: "
        ++ n.notificationType
        ++ " | "
        ++ Msgpck.show(n.payload),
      )
    );

  let _ =
    Event.subscribe(
      neovimProtocol.onNotification,
      n => {
        let msg =
          switch (n) {
          | ModeChanged("normal") => Core.Actions.ChangeMode(Normal)
          | ModeChanged("insert") => Core.Actions.ChangeMode(Insert)
          | ModeChanged("cmdline_normal") =>
            Core.Actions.ChangeMode(Commandline)
          | TablineUpdate(tabs) => Core.Actions.TablineUpdate(tabs)
          | ModeChanged(_) => Core.Actions.ChangeMode(Other)
          | CursorMoved(c) =>
            Core.Actions.CursorMove(
              Core.Types.BufferPosition.create(c.cursorLine, c.cursorColumn),
            )
          | BufferEnter(b) =>
            bufferAttach(b.bufferId);
            Core.Actions.BufferEnter({
              bufferId: b.bufferId,
              buffers: getFullBufferList(),
            });
          | BufferLines(bc) =>
            Core.Actions.BufferUpdate(
              Core.Types.BufferUpdate.create(
                ~startLine=bc.firstLine,
                ~endLine=bc.lastLine,
                ~lines=bc.lines,
                (),
              ),
            )
          | WildmenuShow(w) => Core.Actions.WildmenuShow(w)
          | WildmenuHide(w) => Core.Actions.WildmenuHide(w)
          | WildmenuSelected(s) => Core.Actions.WildmenuSelected(s)
          | CommandlineShow(c) => Core.Actions.CommandlineShow(c)
          | CommandlineHide(c) => Core.Actions.CommandlineHide(c)
          | _ => Noop
          };

        App.dispatch(app, msg);
        prerr_endline("Protocol Notification: " ++ Notification.show(n));
      },
    );
  ();
};

/* Let's get this party started! */
App.startWithState(Core.State.create(), Core.Reducer.reduce, init);
