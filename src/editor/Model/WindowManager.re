open Revery_UI;

[@deriving show]
type layout =
  | VerticalLeft
  | VerticalRight
  | HorizontalTop
  | HorizontalBottom;

module WindowSplitId =
  Revery.UniqueId.Make({});

[@deriving show]
type componentCreator = unit => React.syntheticElement;

[@deriving show]
type split = {
  id: int,
  parentId: int,
  component: componentCreator,
  layout,
  /* if omitted the split will grow to occupy whatever space is available */
  width: option(int),
  height: option(int),
};

[@deriving show]
type direction =
  | Horizontal
  | Vertical;

[@deriving show]
type splitTree =
  | Parent(direction, int, list(splitTree))
  | Leaf(split);

[@deriving show]
type splits = splitTree;

type t = {splits};

let empty = Parent(Vertical, 0, []);

let create = (): t => {splits: empty};

let getId = (id: option(int)) =>
  switch (id) {
  | Some(i) => i
  | None => WindowSplitId.getUniqueId()
  };

let createSplit =
    (~id=?, ~parentId, ~width=?, ~height=?, ~component, ~layout, ()) => {
  id: getId(id),
  parentId,
  component,
  width,
  height,
  layout,
};

type splitAction('a) = split => 'a;

let rec traverseSplitTree = (tree, action: splitAction('a), result) =>
  switch (tree) {
  | Parent(_, _, children) =>
    List.fold_left(
      (accum, child) => traverseSplitTree(child, action, accum),
      result,
      children,
    )
  | Leaf(split) => action(split, result)
  };

let rec add = (id, split, tree) =>
  switch (tree) {
  | Parent(direction, parentId, children) when id == parentId =>
    Parent(direction, parentId, [Leaf(split), ...children])
  | Parent(direction, parentId, children) =>
    List.map(child => add(id, split, child), children)
    |> (newChildren => Parent(direction, parentId, newChildren))
  | Leaf(_) => tree
  };
