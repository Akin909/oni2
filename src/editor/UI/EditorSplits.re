/**
   Editor Splits

   This modules interprets the current state of the window
   manager and arranges its children accordingly
 */
open Revery;
open UI;
open Oni_Model;
open WindowManager;

let component = React.component("EditorSplits");

let splitContainer = Style.[flexGrow(1), flexDirection(`Row)];

let splitStyle = (split: split) =>
  Style.(
    switch (split) {
    | {direction: Vertical, width: Some(w), _} => [width(w), flexGrow(1)]
    | {direction: Vertical, width: None, _} => [flexGrow(1)]
    | {direction: Horizontal, height: None, _} => [flexGrow(1)]
    | {direction: Horizontal, height: Some(h), _} => [
        height(h),
        flexGrow(1),
      ]
    }
  );

let getDockStyle = ({width, _}: dock) => {
  let w =
    switch (width) {
    | Some(w) => w
    | None => 10
    };
  Style.[width(w), top(0), bottom(0)];
};

let renderDock = (dockItems: list(dock), state: State.t) =>
  List.fold_left(
    (accum, item) =>
      [
        <View style={getDockStyle(item)}> {item.component()} </View>,
        <WindowHandle direction=Vertical theme={state.theme} />,
        ...accum,
      ],
    [],
    dockItems,
  );

let debugParent = (isDebugMode, direction) => {
  let debugColor =
    switch (direction) {
    | Vertical => Colors.red
    | Horizontal => Colors.green
    };
  isDebugMode ? Style.[border(~width=2, ~color=debugColor)] : [];
};

let parentStyle = (dir: direction) => {
  let flexDir =
    switch (dir) {
    | Vertical => `Row
    | Horizontal => `Column
    };
  Style.[flexGrow(1), flexDirection(flexDir), ...debugParent(true, dir)];
};

let rec renderTree = (~direction, theme, tree) =>
  switch (tree) {
  | Parent(direction, _, children) =>
    <View style={parentStyle(direction)}>
      ...{List.map(renderTree(~direction, theme), children)}
    </View>
  | Leaf(window) =>
    <View style={splitStyle(window)}>
      {window.component()}
      <WindowHandle direction theme />
    </View>
  };

let createElement = (~children as _, ~state: State.t, ()) =>
  component(hooks => {
    let {State.editorLayout, theme, _} = state;

    let splits =
      renderDock(editorLayout.rightDock, state)
      |> (@)([renderTree(~direction=Vertical, theme, editorLayout.windows)])
      |> (@)(renderDock(editorLayout.leftDock, state));
    (hooks, <View style=splitContainer> ...splits </View>);
  });
