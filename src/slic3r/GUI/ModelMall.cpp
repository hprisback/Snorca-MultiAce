#include "ModelMall.hpp"
#include "GUI_App.hpp"

#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include "wx/evtloop.h"

#include "libslic3r/Model.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "slic3r/Utils/MakerWorldUrl.hpp"
#include "slic3r/Utils/Http.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <wx/progdlg.h>
#include <wx/stdpaths.h>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

namespace Slic3r {
namespace GUI {
MakerWorldPanel::MakerWorldPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
    auto layout      = new wxBoxSizer(wxVERTICAL);
    auto toolbar     = new wxBoxSizer(wxHORIZONTAL);
    auto back        = new wxButton(this, wxID_ANY, _L("Back"));
    auto forward     = new wxButton(this, wxID_ANY, _L("Forward"));
    auto home        = new wxButton(this, wxID_ANY, "MakerWorld");
    auto reload      = new wxButton(this, wxID_ANY, _L("Refresh"));
    auto website     = new wxButton(this, wxID_ANY, _L("Open in browser"));
    auto import_file = new wxButton(this, wxID_ANY, _L("Open downloaded file"));
    for (auto button : {back, forward, home, reload, website, import_file})
        toolbar->Add(button, 0, wxALL, FromDIP(4));
    layout->Add(toolbar, 0, wxEXPAND);
    m_status = new wxStaticText(this, wxID_ANY, _L("Download a model, then choose your U1 printer and filament settings before slicing."));
    layout->Add(m_status, 0, wxEXPAND | wxALL, FromDIP(8));
    m_view = WebView::CreateWebView(this, "about:blank");
    layout->Add(m_view, 1, wxEXPAND);
    SetSizer(layout);
    back->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (m_view->CanGoBack())
            m_view->GoBack();
    });
    forward->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (m_view->CanGoForward())
            m_view->GoForward();
    });
    home->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_home(); });
    reload->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_view->Reload(); });
    website->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const auto url = m_view->GetCurrentURL();
        wxLaunchDefaultBrowser(MakerWorld::is_page(url.ToStdString()) ? url : wxString("https://makerworld.com/en"));
    });
    import_file->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        if (wxGetApp().plater())
            wxGetApp().plater()->add_model();
    });
    m_view->Bind(wxEVT_WEBVIEW_NAVIGATING, &MakerWorldPanel::on_navigation, this);
    m_view->Bind(wxEVT_WEBVIEW_NEWWINDOW, &MakerWorldPanel::on_new_window, this);
    m_view->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &MakerWorldPanel::on_message, this);
    // Consume remote events here; they must never reach WebViewPanel's
    // trusted local Flutter handlers or start its account sync timer.
    m_view->Bind(wxEVT_WEBVIEW_NAVIGATED, [](wxWebViewEvent&) {});
    m_view->Bind(wxEVT_WEBVIEW_TITLE_CHANGED, [](wxWebViewEvent&) {});
    m_view->Bind(wxEVT_WEBVIEW_LOADED, [this](wxWebViewEvent&) {
        m_status->SetLabel(_L("Use Download and open on MakerWorld. Review your printer and filament settings before printing."));
    });
    m_view->Bind(wxEVT_WEBVIEW_ERROR, [this](wxWebViewEvent&) {
        m_status->SetLabel(_L("MakerWorld could not load. Try Refresh or Open in browser, then Open downloaded file."));
    });
    CallAfter([this] { open_home(); });
}

void MakerWorldPanel::open_home()
{
    // Public Studio-compatible view, using its documented download bridge.
    // Login stays in this web session; no Snapmaker credentials are sent.
    m_view->LoadURL("https://makerworld.com/en/studio/webview?from=bambustudio");
}

void MakerWorldPanel::on_navigation(wxWebViewEvent& event)
{
    const auto url = event.GetURL().ToStdString();
    if (MakerWorld::is_open_link(url)) {
        event.Veto();
        if (MakerWorld::is_page(m_view->GetCurrentURL().ToStdString()))
            CallAfter([this, url] { download_and_open(url); });
        return;
    }
    auto file_url = url.substr(0, url.find_first_of("?#"));
    if (MakerWorld::is_download(url) && MakerWorld::supported_file(file_url)) {
        event.Veto();
        CallAfter([this, url] { download_and_open(url); });
        return;
    }
    // HTTPS authentication pages may navigate normally, but receive no
    // native commands. Never execute arbitrary schemes or local file URLs.
    if (url != "about:blank" && MakerWorld::https_host(url).empty())
        event.Veto();
}

