#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treelist.h>
#include <wx/wx.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/matroska_reader.h"
#include "core/matroska_writer.h"

namespace {

enum : int {
  ID_TREE = wxID_HIGHEST + 1,
  ID_ADD_TAG,
  ID_DELETE_TAG,
  ID_COPY_TAG,
  ID_CUT_TAG,
  ID_PASTE_TAG,
  ID_SELECT_ALL,
  ID_UNDO,
  ID_REDO
};

enum class ItemKind {
  Group,
  Field
};

class ItemData final : public wxClientData {
 public:
  ItemKind kind = ItemKind::Group;
  mte::EditableTarget target;
  std::size_t tag_index = 0;
  std::vector<std::size_t> simple_path;
};

std::string track_type_name(std::uint64_t type) {
  switch (type) {
    case 1:
      return "Video";
    case 2:
      return "Audio";
    case 17:
      return "Subtitle";
    default:
      return "Track";
  }
}

std::string track_label(const mte::TrackInfo& track,
                        const std::vector<mte::TrackInfo>& tracks) {
  auto ordinal = 0;
  for (const auto& current : tracks) {
    if (current.type == track.type) {
      ++ordinal;
    }
    if (current.uid == track.uid) {
      break;
    }
  }

  std::ostringstream label;
  label << track_type_name(track.type) << "#" << ordinal;
  if (!track.name.empty()) {
    label << " (" << track.name << ")";
  }
  return label.str();
}

std::string group_label(const mte::EditableTarget& target,
                        const std::vector<mte::TrackInfo>& tracks) {
  std::string prefix = "General";
  if (target.kind == mte::EditableTargetKind::Track) {
    prefix = "Track " + std::to_string(target.track_uid);
    for (const auto& track : tracks) {
      if (track.uid == target.track_uid) {
        prefix = track_label(track, tracks);
        break;
      }
    }
  } else if (target.kind == mte::EditableTargetKind::Other) {
    prefix = "Other targets";
  }

  return prefix;
}

bool same_target(const mte::EditableTarget& left, const mte::EditableTarget& right) {
  return left.kind == right.kind && left.track_uid == right.track_uid;
}

mte::SimpleTag* find_simple(mte::TagEntry& entry,
                            const std::vector<std::size_t>& path) {
  if (path.empty() || path[0] >= entry.simple_tags.size()) {
    return nullptr;
  }

  auto* current = &entry.simple_tags[path[0]];
  for (std::size_t i = 1; i < path.size(); ++i) {
    if (path[i] >= current->children.size()) {
      return nullptr;
    }
    current = &current->children[path[i]];
  }
  return current;
}

void erase_simple(mte::TagEntry& entry, const std::vector<std::size_t>& path) {
  if (path.empty()) {
    return;
  }
  if (path.size() == 1) {
    if (path[0] < entry.simple_tags.size()) {
      entry.simple_tags.erase(entry.simple_tags.begin() + static_cast<std::ptrdiff_t>(path[0]));
    }
    return;
  }

  std::vector<std::size_t> parent_path(path.begin(), path.end() - 1);
  auto* parent = find_simple(entry, parent_path);
  const auto index = path.back();
  if (parent && index < parent->children.size()) {
    parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index));
  }
}

void erase_visible_simple_tags(std::vector<mte::SimpleTag>& tags) {
  for (auto& tag : tags) {
    erase_visible_simple_tags(tag.children);
  }
  tags.erase(std::remove_if(tags.begin(), tags.end(), [](const mte::SimpleTag& tag) {
               return tag.value_type == mte::TagValueType::String &&
                      !mte::is_hidden_extra_tag_name(tag.name);
             }),
             tags.end());
}

