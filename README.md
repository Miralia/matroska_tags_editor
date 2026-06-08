# Matroska Tags Editor

Matroska Tags Editor is a native desktop tool for editing Matroska string
`SimpleTag` metadata in `.mkv`, `.mka`, `.mk3d`, and `.mks` files.

## Usage

- Open a Matroska file with **Open** or drag and drop it into the window.
- Select `Global` or a track group such as `Video#1` or `Audio#1`.
- Use the toolbar or right-click menu to add, delete, copy, paste, undo, and
  redo tag edits.
- Click a `Name` or `Value` cell to edit it inline.
- Click **Save** to write the updated tags back to the file.

Binary tags and mkvmerge statistics tags are preserved when saving, but they are
not shown for editing.
