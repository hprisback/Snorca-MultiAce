#include "PrinterWebView.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/PrinterWebView.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "common_func/common_func.hpp"

#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <slic3r/GUI/Widgets/WebView.hpp>
#include <wx/webview.h>
#include "slic3r/GUI/SSWCP.hpp"
#include "sentry_wrapper/SentryWrapper.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "slic3r/Utils/WebcamUrl.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/button.h>
#include <wx/frame.h>
#include <wx/msgdlg.h>

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

PrinterWebView::PrinterWebView(wxWindow *parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
 {

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    wxString url      = wxString::FromUTF8(LOCALHOST_URL + std::to_string(wxGetApp().get_page_http_port()) + "/web/flutter_web/index.html?path=2");
    auto     real_url = wxGetApp().get_international_url(url);
      // Create the webview
    m_browser = WebView::CreateWebView(this, real_url);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    m_browser->Bind(wxEVT_WEBVIEW_ERROR, &PrinterWebView::OnError, this);
    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &PrinterWebView::OnLoaded, this);
    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterWebView::OnScriptMessage, this, m_browser->GetId());

    SetSizer(topsizer);

    auto camera_bar    = new wxBoxSizer(wxHORIZONTAL);
    auto camera_button = new wxButton(this, wxID_ANY, _L("Live camera (30 fps)"));
    camera_button->SetToolTip(_L("Open the printer's 30 fps webcam player (extended firmware: /webcam/player)"));
    camera_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_live_camera_button(); });
    camera_bar->AddStretchSpacer(1);
    camera_bar->Add(camera_button, 0, wxALL, FromDIP(4));
    topsizer->Add(camera_bar, 0, wxEXPAND);

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    update_mode();

    //Zoom
    m_zoomFactor = 100;

    //Connect the idle events
    Bind(wxEVT_CLOSE_WINDOW, &PrinterWebView::OnClose, this);

 }

PrinterWebView::~PrinterWebView()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    SSWCP::on_webview_delete(m_browser);

    wxGetApp().fltviews().remove_printer_view(this);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}


void PrinterWebView::load_url(wxString& url, wxString apikey)
{
    if (m_browser == nullptr)
        return;
    m_apikey = apikey;
    m_apikey_sent = false;
    
    if (url.find("path=2") != std::string::npos) {
        wxGetApp().fltviews().add_printer_view(this, url, apikey);
    } else {
        wxGetApp().fltviews().remove_printer_view(this);
    }

    m_browser->Show();
    m_browser->LoadURL(url);

    UpdateState();
}

namespace {
wxFrame* s_camera_frame = nullptr;

void show_camera_frame(const std::string& url)
{
    if (s_camera_frame == nullptr) {
        wxWindow* owner = wxGetApp().mainframe;
        s_camera_frame  = new wxFrame(owner, wxID_ANY, _L("Live camera"), wxDefaultPosition, wxSize(960, 600),
                                      wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT);
        auto sizer = new wxBoxSizer(wxVERTICAL);
        auto view  = wxWebView::New(s_camera_frame, wxID_ANY, wxString::FromUTF8(url));
        if (view == nullptr) {
            s_camera_frame->Destroy();
            s_camera_frame = nullptr;
            wxLaunchDefaultBrowser(wxString::FromUTF8(url));
            return;
        }
        view->SetName("live_camera_view");
        sizer->Add(view, 1, wxEXPAND);
        s_camera_frame->SetSizer(sizer);
        s_camera_frame->Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent& evt) {
            if (s_camera_frame)
                s_camera_frame->Destroy();
            s_camera_frame = nullptr;
            evt.Skip(false);
        });
        s_camera_frame->CenterOnParent();
    } else if (auto view = dynamic_cast<wxWebView*>(s_camera_frame->FindWindowByName("live_camera_view"))) {
        if (view->GetCurrentURL() != wxString::FromUTF8(url))
            view->LoadURL(wxString::FromUTF8(url));
    }
    s_camera_frame->Show();
    s_camera_frame->Raise();
}
} // namespace

void PrinterWebView::open_live_camera(const std::string& printer_host, bool only_if_available)
{
    const auto url = Webcam::player_url(printer_host);
    if (url.empty())
        return;
    if (!only_if_available) {
        show_camera_frame(url);
        return;
    }
    Http::get(url)
        .timeout_connect(2)
        .timeout_max(4)
        .size_limit(1024 * 1024)
        .on_complete([url](std::string, unsigned status) {
            if (status >= 200 && status < 300)
                wxGetApp().CallAfter([url]() { show_camera_frame(url); });
        })
        .on_error([url](std::string, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "Live camera player not available at " << url << ": " << status << " " << error;
        })
        .perform();
}