class FieldEditDialog final : public wxDialog {
 public:
  FieldEditDialog(wxWindow* parent,
                  const std::string& title,
                  const std::string& name,
                  const std::string& value,
                  bool name_editable)
      : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    name_ = AddTextField(root, "Name", name);
    value_ = AddTextField(root, "Value", value);
    name_->Enable(name_editable);
    root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
    SetSizerAndFit(root);
    SetMinSize(wxSize(440, -1));
  }

  std::string Name() const {
    return name_->GetValue().ToStdString();
  }

  std::string Value() const {
    return value_->GetValue().ToStdString();
  }

 private:
  wxTextCtrl* AddTextField(wxBoxSizer* root,
                           const wxString& label,
                           const std::string& value) {
    root->Add(new wxStaticText(this, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    auto* ctrl = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(value));
    root->Add(ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    return ctrl;
  }

  wxTextCtrl* name_ = nullptr;
  wxTextCtrl* value_ = nullptr;
};

class MainFrame;

class FileDropTarget final : public wxFileDropTarget {
 public:
  explicit FileDropTarget(MainFrame* frame) : frame_(frame) {}
  bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames) override;

 private:
  MainFrame* frame_ = nullptr;
};

class MainFrame final : public wxFrame {
 public:
  MainFrame()
      : wxFrame(nullptr,
                wxID_ANY,
                "Matroska Extra Tags Editor",
                wxDefaultPosition,
                wxSize(920, 620)) {
    BuildUi();
    BindEvents();
    RefreshCommands();
  }

  void OpenPath(const std::filesystem::path& path) {
    if (!ConfirmDiscard()) {
      return;
    }
    LoadPath(path);
  }

