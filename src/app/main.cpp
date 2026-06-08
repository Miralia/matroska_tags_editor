#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treelist.h>
#include <wx/wx.h>

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
  ID_PASTE_TAG,
  ID_UNDO,
  ID_REDO
};

enum class ItemKind {
  Group,
  Simple
};

class ItemData final : public wxClientData {
 public:
  ItemKind kind = ItemKind::Group;
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

std::string tag_value_text(const mte::SimpleTag& tag) {
  if (tag.value_type == mte::TagValueType::Binary) {
    return "<binary " + std::to_string(tag.binary_value.size()) + " bytes>";
  }
  return tag.string_value;
}

mte::SimpleTag* find_simple(mte::TagEntry& entry, const std::vector<std::size_t>& path) {
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

class TagEditDialog final : public wxDialog {
 public:
  TagEditDialog(wxWindow* parent, const mte::SimpleTag& tag)
      : wxDialog(parent, wxID_ANY, "Edit Tag", wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    name_ = AddTextField(root, "Name", tag.name);
    value_ = AddTextField(root, "Value", tag_value_text(tag));
    language_ = AddTextField(root, "Language", tag.language);
    language_bcp47_ = AddTextField(root, "BCP 47", tag.language_bcp47);
    default_ = new wxCheckBox(this, wxID_ANY, "Default");
    default_->SetValue(tag.is_default);
    root->Add(default_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    if (tag.value_type == mte::TagValueType::Binary) {
      value_->Disable();
    }

    root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
    SetSizerAndFit(root);
    SetMinSize(wxSize(420, -1));
  }

  void ApplyTo(mte::SimpleTag& tag) const {
    tag.name = name_->GetValue().ToStdString();
    if (tag.value_type == mte::TagValueType::String) {
      tag.string_value = value_->GetValue().ToStdString();
    }
    tag.language = language_->GetValue().ToStdString();
    tag.language_bcp47 = language_bcp47_->GetValue().ToStdString();
    tag.is_default = default_->GetValue();
  }

 private:
  wxTextCtrl* AddTextField(wxBoxSizer* root, const wxString& label, const std::string& value) {
    root->Add(new wxStaticText(this, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    auto* ctrl = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(value));
    root->Add(ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    return ctrl;
  }

  wxTextCtrl* name_ = nullptr;
  wxTextCtrl* value_ = nullptr;
  wxTextCtrl* language_ = nullptr;
  wxTextCtrl* language_bcp47_ = nullptr;
  wxCheckBox* default_ = nullptr;
};

class MainFrame final : public wxFrame {
 public:
  MainFrame()
      : wxFrame(nullptr,
                wxID_ANY,
                "Matroska Tags Editor",
                wxDefaultPosition,
                wxSize(1050, 720)) {
    BuildUi();
    BindEvents();
    RefreshCommands();
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
    file_row->Add(path_ctrl_, 1, wxEXPAND | wxALL, 6);
    file_row->Add(open_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 6);
    file_row->Add(save_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 6);

    auto* action_row = new wxBoxSizer(wxHORIZONTAL);
    add_button_ = new wxButton(panel, ID_ADD_TAG, "Add");
    delete_button_ = new wxButton(panel, ID_DELETE_TAG, "Delete");
    copy_button_ = new wxButton(panel, ID_COPY_TAG, "Copy");
    paste_button_ = new wxButton(panel, ID_PASTE_TAG, "Paste");
    undo_button_ = new wxButton(panel, ID_UNDO, "Undo");
    redo_button_ = new wxButton(panel, ID_REDO, "Redo");
    action_row->Add(add_button_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
    action_row->Add(delete_button_, 0, wxRIGHT | wxBOTTOM, 6);
    action_row->Add(copy_button_, 0, wxRIGHT | wxBOTTOM, 6);
    action_row->Add(paste_button_, 0, wxRIGHT | wxBOTTOM, 6);
    action_row->Add(undo_button_, 0, wxRIGHT | wxBOTTOM, 6);
    action_row->Add(redo_button_, 0, wxRIGHT | wxBOTTOM, 6);

    tree_ = new wxTreeListCtrl(panel, ID_TREE, wxDefaultPosition, wxDefaultSize,
                               wxTL_SINGLE);
    tree_->AppendColumn("Name", 250);
    tree_->AppendColumn("Value", 360);
    tree_->AppendColumn("Language", 100);
    tree_->AppendColumn("Default", 80);
    tree_->AppendColumn("Target", 220);
    tree_->AppendColumn("Type", 90);

    root->Add(file_row, 0, wxEXPAND);
    root->Add(action_row, 0, wxEXPAND);
    root->Add(tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    panel->SetSizer(root);
  }

  void BindEvents() {
    Bind(wxEVT_BUTTON, &MainFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_BUTTON, &MainFrame::OnSave, this, wxID_SAVE);
    Bind(wxEVT_BUTTON, &MainFrame::OnAddTag, this, ID_ADD_TAG);
    Bind(wxEVT_BUTTON, &MainFrame::OnDelete, this, ID_DELETE_TAG);
    Bind(wxEVT_BUTTON, &MainFrame::OnCopy, this, ID_COPY_TAG);
    Bind(wxEVT_BUTTON, &MainFrame::OnPaste, this, ID_PASTE_TAG);
    Bind(wxEVT_BUTTON, &MainFrame::OnUndo, this, ID_UNDO);
    Bind(wxEVT_BUTTON, &MainFrame::OnRedo, this, ID_REDO);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    Bind(wxEVT_TREELIST_ITEM_ACTIVATED, &MainFrame::OnItemActivated, this, ID_TREE);
    Bind(wxEVT_TREELIST_SELECTION_CHANGED, &MainFrame::OnSelectionChanged, this, ID_TREE);
  }

  bool ConfirmDiscard() {
    if (!dirty_) {
      return true;
    }
    return wxMessageBox("Discard unsaved changes?", "Unsaved Changes",
                        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) == wxYES;
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

    try {
      document_ = mte::load_tag_document(std::filesystem::path(dialog.GetPath().ToStdString()));
      history_.clear();
      history_.push_back(document_);
      history_index_ = 0;
      saved_history_index_ = 0;
      dirty_ = false;
      path_ctrl_->SetValue(dialog.GetPath());
      PopulateTree();
      RefreshCommands();
    } catch (const std::exception& error) {
      wxMessageBox(error.what(), "Open Error", wxOK | wxICON_ERROR, this);
    }
  }

  void OnSave(wxCommandEvent&) {
    try {
      mte::save_tag_document(document_);
      saved_history_index_ = history_index_;
      dirty_ = false;
      RefreshCommands();
    } catch (const std::exception& error) {
      wxMessageBox(error.what(), "Save Error", wxOK | wxICON_ERROR, this);
    }
  }

  void OnAddTag(wxCommandEvent&) {
    if (document_.path.empty()) {
      return;
    }

    mte::SimpleTag tag;
    tag.name = "TITLE";
    tag.string_value = "";

    auto* selected = SelectedData();
    if (selected && selected->tag_index < document_.tags.size()) {
      if (selected->kind == ItemKind::Group) {
        document_.tags[selected->tag_index].simple_tags.push_back(tag);
      } else if (auto* parent = find_simple(document_.tags[selected->tag_index],
                                            selected->simple_path)) {
        parent->children.push_back(tag);
      }
    } else {
      mte::TagEntry entry;
      entry.simple_tags.push_back(tag);
      document_.tags.push_back(entry);
    }

    PushHistory();
    PopulateTree();
  }

  void OnDelete(wxCommandEvent&) {
    auto* data = SelectedData();
    if (!data) {
      return;
    }

    if (data->tag_index >= document_.tags.size()) {
      return;
    }

    if (data->kind == ItemKind::Group) {
      document_.tags.erase(document_.tags.begin() + static_cast<std::ptrdiff_t>(data->tag_index));
    } else {
      erase_simple(document_.tags[data->tag_index], data->simple_path);
      if (document_.tags[data->tag_index].simple_tags.empty()) {
        document_.tags.erase(document_.tags.begin() +
                             static_cast<std::ptrdiff_t>(data->tag_index));
      }
    }

    PushHistory();
    PopulateTree();
  }

  void OnCopy(wxCommandEvent&) {
    clipboard_.clear();
    auto* data = SelectedData();
    if (!data || data->tag_index >= document_.tags.size()) {
      return;
    }

    if (data->kind == ItemKind::Group) {
      clipboard_ = document_.tags[data->tag_index].simple_tags;
      return;
    }

    if (auto* tag = find_simple(document_.tags[data->tag_index], data->simple_path)) {
      clipboard_.push_back(*tag);
    }
    RefreshCommands();
  }

  void OnPaste(wxCommandEvent&) {
    if (clipboard_.empty() || document_.path.empty()) {
      return;
    }

    auto* data = SelectedData();
    if (data && data->tag_index < document_.tags.size()) {
      if (data->kind == ItemKind::Group) {
        auto& tags = document_.tags[data->tag_index].simple_tags;
        tags.insert(tags.end(), clipboard_.begin(), clipboard_.end());
      } else if (auto* tag = find_simple(document_.tags[data->tag_index], data->simple_path)) {
        tag->children.insert(tag->children.end(), clipboard_.begin(), clipboard_.end());
      }
    } else {
      mte::TagEntry entry;
      entry.simple_tags = clipboard_;
      document_.tags.push_back(entry);
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

  void OnItemActivated(wxTreeListEvent& event) {
    auto* data = static_cast<ItemData*>(tree_->GetItemData(event.GetItem()));
    if (!data || data->kind != ItemKind::Simple || data->tag_index >= document_.tags.size()) {
      return;
    }

    auto* tag = find_simple(document_.tags[data->tag_index], data->simple_path);
    if (!tag) {
      return;
    }

    TagEditDialog dialog(this, *tag);
    if (dialog.ShowModal() != wxID_OK) {
      return;
    }
    dialog.ApplyTo(*tag);
    PushHistory();
    PopulateTree();
  }

  void PopulateTree() {
    tree_->DeleteAllItems();
    auto root = tree_->GetRootItem();

    for (std::size_t i = 0; i < document_.tags.size(); ++i) {
      const auto target = mte::describe_targets(document_.tags[i].targets, document_.tracks);
      auto* data = new ItemData();
      data->kind = ItemKind::Group;
      data->tag_index = i;
      const auto group = tree_->AppendItem(root, wxString::FromUTF8(target), -1, -1, data);
      tree_->SetItemText(group, 4, wxString::FromUTF8(target));
      for (std::size_t j = 0; j < document_.tags[i].simple_tags.size(); ++j) {
        AppendSimple(group, i, {j}, document_.tags[i].simple_tags[j], target);
      }
      tree_->Expand(group);
    }
  }

  void AppendSimple(wxTreeListItem parent,
                    std::size_t tag_index,
                    std::vector<std::size_t> path,
                    const mte::SimpleTag& tag,
                    const std::string& target) {
    auto* data = new ItemData();
    data->kind = ItemKind::Simple;
    data->tag_index = tag_index;
    data->simple_path = path;

    const auto item = tree_->AppendItem(parent, wxString::FromUTF8(tag.name), -1, -1, data);
    tree_->SetItemText(item, 1, wxString::FromUTF8(tag_value_text(tag)));
    tree_->SetItemText(item, 2, wxString::FromUTF8(tag.language_bcp47.empty()
                                                       ? tag.language
                                                       : tag.language_bcp47));
    tree_->SetItemText(item, 3, tag.is_default ? "Yes" : "No");
    tree_->SetItemText(item, 4, wxString::FromUTF8(target));
    tree_->SetItemText(item, 5, tag.value_type == mte::TagValueType::Binary ? "Binary" : "String");

    for (std::size_t i = 0; i < tag.children.size(); ++i) {
      auto child_path = path;
      child_path.push_back(i);
      AppendSimple(item, tag_index, std::move(child_path), tag.children[i], target);
    }
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

  ItemData* SelectedData() const {
    const auto item = tree_->GetSelection();
    if (!item.IsOk()) {
      return nullptr;
    }
    return static_cast<ItemData*>(tree_->GetItemData(item));
  }

  void RefreshCommands() {
    const auto has_document = !document_.path.empty();
    const auto has_selection = SelectedData() != nullptr;
    save_button_->Enable(has_document && dirty_);
    add_button_->Enable(has_document);
    delete_button_->Enable(has_document && has_selection);
    copy_button_->Enable(has_document && has_selection);
    paste_button_->Enable(has_document && !clipboard_.empty());
    undo_button_->Enable(history_index_ > 0);
    redo_button_->Enable(history_index_ + 1 < history_.size());
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
};

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