void MakerWorldPanel::on_new_window(wxWebViewEvent& event)
{
    event.Veto();
    const auto url = event.GetURL().ToStdString();
    if (MakerWorld::is_open_link(url)) {
        if (MakerWorld::is_page(m_view->GetCurrentURL().ToStdString()))
            CallAfter([this, url] { download_and_open(url); });
    } else if (!MakerWorld::https_host(url).empty()) {
        m_view->LoadURL(event.GetURL());
    }
}

void MakerWorldPanel::on_message(wxWebViewEvent& event)
{
    if (!MakerWorld::is_page(m_view->GetCurrentURL().ToStdString()) || !MakerWorld::is_page(event.GetURL().ToStdString()) ||
        event.GetString().size() > 65536)
        return;
    try {
        const auto message = json::parse(event.GetString().utf8_string());
        const auto command = message.value("command", std::string());
        if (command == "makerworld_model_open") {
            const auto url = message.at("model").at("url").get<std::string>();
            CallAfter([this, url] { download_and_open(url); });
        } else if (command == "common_openurl") {
            const auto url = message.value("url", std::string());
            if (MakerWorld::is_open_link(url))
                CallAfter([this, url] { download_and_open(url); });
            else if (!MakerWorld::https_host(url).empty())
                m_view->LoadURL(wxString::FromUTF8(url));
        } else if (command == "homepage_login_or_register") {
            // The full website provides its own login; no Bambu cloud plugin required.
            m_view->LoadURL("https://makerworld.com/en");
        } else if (command == "homepage_leftmenu_switch") {
            open_home();
        }
    } catch (const std::exception&) {
        m_status->SetLabel(_L("MakerWorld sent an unsupported request. Try downloading the file and opening it manually."));
    }
}