 private:
  void BuildUi() {
    auto* panel = new wxPanel(this);
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* file_row = new wxBoxSizer(wxHORIZONTAL);
    path_ctrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                wxTE_READONLY);
    open_button_ = new wxButton(panel, wxID_OPEN, "Open");
    save_button_ = new wxButton(panel, wxID_SAVE, "Save");
    file_row->Add(path_ctrl_, 1, wxEXPAND | wxALL, 4);
    file_row->Add(open_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
    file_row->Add(save_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
    add_button_ = new wxButton(panel, ID_ADD_TAG, "Add");
    delete_button_ = new wxButton(panel, ID_DELETE_TAG, "Delete");
    copy_button_ = new wxButton(panel, ID_COPY_TAG, "Copy");
    paste_button_ = new wxButton(panel, ID_PASTE_TAG, "Paste");
    undo_button_ = new wxButton(panel, ID_UNDO, "Undo");
    redo_button_ = new wxButton(panel, ID_REDO, "Redo");
    const wxSize compact_button_size(68, -1);
    for (auto* button : {open_button_, save_button_, add_button_, delete_button_,
                         copy_button_, paste_button_, undo_button_, redo_button_}) {
      button->SetMinSize(compact_button_size);
    }
    file_row->Add(add_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
    file_row->Add(delete_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
    file_row->Add(copy_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
    file_row->Add(paste_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
    file_row->Add(undo_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
    file_row->Add(redo_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);

    tree_ = new wxTreeListCtrl(panel, ID_TREE, wxDefaultPosition, wxDefaultSize,
                               wxTL_MULTIPLE | wxTL_DEFAULT_STYLE);
    tree_->AppendColumn("Name", 320);
    tree_->AppendColumn("Value", 560);
    tree_->SetDropTarget(new FileDropTarget(this));

    root->Add(file_row, 0, wxEXPAND);
    root->Add(tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 3);
    panel->SetSizer(root);
    CreateStatusBar();

    wxAcceleratorEntry entries[8];
    entries[0].Set(wxACCEL_CTRL, static_cast<int>('C'), ID_COPY_TAG);
    entries[1].Set(wxACCEL_CTRL, static_cast<int>('X'), ID_CUT_TAG);
    entries[2].Set(wxACCEL_CTRL, static_cast<int>('V'), ID_PASTE_TAG);
    entries[3].Set(wxACCEL_CTRL, static_cast<int>('A'), ID_SELECT_ALL);
    entries[4].Set(wxACCEL_CTRL, static_cast<int>('S'), wxID_SAVE);
    entries[5].Set(wxACCEL_CTRL, static_cast<int>('Z'), ID_UNDO);
    entries[6].Set(wxACCEL_CTRL | wxACCEL_SHIFT, static_cast<int>('Z'), ID_REDO);
    entries[7].Set(wxACCEL_NORMAL, WXK_DELETE, ID_DELETE_TAG);
    SetAcceleratorTable(wxAcceleratorTable(8, entries));
  }

  void BindEvents() {
    Bind(wxEVT_BUTTON, &MainFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_BUTTON, &MainFrame::OnSave, this, wxID_SAVE);
    Bind(wxEVT_BUTTON, &MainFrame::OnAddTag, this, ID_ADD_TAG);
    Bind(wxEVT_BUTTON, &MainFrame::OnDelete, this, ID_DELETE_TAG);
    Bind(wxEVT_BUTTON, &MainFrame::OnCopy, this, ID_COPY_TAG);
    Bind(wxEVT_MENU, &MainFrame::OnCut, this, ID_CUT_TAG);
    Bind(wxEVT_BUTTON, &MainFrame::OnPaste, this, ID_PASTE_TAG);
    Bind(wxEVT_BUTTON, &MainFrame::OnUndo, this, ID_UNDO);
    Bind(wxEVT_BUTTON, &MainFrame::OnRedo, this, ID_REDO);
    Bind(wxEVT_MENU, &MainFrame::OnSave, this, wxID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnAddTag, this, ID_ADD_TAG);
    Bind(wxEVT_MENU, &MainFrame::OnDelete, this, ID_DELETE_TAG);
    Bind(wxEVT_MENU, &MainFrame::OnCopy, this, ID_COPY_TAG);
    Bind(wxEVT_MENU, &MainFrame::OnPaste, this, ID_PASTE_TAG);
    Bind(wxEVT_MENU, &MainFrame::OnUndo, this, ID_UNDO);
    Bind(wxEVT_MENU, &MainFrame::OnRedo, this, ID_REDO);
    Bind(wxEVT_MENU, &MainFrame::OnSelectAll, this, ID_SELECT_ALL);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    Bind(wxEVT_TREELIST_ITEM_ACTIVATED, &MainFrame::OnItemActivated, this, ID_TREE);
    Bind(wxEVT_TREELIST_SELECTION_CHANGED, &MainFrame::OnSelectionChanged, this, ID_TREE);
    Bind(wxEVT_TREELIST_ITEM_CONTEXT_MENU, &MainFrame::OnContextMenu, this, ID_TREE);
  }

  bool ConfirmDiscard() {
    if (!dirty_) {
      return true;
    }
    return wxMessageBox("Discard unsaved changes?", "Unsaved Changes",
                        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) == wxYES;
  }

  void LoadPath(const std::filesystem::path& path) {
    try {
      document_ = mte::load_tag_document(path);
      history_.clear();
      history_.push_back(document_);
      history_index_ = 0;
      saved_history_index_ = 0;
      dirty_ = false;
      path_ctrl_->SetValue(wxString::FromUTF8(path.string()));
      SetStatusText("Loaded " + wxString::FromUTF8(path.filename().string()));
      PopulateTree();
      RefreshCommands();
    } catch (const std::exception& error) {
      wxMessageBox(error.what(), "Open Error", wxOK | wxICON_ERROR, this);
    }
  }

  void OnOpen(wxCommandEvent&) {
    if (!ConfirmDiscard()) {
      return;
    }

    wxFileDialog dialog(this,
                        "Open Matroska file",
                        "",
                        "",
                        "Matroska files (*.mkv;*.mka;*.mk3d;*.mks)|*.mkv;*.mka;*.mk3d;*.mks",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_CANCEL) {
      return;
    }
    LoadPath(std::filesystem::path(dialog.GetPath().ToStdString()));
  }

  void OnSave(wxCommandEvent&) {
    try {
      mte::save_tag_document(document_);
      saved_history_index_ = history_index_;
      dirty_ = false;
      SetStatusText("Saved");
      RefreshCommands();
    } catch (const std::exception& error) {
      wxMessageBox(error.what(), "Save Error", wxOK | wxICON_ERROR, this);
    }
  }

  void OnAddTag(wxCommandEvent&) {
    if (document_.path.empty()) {
      return;
    }

    const auto context = CurrentContext();
    FieldEditDialog dialog(this, "Add Tag", "TITLE", "", true);
    if (dialog.ShowModal() != wxID_OK) {
      return;
    }

    const auto name = dialog.Name();
    const auto value = dialog.Value();
    AddExtraTag(context, name, value);
    SetStatusText("Added tag");
    PushHistory();
    PopulateTree();
  }

  void OnDelete(wxCommandEvent&) {
    DeleteSelected();
  }

  void OnCopy(wxCommandEvent&) {
    clipboard_.clear();
    for (const auto* data : SelectedData()) {
      if (data->kind == ItemKind::Group) {
        CopyGroup(*data);
      } else {
        CopyField(*data);
      }
    }
    RefreshCommands();
  }

  void OnCut(wxCommandEvent&) {
    OnCopyMenu();
    DeleteSelected();
  }

  void OnCopyMenu() {
    wxCommandEvent event;
    OnCopy(event);
  }

  void OnPaste(wxCommandEvent&) {
    if (clipboard_.empty() || document_.path.empty()) {
      return;
    }

    const auto context = CurrentContext();
    for (const auto& tag : clipboard_) {
      AddExtraTag(context, tag.name, tag.string_value);
    }
    PushHistory();
    PopulateTree();
  }

  void OnUndo(wxCommandEvent&) {
    if (history_index_ == 0) {
      return;
    }
    --history_index_;
    document_ = history_[history_index_];
    dirty_ = history_index_ != saved_history_index_;
    PopulateTree();
    RefreshCommands();
  }

  void OnRedo(wxCommandEvent&) {
    if (history_index_ + 1 >= history_.size()) {
      return;
    }
    ++history_index_;
    document_ = history_[history_index_];
    dirty_ = history_index_ != saved_history_index_;
    PopulateTree();
    RefreshCommands();
  }

  void OnSelectAll(wxCommandEvent&) {
    wxTreeListItem item = tree_->GetFirstItem();
    while (item.IsOk()) {
      if (auto* data = static_cast<ItemData*>(tree_->GetItemData(item));
          data && data->kind == ItemKind::Field) {
        tree_->Select(item);
      }
      item = tree_->GetNextItem(item);
    }
    RefreshCommands();
  }

  void OnClose(wxCloseEvent& event) {
    if (ConfirmDiscard()) {
      Destroy();
      return;
    }
    event.Veto();
  }

  void OnSelectionChanged(wxTreeListEvent&) {
    RefreshCommands();
  }

  void OnContextMenu(wxTreeListEvent& event) {
    const auto item = event.GetItem();
    if (item.IsOk()) {
      tree_->Select(item);
    }

    wxMenu menu;
    auto* data = SelectedData().empty() ? nullptr : SelectedData().front();
    const auto is_group = data && data->kind == ItemKind::Group;
    if (is_group) {
      menu.Append(ID_ADD_TAG, "Add Tag");
      menu.AppendSeparator();
      menu.Append(ID_COPY_TAG, "Copy All Tags");
      menu.Append(ID_CUT_TAG, "Cut All Tags");
      menu.Append(ID_PASTE_TAG, "Paste Tags");
      menu.AppendSeparator();
      menu.Append(ID_DELETE_TAG, "Delete Visible Fields");
    } else {
      menu.Append(ID_UNDO, "Undo");
      menu.Append(ID_REDO, "Redo");
      menu.AppendSeparator();
      menu.Append(ID_CUT_TAG, "Cut");
      menu.Append(ID_COPY_TAG, "Copy");
      menu.Append(ID_PASTE_TAG, "Paste");
      menu.AppendSeparator();
      menu.Append(ID_DELETE_TAG, "Delete");
      menu.AppendSeparator();
      menu.Append(ID_SELECT_ALL, "Select All");
    }
    menu.Enable(ID_UNDO, history_index_ > 0);
    menu.Enable(ID_REDO, history_index_ + 1 < history_.size());
    menu.Enable(ID_PASTE_TAG, !clipboard_.empty());
    PopupMenu(&menu);
  }

  void OnItemActivated(wxTreeListEvent& event) {
    auto* data = static_cast<ItemData*>(tree_->GetItemData(event.GetItem()));
    if (!data || data->kind != ItemKind::Field) {
      return;
    }

    FieldEditDialog dialog(this, "Edit Tag", FieldName(*data), FieldValue(*data), true);
    if (dialog.ShowModal() != wxID_OK) {
      return;
    }
    ApplyField(*data, dialog.Name(), dialog.Value());
    PushHistory();
    PopulateTree();
  }

  void PopulateTree() {
    populating_tree_ = true;
    tree_->DeleteAllItems();
    auto root = tree_->GetRootItem();
    const auto fields = mte::editable_fields(document_);

    AppendGroup(root, {mte::EditableTargetKind::General, 0}, fields);

    for (const auto& track : document_.tracks) {
      AppendGroup(root, {mte::EditableTargetKind::Track, track.uid}, fields);
    }

    AppendGroup(root, {mte::EditableTargetKind::Other, 0}, fields);
    populating_tree_ = false;
  }

  void AppendGroup(wxTreeListItem root,
                   mte::EditableTarget target,
                   const std::vector<mte::EditableField>& fields) {
    const auto has_fields =
        std::any_of(fields.begin(), fields.end(), [&](const mte::EditableField& field) {
          return same_target(field.target, target);
        });
    if (!has_fields) {
      return;
    }

    auto* group_data = new ItemData();
    group_data->kind = ItemKind::Group;
    group_data->target = target;

    const auto group =
        tree_->AppendItem(root, wxString::FromUTF8(group_label(target, document_.tracks)),
                          -1, -1, group_data);
    tree_->SetItemText(group, 1, "");

    for (const auto& field : fields) {
      if (!same_target(field.target, target)) {
        continue;
      }
      auto* data = new ItemData();
      data->kind = ItemKind::Field;
      data->target = field.target;
      data->tag_index = field.tag_index;
      data->simple_path = field.simple_path;

      const auto item =
          tree_->AppendItem(group, wxString::FromUTF8(field.name), -1, -1, data);
      tree_->SetItemText(item, 1, wxString::FromUTF8(field.value));
    }
    tree_->Expand(group);
  }

  std::vector<ItemData*> SelectedData() const {
    wxTreeListItems selected;
    tree_->GetSelections(selected);
    std::vector<ItemData*> data;
    for (const auto& item : selected) {
      if (item.IsOk()) {
        if (auto* item_data = static_cast<ItemData*>(tree_->GetItemData(item))) {
          data.push_back(item_data);
        }
      }
    }
    return data;
  }

  mte::EditableTarget CurrentContext() const {
    const auto selected = SelectedData();
    if (!selected.empty()) {
      return selected.front()->target;
    }
    return {mte::EditableTargetKind::General, 0};
  }

  void PushHistory() {
    if (history_index_ + 1 < history_.size()) {
      history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(history_index_ + 1),
                     history_.end());
    }
    history_.push_back(document_);
    history_index_ = history_.size() - 1;
    dirty_ = history_index_ != saved_history_index_;
    RefreshCommands();
  }

  void RefreshCommands() {
    const auto has_document = !document_.path.empty();
    const auto has_selection = !SelectedData().empty();
    save_button_->Enable(has_document && dirty_);
    add_button_->Enable(has_document);
    delete_button_->Enable(has_document && has_selection);
    copy_button_->Enable(has_document && has_selection);
    paste_button_->Enable(has_document && !clipboard_.empty());
    undo_button_->Enable(history_index_ > 0);
    redo_button_->Enable(history_index_ + 1 < history_.size());
  }

  std::string FieldName(const ItemData& data) const {
    if (data.tag_index < document_.tags.size()) {
      if (auto* tag = find_simple(const_cast<mte::TagEntry&>(document_.tags[data.tag_index]),
                                  data.simple_path)) {
        return tag->name;
      }
    }
    return {};
  }

  std::string FieldValue(const ItemData& data) const {
    if (data.tag_index < document_.tags.size()) {
      if (auto* tag = find_simple(const_cast<mte::TagEntry&>(document_.tags[data.tag_index]),
                                  data.simple_path)) {
        return tag->string_value;
      }
    }
    return {};
  }

  void ApplyField(const ItemData& data, const std::string& name, const std::string& value) {
    if (data.tag_index < document_.tags.size()) {
      if (auto* tag = find_simple(document_.tags[data.tag_index], data.simple_path)) {
        tag->name = name;
        tag->string_value = value;
      }
    }
  }

  void DeleteSelected() {
    const auto selected = SelectedData();
    if (selected.empty()) {
      return;
    }

    for (const auto* data : selected) {
      if (data->kind == ItemKind::Group) {
        DeleteGroup(*data);
      }
    }

    std::vector<ItemData> extra_items;
    for (const auto* data : selected) {
      if (data->kind == ItemKind::Field) {
        extra_items.push_back(*data);
      }
    }
    std::sort(extra_items.begin(), extra_items.end(), [](const ItemData& left,
                                                         const ItemData& right) {
      if (left.tag_index != right.tag_index) {
        return left.tag_index > right.tag_index;
      }
      return left.simple_path > right.simple_path;
    });
    for (const auto& data : extra_items) {
      if (data.tag_index < document_.tags.size()) {
        erase_simple(document_.tags[data.tag_index], data.simple_path);
      }
    }
    RemoveEmptyTagEntries();
    PushHistory();
    PopulateTree();
  }

  void DeleteGroup(const ItemData& group) {
    for (auto& entry : document_.tags) {
      const auto target = TargetForTag(entry.targets);
      if (same_target(target, group.target)) {
        erase_visible_simple_tags(entry.simple_tags);
      }
    }

  }

  void CopyGroup(const ItemData& group) {
    for (const auto& entry : document_.tags) {
      const auto target = TargetForTag(entry.targets);
      if (same_target(target, group.target)) {
        for (const auto& tag : entry.simple_tags) {
          if (tag.value_type == mte::TagValueType::String &&
              !mte::is_hidden_extra_tag_name(tag.name)) {
            clipboard_.push_back(tag);
          }
        }
      }
    }
  }

  void CopyField(const ItemData& data) {
    mte::SimpleTag tag;
    tag.name = FieldName(data);
    tag.string_value = FieldValue(data);
    if (!tag.name.empty()) {
      clipboard_.push_back(tag);
    }
  }

  void AddExtraTag(mte::EditableTarget target,
                   const std::string& name,
                   const std::string& value) {
    mte::SimpleTag tag;
    tag.name = name;
    tag.string_value = value;

    for (auto& entry : document_.tags) {
      const auto entry_target = TargetForTag(entry.targets);
      if (entry_target.kind == target.kind && entry_target.track_uid == target.track_uid) {
        entry.simple_tags.push_back(tag);
        return;
      }
    }

    mte::TagEntry entry;
    if (target.kind == mte::EditableTargetKind::Track && target.track_uid != 0) {
      entry.targets.track_uids.push_back(target.track_uid);
    }
    entry.simple_tags.push_back(tag);
    document_.tags.push_back(std::move(entry));
  }

  static mte::EditableTarget TargetForTag(const mte::TagTargets& targets) {
    if (!targets.track_uids.empty()) {
      return {mte::EditableTargetKind::Track, targets.track_uids.front()};
    }
    if (!targets.edition_uids.empty() || !targets.chapter_uids.empty() ||
        !targets.attachment_uids.empty()) {
      return {mte::EditableTargetKind::Other, 0};
    }
    return {mte::EditableTargetKind::General, 0};
  }

  void RemoveEmptyTagEntries() {
    document_.tags.erase(
        std::remove_if(document_.tags.begin(), document_.tags.end(),
                       [](const mte::TagEntry& entry) { return entry.simple_tags.empty(); }),
        document_.tags.end());
  }

  wxTextCtrl* path_ctrl_ = nullptr;
  wxButton* open_button_ = nullptr;
  wxButton* save_button_ = nullptr;
  wxButton* add_button_ = nullptr;
  wxButton* delete_button_ = nullptr;
  wxButton* copy_button_ = nullptr;
  wxButton* paste_button_ = nullptr;
  wxButton* undo_button_ = nullptr;
  wxButton* redo_button_ = nullptr;
  wxTreeListCtrl* tree_ = nullptr;

  mte::TagDocument document_;
  std::vector<mte::TagDocument> history_;
  std::vector<mte::SimpleTag> clipboard_;
  std::size_t history_index_ = 0;
  std::size_t saved_history_index_ = 0;
  bool dirty_ = false;
  bool populating_tree_ = false;
};

bool FileDropTarget::OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames) {
  if (!frame_ || filenames.empty()) {
    return false;
  }
  frame_->OpenPath(std::filesystem::path(filenames.front().ToStdString()));
  return true;
}

class App final : public wxApp {
 public:
  bool OnInit() override {
    auto* frame = new MainFrame();
    frame->Show(true);
    return true;
  }
};

}  // namespace

wxIMPLEMENT_APP(App);
