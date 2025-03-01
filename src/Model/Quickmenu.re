open Actions;

type t = {
  variant,
  prefix: option(string),
  inputText: Component_InputText.model,
  items: array(menuItem),
  filterProgress: progress,
  ripgrepProgress: progress,
  focused: option(int) // TODO: Might not be a great idea to use an index to refer to a specific item in an array that changes over time
}

and variant =
  Actions.quickmenuVariant =
    | CommandPalette
    | EditorsPicker
    | FilesPicker
    | Wildmenu(Vim.Types.cmdlineType)
    | ThemesPicker(list(Feature_Theme.theme))
    | FileTypesPicker({
        bufferId: int,
        languages:
          list((string, option(Oni_Core.IconTheme.IconDefinition.t))),
      })
    | DocumentSymbols
    | Extension({
        id: int,
        hasItems: bool,
        resolver: Lwt.u(int),
      });

let placeholderText =
  fun
  | FilesPicker
  | ThemesPicker(_)
  | CommandPalette
  | DocumentSymbols => "type to search..."
  | _ => "";

let defaults = variant => {
  variant,
  prefix: None,
  inputText:
    Component_InputText.create(~placeholder=placeholderText(variant)),
  focused: None,
  items: [||],
  filterProgress: Complete,
  ripgrepProgress: Complete,
};

// TODO: This doesn't really belong here. Find a better home for it.
let getLabel = (item: menuItem) => {
  switch (item.category) {
  | Some(v) => v ++ ": " ++ item.name
  | None => item.name
  };
};