void MakerWorldPanel::download_and_open(const std::string& payload)
{
    if (m_downloading || !wxGetApp().plater())
        return;
    const auto download = MakerWorld::parse_download(payload);
    if (!download) {
        wxMessageBox(_L("This link is not a supported MakerWorld model download."), "MakerWorld", wxOK | wxICON_INFORMATION, this);
        return;
    }
    m_downloading = true;
    struct ResetFlag
    {
        bool& flag;
        ~ResetFlag() { flag = false; }
    } reset{m_downloading};
    try {
        namespace fs = boost::filesystem;
        fs::path folder(wxGetApp().app_config->get("download_path"));
        if (folder.empty() || !fs::is_directory(folder))
            folder = fs::path(wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads).ToStdWstring());
        folder /= "MakerWorld";
        fs::create_directories(folder);
        const fs::path    destination = folder / (fs::unique_path("%%%%-%%%%-%%%%").string() + "-" + download.name);
        const fs::path    temporary   = destination.string() + ".download";
        std::atomic<bool> cancel{false};
        std::atomic<int>  percent{0};
        struct Result
        {
            bool        ok{false};
            std::string error;
        };
        auto             worker = std::async(std::launch::async, [&cancel, &percent, download, temporary] {
            Result result;
            try {
                auto http = Http::get(download.url);
                http.secure_public_download()
                    .size_limit(500 * 1024 * 1024)
                    .timeout_connect(20)
                    .timeout_max(300)
                    .on_progress([&](Http::Progress p, bool& stop) {
                        stop = cancel.load();
                        if (p.dltotal)
                            percent = static_cast<int>(std::min<size_t>(99, 100 * p.dlnow / p.dltotal));
                    })
                    .on_error([&](std::string, std::string error, unsigned status) {
                        result.error = "Download failed (HTTP " + std::to_string(status) + "). " + error;
                    })
                    .on_complete([&](std::string body, unsigned status) {
                        if (cancel || status != 200 || body.empty()) {
                            result.error = "No model file was returned.";
                            return;
                        }
                        const auto ext = MakerWorld::lower(fs::path(download.name).extension().string());
                        if ((ext == ".3mf" || ext == ".zip") && body.compare(0, 2, "PK") != 0) {
                            result.error =
                                "MakerWorld returned a web page instead of a model. Sign in or download the file in your browser.";
                            return;
                        }
                        fs::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                        file.write(body.data(), static_cast<std::streamsize>(body.size()));
                        file.close();
                        result.ok = !file.fail();
                        if (!result.ok)
                            result.error = "Could not save the downloaded model.";
                    })
                    .perform_sync();
            } catch (const std::exception& e) {
                result.error = e.what();
            }
            return result;
        });
        wxProgressDialog progress("MakerWorld", _L("Downloading model..."), 100, this, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE);
        while (worker.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
            if (!progress.Update(percent.load()))
                cancel = true;
        }
        const auto result = worker.get();
        progress.Hide();
        if (cancel || !result.ok) {
            if (fs::exists(temporary))
                fs::remove(temporary);
            if (!cancel)
                wxMessageBox(wxString::FromUTF8(result.error) + "\n\n" + _L("Try Open in browser, then Open downloaded file."),
                             "MakerWorld", wxOK | wxICON_WARNING, this);
            return;
        }
        fs::rename(temporary, destination);
        wxGetApp().mainframe->select_tab(static_cast<size_t>(MainFrame::TabPosition::tp3DEditor));
        if (MakerWorld::lower(destination.extension().string()) == ".3mf")
            wxGetApp().plater()->load_project(wxString(destination.wstring()));
        else
            wxGetApp().plater()->load_files(std::vector<fs::path>{destination}, LoadStrategy::LoadModel);
    } catch (const std::exception& e) {
        wxMessageBox(wxString::FromUTF8(e.what()), "MakerWorld", wxOK | wxICON_WARNING, this);
    }
}

    ModelMallDialog::ModelMallDialog(Plater* plater /*= nullptr*/)
        :DPIFrame(nullptr, wxID_ANY, _L("3D Models"), wxDefaultPosition, wxDefaultSize, wxCLOSE_BOX|wxDEFAULT_DIALOG_STYLE|wxMAXIMIZE_BOX|wxMINIMIZE_BOX|wxRESIZE_BORDER)
    {
        // icon
        std::string icon_path = (boost::format("%1%/images/Snapmaker_OrcaTitle.ico") % resources_dir()).str();
        SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

        SetSize(MODEL_MALL_PAGE_SIZE);
        SetMinSize(wxSize(MODEL_MALL_PAGE_SIZE.x / 4, MODEL_MALL_PAGE_SIZE.y / 4));

        wxBoxSizer* m_sizer_main = new wxBoxSizer(wxVERTICAL);

        auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
        m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
        m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);

        m_web_control_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, MODEL_MALL_PAGE_CONTROL_SIZE, wxTAB_TRAVERSAL);
        m_web_control_panel->SetBackgroundColour(*wxWHITE);
        m_web_control_panel->SetSize(MODEL_MALL_PAGE_CONTROL_SIZE);


        wxBoxSizer* m_sizer_web_control = new wxBoxSizer(wxHORIZONTAL);

        auto m_control_back = new ScalableButton(m_web_control_panel, wxID_ANY, "mall_control_back", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
        m_control_back->SetBackgroundColour(*wxWHITE);
        m_control_back->SetSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_back->SetMinSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_back->SetMaxSize(wxSize(FromDIP(25), FromDIP(30)));

        m_control_back->Bind(wxEVT_LEFT_DOWN, &ModelMallDialog::on_back, this);
        m_control_back->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_HAND));});
        m_control_back->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_ARROW));});


        auto m_control_forward = new ScalableButton(m_web_control_panel, wxID_ANY, "mall_control_forward", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
        m_control_forward->SetBackgroundColour(*wxWHITE);
        m_control_forward->SetSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_forward->SetMinSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_forward->SetMaxSize(wxSize(FromDIP(25), FromDIP(30)));

        m_control_forward->Bind(wxEVT_LEFT_DOWN, &ModelMallDialog::on_forward, this);
        m_control_forward->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_HAND)); });
        m_control_forward->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_ARROW)); });

        auto m_control_refresh = new ScalableButton(m_web_control_panel, wxID_ANY, "mall_control_refresh", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
        m_control_refresh->SetBackgroundColour(*wxWHITE);
        m_control_refresh->SetSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_refresh->SetMinSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_refresh->SetMaxSize(wxSize(FromDIP(25), FromDIP(30)));
        m_control_refresh->Bind(wxEVT_LEFT_DOWN, &ModelMallDialog::on_refresh, this);
        m_control_refresh->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_HAND)); });
        m_control_refresh->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCursor(wxCURSOR_ARROW)); });

#ifdef __APPLE__
        // FIXME: maybe should be using GUI::shortkey_ctrl_prefix() or equivalent?
        m_control_back->SetToolTip(_L("Click to return") + "(" + u8"\u2318+" /* u8"⌘+" */ + _L("Left Arrow") + ")");
        m_control_forward->SetToolTip(_L("Click to continue") + "(" + u8"\u2318+"  /* u8"⌘+" */ + _L("Right Arrow") + ")");
#else
        // FIXME: maybe should be using GUI::shortkey_alt_prefix() or equivalent?
        m_control_back->SetToolTip(_L("Click to return") + "(" + _L("Alt+") + _L("Left Arrow") + ")");
        m_control_forward->SetToolTip(_L("Click to continue") + "(" + _L("Alt+") + _L("Right Arrow") + ")");
