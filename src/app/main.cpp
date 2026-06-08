#include <wx/dataview.h>
#include <wx/dataobj.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/matroska_reader.h"
#include "core/matroska_writer.h"

namespace {

constexpr int kDefaultWindowWidth = 480;
constexpr int kDefaultWindowHeight = 700;
constexpr int kMinWindowWidth = 400;
constexpr int kMinWindowHeight = 540;
constexpr int kNameColumnMinWidth = 150;
constexpr int kValueColumnMinWidth = 210;

enum : int {
  ID_TREE = wxID_HIGHEST + 1,
  ID_ADD_TAG,
  ID_DELETE_TAG,
  ID_COPY_TAG,
  ID_CUT_TAG,
  ID_PASTE_TAG,
  ID_SELECT_ALL,
  ID_UNDO,
  ID_REDO,
  ID_INLINE_EDITOR
};

enum class ItemKind {
  Group,
  Field
};

struct ItemData {
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

bool is_file_data_format(const wxDataFormat& format) {
  return format == wxDF_FILENAME;
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
  std::string prefix = "Global";
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

bool has_visible_simple_tags(const std::vector<mte::SimpleTag>& tags) {
  return std::any_of(tags.begin(), tags.end(), [](const mte::SimpleTag& tag) {
    if (tag.value_type == mte::TagValueType::String &&
        !mte::is_hidden_extra_tag_name(tag.name)) {
      return true;
    }
    return has_visible_simple_tags(tag.children);
  });
}

struct InlineEditRequest {
  std::size_t tag_index = 0;
  std::vector<std::size_t> simple_path;
  unsigned column = 0;
};

struct ViewNode {
  ItemData data;
  std::string label;
  std::string name;
  std::string value;
  ViewNode* parent = nullptr;
  std::vector<std::unique_ptr<ViewNode>> children;
};

class TagTreeModel final : public wxDataViewModel {
 public:
  unsigned int GetColumnCount() const override {
    return 2;
  }

  wxString GetColumnType(unsigned int) const override {
    return "string";
  }

  void GetValue(wxVariant& variant,
                const wxDataViewItem& item,
                unsigned int column) const override {
    const auto* node = NodeFromItem(item);
    if (!node) {
      variant = "";
      return;
    }

    if (node->data.kind == ItemKind::Group) {
      variant = column == 0 ? wxString::FromUTF8(node->label) : wxString();
      return;
    }

    variant = wxString::FromUTF8(
        mte::editable_display_text(column == 0 ? node->name : node->value));
  }

  bool SetValue(const wxVariant&, const wxDataViewItem&, unsigned int) override {
    return false;
  }

  bool GetAttr(const wxDataViewItem& item,
               unsigned int column,
               wxDataViewItemAttr& attr) const override {
    const auto* node = NodeFromItem(item);
    if (!node) {
      return false;
    }

    if (node->data.kind == ItemKind::Group && column == 0) {
      attr.SetBold(true);
      return true;
    }

    if (node->data.kind == ItemKind::Field &&
        ((column == 0 && node->name.empty()) ||
         (column == 1 && node->value.empty()))) {
      attr.SetColour(wxColour(128, 128, 128));
      attr.SetItalic(true);
      return true;
    }

    return false;
  }

  wxDataViewItem GetParent(const wxDataViewItem& item) const override {
    const auto* node = NodeFromItem(item);
    if (!node || !node->parent) {
      return wxDataViewItem();
    }
    return ItemFromNode(node->parent);
  }

  bool IsContainer(const wxDataViewItem& item) const override {
    if (!item.IsOk()) {
      return true;
    }
    const auto* node = NodeFromItem(item);
    return node && node->data.kind == ItemKind::Group;
  }

  unsigned int GetChildren(const wxDataViewItem& item,
                           wxDataViewItemArray& children) const override {
    if (!item.IsOk()) {
      for (const auto& root : roots_) {
        children.Add(ItemFromNode(root.get()));
      }
      return static_cast<unsigned int>(roots_.size());
    }

    const auto* node = NodeFromItem(item);
    if (!node) {
      return 0;
    }

    for (const auto& child : node->children) {
      children.Add(ItemFromNode(child.get()));
    }
    return static_cast<unsigned int>(node->children.size());
  }

  void Rebuild(const mte::TagDocument& document) {
    BeforeReset();
    roots_.clear();

    const auto fields = mte::editable_fields(document);
    for (const auto& target : mte::editable_targets(document)) {
      auto group = std::make_unique<ViewNode>();
      group->data.kind = ItemKind::Group;
      group->data.target = target;
      group->label = group_label(target, document.tracks);

      for (const auto& field : fields) {
        if (!same_target(field.target, target)) {
          continue;
        }

        auto child = std::make_unique<ViewNode>();
        child->data.kind = ItemKind::Field;
        child->data.target = field.target;
        child->data.tag_index = field.tag_index;
        child->data.simple_path = field.simple_path;
        child->name = field.name;
        child->value = field.value;
        child->parent = group.get();
        group->children.push_back(std::move(child));
      }

      roots_.push_back(std::move(group));
    }

    AfterReset();
  }

  std::vector<wxDataViewItem> RootItems() const {
    std::vector<wxDataViewItem> items;
    for (const auto& root : roots_) {
      items.push_back(ItemFromNode(root.get()));
    }
    return items;
  }

  void CollectFieldItems(wxDataViewItemArray& items) const {
    for (const auto& root : roots_) {
      CollectFieldItems(*root, items);
    }
  }

  wxDataViewItem FindField(std::size_t tag_index,
                           const std::vector<std::size_t>& simple_path) const {
    for (const auto& root : roots_) {
      for (const auto& child : root->children) {
        if (child->data.kind == ItemKind::Field &&
            child->data.tag_index == tag_index &&
            child->data.simple_path == simple_path) {
          return ItemFromNode(child.get());
        }
      }
    }
    return wxDataViewItem();
  }

  ItemData* DataForItem(const wxDataViewItem& item) {
    auto* node = NodeFromItem(item);
    return node ? &node->data : nullptr;
  }

  const ItemData* DataForItem(const wxDataViewItem& item) const {
    const auto* node = NodeFromItem(item);
    return node ? &node->data : nullptr;
  }

 private:
  static wxDataViewItem ItemFromNode(const ViewNode* node) {
    return wxDataViewItem(const_cast<ViewNode*>(node));
  }

  static ViewNode* NodeFromItem(const wxDataViewItem& item) {
    return static_cast<ViewNode*>(item.GetID());
  }

  static void CollectFieldItems(const ViewNode& node, wxDataViewItemArray& items) {
    if (node.data.kind == ItemKind::Field) {
      items.Add(ItemFromNode(&node));
    }
    for (const auto& child : node.children) {
      CollectFieldItems(*child, items);
    }
  }

  std::vector<std::unique_ptr<ViewNode>> roots_;
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
                wxSize(kDefaultWindowWidth, kDefaultWindowHeight)) {
    BuildUi();
    BindEvents();
    RefreshCommands();
  }

  bool OpenPath(const std::filesystem::path& path) {
    if (path.empty()) {
      return false;
    }
    if (!ConfirmDiscard()) {
      return false;
    }
    return LoadPath(path);
  }

  bool OpenDroppedFiles(const wxArrayString& filenames) {
    std::vector<std::filesystem::path> paths;
    paths.reserve(filenames.size());
    for (const auto& filename : filenames) {
      paths.emplace_back(filename.ToStdString());
    }

    const auto path = mte::first_dropped_file_path(paths);
    return path.has_value() && OpenPath(*path);
  }

 private:
  void BuildUi() {
    auto* panel = new wxPanel(this);
    auto* root = new wxBoxSizer(wxVERTICAL);
    InstallFileDropTarget(this);
    InstallFileDropTarget(panel);

    auto* file_row = new wxBoxSizer(wxHORIZONTAL);
    path_ctrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                wxTE_READONLY);
    open_button_ = new wxButton(panel, wxID_OPEN, "Open");
    save_button_ = new wxButton(panel, wxID_SAVE, "Save");
    InstallFileDropTarget(path_ctrl_);
    file_row->Add(path_ctrl_, 1, wxEXPAND | wxALL, 4);
    file_row->Add(open_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
    file_row->Add(save_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
    const wxSize compact_button_size(62, -1);
    for (auto* button : {open_button_, save_button_}) {
      button->SetMinSize(compact_button_size);
    }
    auto* action_row = new wxBoxSizer(wxHORIZONTAL);
    add_button_ = new wxButton(panel, ID_ADD_TAG, "Add");
    delete_button_ = new wxButton(panel, ID_DELETE_TAG, "Delete");
    copy_button_ = new wxButton(panel, ID_COPY_TAG, "Copy");
    paste_button_ = new wxButton(panel, ID_PASTE_TAG, "Paste");
    undo_button_ = new wxButton(panel, ID_UNDO, "Undo");
    redo_button_ = new wxButton(panel, ID_REDO, "Redo");
    for (auto* button : {add_button_, delete_button_, copy_button_, paste_button_,
                         undo_button_, redo_button_}) {
      button->SetMinSize(compact_button_size);
      action_row->Add(button, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 4);
    }

    tree_ = new wxDataViewCtrl(panel, ID_TREE, wxDefaultPosition, wxDefaultSize,
                               wxDV_MULTIPLE | wxDV_ROW_LINES);
    tree_->AppendTextColumn("Name", 0, wxDATAVIEW_CELL_INERT,
                            kNameColumnMinWidth, wxALIGN_LEFT,
                            wxDATAVIEW_COL_RESIZABLE);
    tree_->AppendTextColumn("Value", 1, wxDATAVIEW_CELL_INERT,
                            kValueColumnMinWidth, wxALIGN_LEFT,
                            wxDATAVIEW_COL_RESIZABLE);
    model_ = new TagTreeModel();
    tree_->AssociateModel(model_);
    model_->DecRef();
    InstallTreeFileDropTarget();

    root->Add(file_row, 0, wxEXPAND);
    root->Add(action_row, 0, wxLEFT | wxRIGHT, 4);
    root->Add(tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 3);
    panel->SetSizer(root);
    SetMinSize(wxSize(kMinWindowWidth, kMinWindowHeight));
    CreateStatusBar();
    CallAfter([this]() { ResizeColumns(); });

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
    Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &MainFrame::OnItemActivated, this, ID_TREE);
    Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &MainFrame::OnSelectionChanged, this, ID_TREE);
    Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &MainFrame::OnContextMenu, this, ID_TREE);
    Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &MainFrame::OnTreeFileDropPossible, this,
         ID_TREE);
    Bind(wxEVT_DATAVIEW_ITEM_DROP, &MainFrame::OnTreeFileDrop, this, ID_TREE);
    Bind(wxEVT_SIZE, &MainFrame::OnSize, this);
    tree_->Bind(wxEVT_LEFT_DCLICK, &MainFrame::OnTreeDoubleClick, this);
  }

  void InstallFileDropTarget(wxWindow* window) {
    if (window) {
      window->SetDropTarget(new FileDropTarget(this));
    }
  }

  void InstallTreeFileDropTarget() {
    if (tree_ && !tree_->EnableDropTarget(wxDataFormat(wxDF_FILENAME))) {
      InstallFileDropTarget(tree_);
    }
  }

  bool ConfirmDiscard() {
    if (!dirty_) {
      return true;
    }
    return wxMessageBox("Discard unsaved changes?", "Unsaved Changes",
                        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) == wxYES;
  }

  bool LoadPath(const std::filesystem::path& path) {
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
      return true;
    } catch (const std::exception& error) {
      wxMessageBox(error.what(), "Open Error", wxOK | wxICON_ERROR, this);
      return false;
    }
  }

  void OnOpen(wxCommandEvent&) {
    wxFileDialog dialog(this,
                        "Open Matroska file",
                        "",
                        "",
                        "Matroska files (*.mkv;*.mka;*.mk3d;*.mks)|*.mkv;*.mka;*.mk3d;*.mks",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_CANCEL) {
      return;
    }
    OpenPath(std::filesystem::path(dialog.GetPath().ToStdString()));
  }

  void OnTreeFileDropPossible(wxDataViewEvent& event) {
    event.SetDropEffect(is_file_data_format(event.GetDataFormat()) ? wxDragCopy
                                                                   : wxDragNone);
  }

  void OnTreeFileDrop(wxDataViewEvent& event) {
    if (!is_file_data_format(event.GetDataFormat()) || !event.GetDataBuffer() ||
        event.GetDataSize() == 0) {
      event.SetDropEffect(wxDragNone);
      return;
    }

    wxFileDataObject file_data;
    if (!file_data.SetData(event.GetDataFormat(), event.GetDataSize(),
                           event.GetDataBuffer())) {
      event.SetDropEffect(wxDragNone);
      return;
    }

    event.SetDropEffect(OpenDroppedFiles(file_data.GetFilenames()) ? wxDragCopy
                                                                   : wxDragNone);
  }

  void OnSave(wxCommandEvent&) {
    if (!ValidateBeforeSave()) {
      return;
    }

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

    const auto field = mte::add_extra_tag(document_, CurrentContext(), "", "");
    pending_inline_edit_ = InlineEditRequest{field.tag_index, field.simple_path, 0};
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
      mte::add_extra_tag(document_, context, tag.name, tag.string_value);
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
    wxDataViewItemArray items;
    model_->CollectFieldItems(items);
    tree_->SetSelections(items);
    RefreshCommands();
  }

  void OnClose(wxCloseEvent& event) {
    if (ConfirmDiscard()) {
      Destroy();
      return;
    }
    event.Veto();
  }

  void OnSelectionChanged(wxDataViewEvent&) {
    RefreshCommands();
  }

  void OnSize(wxSizeEvent& event) {
    event.Skip();
    if (inline_editor_) {
      FinishInlineEdit(true);
    }
    ResizeColumns();
  }

  void OnTreeDoubleClick(wxMouseEvent& event) {
    wxDataViewItem data_view_item;
    wxDataViewColumn* data_view_column = nullptr;
    tree_->HitTest(event.GetPosition(), data_view_item, data_view_column);
    if (!data_view_item.IsOk() || !data_view_column) {
      event.Skip();
      return;
    }

    const auto column = data_view_column->GetModelColumn();
    if (column > 1) {
      event.Skip();
      return;
    }

    auto* data = model_->DataForItem(data_view_item);
    if (!data || data->kind != ItemKind::Field) {
      event.Skip();
      return;
    }

    StartInlineEdit(data_view_item, column);
  }

  void OnContextMenu(wxDataViewEvent& event) {
    const auto item = event.GetItem();
    if (item.IsOk()) {
      tree_->Select(item);
    }

    wxMenu menu;
    auto* data = item.IsOk() ? model_->DataForItem(item) : nullptr;
    const auto is_group = data && data->kind == ItemKind::Group;
    if (is_group) {
      const auto group_has_tags = GroupHasVisibleTags(*data);
      menu.Append(ID_ADD_TAG, "Add Tag");
      menu.AppendSeparator();
      menu.Append(ID_COPY_TAG, "Copy All Tags");
      menu.Append(ID_CUT_TAG, "Cut All Tags");
      menu.Append(ID_PASTE_TAG, "Paste Tags");
      menu.AppendSeparator();
      menu.Append(ID_DELETE_TAG, "Delete Visible Fields");
      menu.Enable(ID_COPY_TAG, group_has_tags);
      menu.Enable(ID_CUT_TAG, group_has_tags);
      menu.Enable(ID_DELETE_TAG, group_has_tags);
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

  void OnItemActivated(wxDataViewEvent& event) {
    auto* data = model_->DataForItem(event.GetItem());
    if (!data || data->kind != ItemKind::Field) {
      return;
    }

    StartInlineEdit(event.GetItem(), 0);
  }

  void PopulateTree() {
    inline_item_to_edit_ = wxDataViewItem();
    model_->Rebuild(document_);
    for (const auto& item : model_->RootItems()) {
      tree_->Expand(item);
    }
    ResizeColumns();
    if (pending_inline_edit_) {
      const auto item =
          model_->FindField(pending_inline_edit_->tag_index,
                            pending_inline_edit_->simple_path);
      if (item.IsOk()) {
        inline_item_to_edit_ = item;
        inline_column_to_edit_ = pending_inline_edit_->column;
      }
    }
    if (inline_item_to_edit_.IsOk()) {
      const auto item = inline_item_to_edit_;
      const auto column = inline_column_to_edit_;
      pending_inline_edit_.reset();
      CallAfter([this, item, column]() {
        tree_->Select(item);
        tree_->EnsureVisible(item);
        StartInlineEdit(item, column);
      });
    }
  }

  std::vector<ItemData*> SelectedData() const {
    wxDataViewItemArray selected;
    tree_->GetSelections(selected);
    std::vector<ItemData*> data;
    for (const auto& item : selected) {
      if (item.IsOk()) {
        if (auto* item_data = model_->DataForItem(item)) {
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

  void ResizeColumns() {
    if (!tree_) {
      return;
    }
    const auto width = tree_->GetClientSize().GetWidth();
    if (width <= 0) {
      return;
    }
    const auto available = std::max(kNameColumnMinWidth + kValueColumnMinWidth,
                                    width - 12);
    const auto name_width =
        std::max(kNameColumnMinWidth, std::min(190, available * 38 / 100));
    const auto value_width = std::max(kValueColumnMinWidth, available - name_width);
    if (auto* column = tree_->GetColumn(0)) {
      column->SetWidth(name_width);
    }
    if (auto* column = tree_->GetColumn(1)) {
      column->SetWidth(value_width);
    }
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

  void ApplyFieldColumn(const ItemData& data, unsigned column, const std::string& value) {
    if (data.tag_index < document_.tags.size()) {
      if (auto* tag = find_simple(document_.tags[data.tag_index], data.simple_path)) {
        if (column == 0) {
          tag->name = value;
        } else {
          tag->string_value = value;
        }
      }
    }
  }

  void DeleteSelected() {
    const auto selected = SelectedData();
    if (selected.empty()) {
      return;
    }

    bool changed = false;
    for (const auto* data : selected) {
      if (data->kind == ItemKind::Group) {
        changed = DeleteGroup(*data) || changed;
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
        changed = true;
      }
    }
    if (!changed) {
      return;
    }
    RemoveEmptyTagEntries();
    PushHistory();
    PopulateTree();
  }

  bool DeleteGroup(const ItemData& group) {
    auto changed = false;
    for (auto& entry : document_.tags) {
      const auto target = TargetForTag(entry.targets);
      if (same_target(target, group.target)) {
        const auto had_visible_tags = has_visible_simple_tags(entry.simple_tags);
        erase_visible_simple_tags(entry.simple_tags);
        changed = had_visible_tags || changed;
      }
    }
    return changed;
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

  bool GroupHasVisibleTags(const ItemData& group) const {
    for (const auto& entry : document_.tags) {
      const auto target = TargetForTag(entry.targets);
      if (same_target(target, group.target) &&
          has_visible_simple_tags(entry.simple_tags)) {
        return true;
      }
    }
    return false;
  }

  void StartInlineEdit(wxDataViewItem item, unsigned column) {
    if (inline_editor_) {
      FinishInlineEdit(true);
      return;
    }
    if (!item.IsOk() || column > 1) {
      return;
    }
    auto* data = model_->DataForItem(item);
    if (!data || data->kind != ItemKind::Field) {
      return;
    }

    auto* data_view_column = tree_->GetColumn(column);
    if (!data_view_column) {
      return;
    }

    auto rect = tree_->GetItemRect(item, data_view_column);
    if (rect.IsEmpty()) {
      return;
    }

    const auto value = column == 0 ? FieldName(*data) : FieldValue(*data);
    inline_item_ = item;
    inline_column_ = column;
    inline_original_value_ = value;
    inline_editor_ =
        new wxTextCtrl(tree_, ID_INLINE_EDITOR, wxString::FromUTF8(value),
                       wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    inline_editor_->SetFont(tree_->GetFont());
    const auto original_height = rect.GetHeight();
    rect.Deflate(1, 0);
    const auto editor_height =
        mte::inline_editor_height(original_height,
                                  inline_editor_->GetBestSize().GetHeight(),
                                  tree_->FromDIP(4));
    rect.SetY(std::max(0, rect.GetY() - (editor_height - original_height) / 2));
    rect.SetHeight(editor_height);
    inline_editor_->SetSize(rect);
    inline_editor_->SetSelection(-1, -1);
    inline_editor_->Bind(wxEVT_TEXT_ENTER, &MainFrame::OnInlineTextEnter, this);
    inline_editor_->Bind(wxEVT_CHAR_HOOK, &MainFrame::OnInlineCharHook, this);
    inline_editor_->Bind(wxEVT_KILL_FOCUS, &MainFrame::OnInlineKillFocus, this);
    inline_editor_->SetFocus();
  }

  void OnInlineTextEnter(wxCommandEvent&) {
    FinishInlineEdit(true);
  }

  void OnInlineCharHook(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_ESCAPE) {
      FinishInlineEdit(false);
      return;
    }
    if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER) {
      FinishInlineEdit(true);
      return;
    }
    event.Skip();
  }

  void OnInlineKillFocus(wxFocusEvent& event) {
    event.Skip();
    if (ending_inline_edit_) {
      return;
    }
    CallAfter([this]() {
      if (inline_editor_) {
        FinishInlineEdit(true);
      }
    });
  }

  void FinishInlineEdit(bool commit) {
    if (!inline_editor_) {
      return;
    }

    ending_inline_edit_ = true;
    auto* editor = inline_editor_;
    const auto value = editor->GetValue().ToStdString();
    const auto item = inline_item_;
    const auto column = inline_column_;
    inline_editor_ = nullptr;
    inline_item_ = wxDataViewItem();
    inline_column_ = 0;
    editor->Destroy();
    ending_inline_edit_ = false;

    if (!commit || value == inline_original_value_) {
      inline_original_value_.clear();
      return;
    }

    inline_original_value_.clear();
    if (!item.IsOk()) {
      return;
    }
    auto* data = model_->DataForItem(item);
    if (data && data->kind == ItemKind::Field) {
      ApplyFieldColumn(*data, column, value);
      PushHistory();
      PopulateTree();
    }
  }

  bool ValidateBeforeSave() {
    for (const auto& field : mte::editable_fields(document_)) {
      if (field.name.empty() || field.value.empty()) {
        wxMessageBox("All visible tags must have a name and a value.",
                     "Validation Error", wxOK | wxICON_WARNING, this);
        return false;
      }
    }
    return true;
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
  wxDataViewCtrl* tree_ = nullptr;
  TagTreeModel* model_ = nullptr;
  wxTextCtrl* inline_editor_ = nullptr;

  mte::TagDocument document_;
  std::vector<mte::TagDocument> history_;
  std::vector<mte::SimpleTag> clipboard_;
  std::size_t history_index_ = 0;
  std::size_t saved_history_index_ = 0;
  wxDataViewItem inline_item_;
  unsigned inline_column_ = 0;
  std::string inline_original_value_;
  std::optional<InlineEditRequest> pending_inline_edit_;
  wxDataViewItem inline_item_to_edit_;
  unsigned inline_column_to_edit_ = 0;
  bool dirty_ = false;
  bool ending_inline_edit_ = false;
};

bool FileDropTarget::OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames) {
  if (!frame_ || filenames.empty()) {
    return false;
  }
  return frame_->OpenDroppedFiles(filenames);
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