void PrinterWebView::on_live_camera_button()
{
    std::string host_address;
    std::shared_ptr<PrintHost> host;
    wxGetApp().get_connect_host(host);
    if (host)
        host_address = host->get_host();
    if (Webcam::player_url(host_address).empty() && wxGetApp().preset_bundle) {
        const auto& config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        if (config.has("print_host"))
            host_address = config.opt_string("print_host");
    }
    if (Webcam::player_url(host_address).empty()) {
        wxMessageBox(_L("Connect to your printer first, then open the live camera again."), _L("Live camera"),
                     wxOK | wxICON_INFORMATION, this);
        return;
    }
    open_live_camera(host_address, false);
}

void PrinterWebView::reload()
{
    m_browser->Reload();
}

bool PrinterWebView::isSnapmakerPage()
{
    if (m_browser == nullptr)
        return false;
    auto url = m_browser->GetCurrentURL();
    return (url.find("flutter_web") != std::string::npos);
}

void PrinterWebView::sendMessage(const std::string& msg) {
    WebView::RunScript(m_browser, msg);
}

void PrinterWebView::update_mode()
{
    // m_browser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode"));
    m_browser->EnableAccessToDevTools(true);
}

/**
 * Method that retrieves the current state from the web control and updates the
 * GUI the reflect this current state.
 */
void PrinterWebView::UpdateState() {
  // SetTitle(m_browser->GetCurrentTitle());

}

void PrinterWebView::OnClose(wxCloseEvent& evt)
{
    this->Hide();
}

void PrinterWebView::SendAPIKey()
{
    if (m_apikey_sent || m_apikey.IsEmpty())
        return;
    m_apikey_sent   = true;
    wxString script = wxString::Format(R"(
    // Check if window.fetch exists before overriding
    if (window.fetch) {
        const originalFetch = window.fetch;
        window.fetch = function(input, init = {}) {
            init.headers = init.headers || {};
            init.headers['X-API-Key'] = '%s';
            return originalFetch(input, init);
        };
    }
)",
                                       m_apikey);
    m_browser->RemoveAllUserScripts();

    m_browser->AddUserScript(script);
    m_browser->Reload();
}

void PrinterWebView::OnError(wxWebViewEvent &evt)
{
    auto e = "unknown error";
    switch (evt.GetInt()) {
      case wxWEBVIEW_NAV_ERR_CONNECTION:
        e = "wxWEBVIEW_NAV_ERR_CONNECTION";
        break;
      case wxWEBVIEW_NAV_ERR_CERTIFICATE:
        e = "wxWEBVIEW_NAV_ERR_CERTIFICATE";
        break;
      case wxWEBVIEW_NAV_ERR_AUTH:
        e = "wxWEBVIEW_NAV_ERR_AUTH";
        break;
      case wxWEBVIEW_NAV_ERR_SECURITY:
        e = "wxWEBVIEW_NAV_ERR_SECURITY";
        break;
      case wxWEBVIEW_NAV_ERR_NOT_FOUND:
        e = "wxWEBVIEW_NAV_ERR_NOT_FOUND";
        break;
      case wxWEBVIEW_NAV_ERR_REQUEST:
        e = "wxWEBVIEW_NAV_ERR_REQUEST";
        break;
      case wxWEBVIEW_NAV_ERR_USER_CANCELLED:
        e = "wxWEBVIEW_NAV_ERR_USER_CANCELLED";
        break;
      case wxWEBVIEW_NAV_ERR_OTHER:
        e = "wxWEBVIEW_NAV_ERR_OTHER";
        break;
      }
    BOOST_LOG_TRIVIAL(fatal) << __FUNCTION__<< boost::format(":PrinterWebView error loading page %1% %2% %3% %4%") %evt.GetURL() %evt.GetTarget() %e %evt.GetString();
}

void PrinterWebView::OnLoaded(wxWebViewEvent &evt)
{
    if (evt.GetURL().IsEmpty())
        return;
    SendAPIKey();
}

void PrinterWebView::OnScriptMessage(wxWebViewEvent& evt) {
    // BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetString().ToUTF8().data();

    // if (wxGetApp().get_mode() == comDevelop)
    //     wxLogMessage("Script message received; value = %s, handler = %s", evt.GetString(), evt.GetMessageHandler());

    // test
    SSWCP::handle_web_message(evt.GetString().ToUTF8().data(), m_browser);
}


} // GUI
} // Slic3r
