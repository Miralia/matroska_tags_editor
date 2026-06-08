#include <wx/wx.h>

#include "core/tag_document.h"

namespace {

class MainFrame final : public wxFrame {
 public:
  MainFrame()
      : wxFrame(nullptr,
                wxID_ANY,
                "Matroska Tags Editor",
                wxDefaultPosition,
                wxSize(900, 650)) {
    auto* panel = new wxPanel(this);
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    path_ctrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                wxTE_READONLY);
    auto* open = new wxButton(panel, wxID_OPEN, "Open");
    save_button_ = new wxButton(panel, wxID_SAVE, "Save");
    save_button_->Disable();

    toolbar->Add(path_ctrl_, 1, wxEXPAND | wxALL, 6);
    toolbar->Add(open, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 6);
    toolbar->Add(save_button_, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 6);

    auto* placeholder = new wxStaticText(
        panel,
        wxID_ANY,
        "Native rewrite scaffold is ready. Tag reading and editing will be "
        "implemented in the next stages.");
    placeholder->SetWindowStyleFlag(wxALIGN_CENTER);

    root->Add(toolbar, 0, wxEXPAND);
    root->Add(placeholder, 1, wxEXPAND | wxALL, 24);
    panel->SetSizer(root);

    Bind(wxEVT_BUTTON, &MainFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_BUTTON, &MainFrame::OnSave, this, wxID_SAVE);
  }

 private:
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

    path_ctrl_->SetValue(dialog.GetPath());
  }

  void OnSave(wxCommandEvent&) {
    wxMessageBox("Saving is not wired yet.", "Matroska Tags Editor", wxOK | wxICON_INFORMATION);
  }

  wxTextCtrl* path_ctrl_ = nullptr;
  wxButton* save_button_ = nullptr;
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