#endif
        
        m_control_refresh->SetToolTip(_L("Refresh"));
        /* auto m_textCtrl1 = new wxTextCtrl(m_web_control_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(600, 30), 0);
         auto m_button1 = new wxButton(m_web_control_panel, wxID_ANY, wxT("GO"), wxDefaultPosition, wxDefaultSize, 0);
         m_button1->Bind(wxEVT_BUTTON, [this,m_textCtrl1](auto& e) {
             go_to_url(m_textCtrl1->GetValue());
         });*/
        
        m_sizer_web_control->Add( m_control_back, 0, wxALIGN_CENTER | wxLEFT, FromDIP(26) );
        m_sizer_web_control->Add(m_control_forward, 0, wxALIGN_CENTER | wxLEFT, FromDIP(26));
        m_sizer_web_control->Add(m_control_refresh, 0, wxALIGN_CENTER | wxLEFT, FromDIP(26));
        //m_sizer_web_control->Add(m_button1, 0, wxALIGN_CENTER|wxLEFT, 5);
        //m_sizer_web_control->Add(m_textCtrl1, 0, wxALIGN_CENTER|wxLEFT, 5);

        m_web_control_panel->SetSizer(m_sizer_web_control);
        m_web_control_panel->Layout();
        m_sizer_web_control->Fit(m_web_control_panel);

        m_browser = WebView::CreateWebView(this, wxEmptyString);
        if (m_browser == nullptr) {
            wxLogError("Could not init m_browser");
            return;
        }

        m_browser->SetSize(MODEL_MALL_PAGE_WEB_SIZE);
        m_browser->SetMinSize(MODEL_MALL_PAGE_WEB_SIZE);
        m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &ModelMallDialog::OnScriptMessage, this, m_browser->GetId());

        m_sizer_main->Add(m_web_control_panel, 0, wxEXPAND, 0);
        m_sizer_main->Add(m_browser, 1, wxEXPAND, 0);
        SetSizer(m_sizer_main);
        Layout();
        Fit();

        Centre(wxBOTH);
        Bind(wxEVT_SHOW, &ModelMallDialog::on_show, this);

        Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {
            this->Hide();
        });
    }


    ModelMallDialog::~ModelMallDialog()
    {
    }

    void ModelMallDialog::OnScriptMessage(wxWebViewEvent& evt)
    {
        try {
            wxString strInput = evt.GetString();
            json     j = json::parse(strInput.utf8_string());

            wxString strCmd = j["command"];
            
            if(strCmd == "request_close_publish_window") {
                this->Hide();
            }
          
        }
        catch (std::exception&) {
            // wxMessageBox(e.what(), "json Exception", MB_OK);
        }
    }

    void ModelMallDialog::on_dpi_changed(const wxRect& suggested_rect)
    {
    }

    void ModelMallDialog::on_show(wxShowEvent& event)
    {
        wxGetApp().UpdateFrameDarkUI(this);
        if (event.IsShown()) {
            Centre(wxBOTH);
        }
        /*else {
            go_to_url(m_url);
        }*/
        event.Skip();
    }

    void ModelMallDialog::on_refresh(wxMouseEvent& evt)
    {
        if (!m_browser->GetCurrentURL().empty()) {
            m_browser->Reload();
        }
    }

    void ModelMallDialog::on_back(wxMouseEvent& evt)
    {
        if (m_browser->CanGoBack()) {
            m_browser->GoBack();
        }
    }

    void ModelMallDialog::on_forward(wxMouseEvent& evt)
    {
        if (m_browser->CanGoForward()) {
            m_browser->GoForward();
        }
    }

    void ModelMallDialog::go_to_url(wxString url)
    {
        //m_browser->LoadURL(url);
        WebView::LoadUrl(m_browser, url);
    }

    void ModelMallDialog::show_control(bool show) 
    {
        m_web_control_panel->Show(show);
        Layout();
        Fit();
    }

    void ModelMallDialog::go_to_mall(wxString url)
    {
        /*if (!url.empty() && m_homepage_url.empty()) {
            m_homepage_url = url;
        }*/
        if(url.empty())return;
        m_url = url;
        go_to_url(url);
    }

    void ModelMallDialog::go_to_publish(wxString url)
    {
        /*if (!url.empty() && m_publish_url.empty()) {
            m_publish_url = url;
        }*/
        if(url.empty())return;
        m_url = url;
        go_to_url(url);
    }

}
} // namespace Slic3r::GUI
