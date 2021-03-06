// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/api/atom_api_app.h"

#include <string>
#include <vector>

#if defined(OS_WIN)
#include <shlobj.h>
#endif

#include "atom/browser/api/atom_api_menu.h"
#include "atom/browser/atom_browser_context.h"
#include "atom/browser/atom_browser_main_parts.h"
#include "atom/browser/browser.h"
#include "atom/common/native_mate_converters/file_path_converter.h"
#include "atom/common/native_mate_converters/gurl_converter.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "brightray/browser/brightray_paths.h"
#include "native_mate/callback.h"
#include "native_mate/dictionary.h"
#include "native_mate/object_template_builder.h"
#include "net/base/load_flags.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

#if defined(OS_WIN)
#include "base/strings/utf_string_conversions.h"
#endif

#include "atom/common/node_includes.h"

using atom::Browser;

namespace mate {

#if defined(OS_WIN)
template<>
struct Converter<Browser::UserTask> {
  static bool FromV8(v8::Isolate* isolate, v8::Local<v8::Value> val,
                     Browser::UserTask* out) {
    mate::Dictionary dict;
    if (!ConvertFromV8(isolate, val, &dict))
      return false;
    if (!dict.Get("program", &(out->program)) ||
        !dict.Get("title", &(out->title)))
      return false;
    if (dict.Get("iconPath", &(out->icon_path)) &&
        !dict.Get("iconIndex", &(out->icon_index)))
      return false;
    dict.Get("arguments", &(out->arguments));
    dict.Get("description", &(out->description));
    return true;
  }
};
#endif

}  // namespace mate


namespace atom {

namespace api {

namespace {

// Return the path constant from string.
int GetPathConstant(const std::string& name) {
  if (name == "appData")
    return brightray::DIR_APP_DATA;
  else if (name == "userData")
    return brightray::DIR_USER_DATA;
  else if (name == "cache")
    return brightray::DIR_CACHE;
  else if (name == "userCache")
    return brightray::DIR_USER_CACHE;
  else if (name == "home")
    return base::DIR_HOME;
  else if (name == "temp")
    return base::DIR_TEMP;
  else if (name == "userDesktop")
    return base::DIR_USER_DESKTOP;
  else if (name == "exe")
    return base::FILE_EXE;
  else if (name == "module")
    return base::FILE_MODULE;
  else
    return -1;
}

class ResolveProxyHelper {
 public:
  ResolveProxyHelper(const GURL& url, App::ResolveProxyCallback callback)
      : callback_(callback) {
    auto browser_context = AtomBrowserMainParts::Get()->browser_context();
    net::ProxyService* proxy_service = browser_context->
        url_request_context_getter()->GetURLRequestContext()->proxy_service();

    // Start the request.
    int result = proxy_service->ResolveProxy(
        url, net::LOAD_NORMAL, &proxy_info_,
        base::Bind(&ResolveProxyHelper::OnResolveProxyCompleted,
                   base::Unretained(this)),
        &pac_req_, nullptr, net::BoundNetLog());

    // Completed synchronously.
    if (result != net::ERR_IO_PENDING)
      OnResolveProxyCompleted(result);
  }

  void OnResolveProxyCompleted(int result) {
    std::string proxy;
    if (result == net::OK)
      proxy = proxy_info_.ToPacString();
    callback_.Run(proxy);

    delete this;
  }

 private:
  App::ResolveProxyCallback callback_;
  net::ProxyInfo proxy_info_;
  net::ProxyService::PacRequest* pac_req_;

  DISALLOW_COPY_AND_ASSIGN(ResolveProxyHelper);
};

}  // namespace

App::App() {
  Browser::Get()->AddObserver(this);
}

App::~App() {
  Browser::Get()->RemoveObserver(this);
}

void App::OnBeforeQuit(bool* prevent_default) {
  *prevent_default = Emit("before-quit");
}

void App::OnWillQuit(bool* prevent_default) {
  *prevent_default = Emit("will-quit");
}

void App::OnWindowAllClosed() {
  Emit("window-all-closed");
}

void App::OnQuit() {
  Emit("quit");
}

void App::OnOpenFile(bool* prevent_default, const std::string& file_path) {
  *prevent_default = Emit("open-file", file_path);
}

void App::OnOpenURL(const std::string& url) {
  Emit("open-url", url);
}

void App::OnActivateWithNoOpenWindows() {
  Emit("activate-with-no-open-windows");
}

void App::OnWillFinishLaunching() {
  Emit("will-finish-launching");
}

void App::OnFinishLaunching() {
  Emit("ready");
}

base::FilePath App::GetPath(mate::Arguments* args, const std::string& name) {
  bool succeed = false;
  base::FilePath path;
  int key = GetPathConstant(name);
  if (key >= 0)
    succeed = PathService::Get(key, &path);
  if (!succeed)
    args->ThrowError("Failed to get path");
  return path;
}

void App::SetPath(mate::Arguments* args,
                  const std::string& name,
                  const base::FilePath& path) {
  bool succeed = false;
  int key = GetPathConstant(name);
  if (key >= 0)
    succeed = PathService::Override(key, path);
  if (!succeed)
    args->ThrowError("Failed to set path");
}

void App::ResolveProxy(const GURL& url, ResolveProxyCallback callback) {
  new ResolveProxyHelper(url, callback);
}

void App::SetDesktopName(const std::string& desktop_name) {
#if defined(OS_LINUX)
  scoped_ptr<base::Environment> env(base::Environment::Create());
  env->SetVar("CHROME_DESKTOP", desktop_name);
#endif
}

void App::SetAppUserModelId(const std::string& app_id) {
#if defined(OS_WIN)
  base::string16 app_id_utf16 = base::UTF8ToUTF16(app_id);
  SetCurrentProcessExplicitAppUserModelID(app_id_utf16.c_str());
#endif
}

mate::ObjectTemplateBuilder App::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  auto browser = base::Unretained(Browser::Get());
  return mate::ObjectTemplateBuilder(isolate)
      .SetMethod("quit", base::Bind(&Browser::Quit, browser))
      .SetMethod("focus", base::Bind(&Browser::Focus, browser))
      .SetMethod("getVersion", base::Bind(&Browser::GetVersion, browser))
      .SetMethod("setVersion", base::Bind(&Browser::SetVersion, browser))
      .SetMethod("getName", base::Bind(&Browser::GetName, browser))
      .SetMethod("setName", base::Bind(&Browser::SetName, browser))
      .SetMethod("isReady", base::Bind(&Browser::is_ready, browser))
      .SetMethod("addRecentDocument",
                 base::Bind(&Browser::AddRecentDocument, browser))
      .SetMethod("clearRecentDocuments",
                 base::Bind(&Browser::ClearRecentDocuments, browser))
#if defined(OS_WIN)
      .SetMethod("setUserTasks",
                 base::Bind(&Browser::SetUserTasks, browser))
#endif
      .SetMethod("setPath", &App::SetPath)
      .SetMethod("getPath", &App::GetPath)
      .SetMethod("resolveProxy", &App::ResolveProxy)
      .SetMethod("setDesktopName", &App::SetDesktopName)
      .SetMethod("setAppUserModelId", &App::SetAppUserModelId);
}

// static
mate::Handle<App> App::Create(v8::Isolate* isolate) {
  return CreateHandle(isolate, new App);
}

}  // namespace api

}  // namespace atom


namespace {

void AppendSwitch(const std::string& switch_string, mate::Arguments* args) {
  auto command_line = base::CommandLine::ForCurrentProcess();
  std::string value;
  if (args->GetNext(&value))
    command_line->AppendSwitchASCII(switch_string, value);
  else
    command_line->AppendSwitch(switch_string);
}

#if defined(OS_MACOSX)
int DockBounce(const std::string& type) {
  int request_id = -1;
  if (type == "critical")
    request_id = Browser::Get()->DockBounce(Browser::BOUNCE_CRITICAL);
  else if (type == "informational")
    request_id = Browser::Get()->DockBounce(Browser::BOUNCE_INFORMATIONAL);
  return request_id;
}

void DockSetMenu(atom::api::Menu* menu) {
  Browser::Get()->DockSetMenu(menu->model());
}
#endif

void Initialize(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context, void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  auto command_line = base::CommandLine::ForCurrentProcess();

  mate::Dictionary dict(isolate, exports);
  dict.Set("app", atom::api::App::Create(isolate));
  dict.SetMethod("appendSwitch", &AppendSwitch);
  dict.SetMethod("appendArgument",
                 base::Bind(&base::CommandLine::AppendArg,
                            base::Unretained(command_line)));
#if defined(OS_MACOSX)
  auto browser = base::Unretained(Browser::Get());
  dict.SetMethod("dockBounce", &DockBounce);
  dict.SetMethod("dockCancelBounce",
                 base::Bind(&Browser::DockCancelBounce, browser));
  dict.SetMethod("dockSetBadgeText",
                 base::Bind(&Browser::DockSetBadgeText, browser));
  dict.SetMethod("dockGetBadgeText",
                 base::Bind(&Browser::DockGetBadgeText, browser));
  dict.SetMethod("dockHide", base::Bind(&Browser::DockHide, browser));
  dict.SetMethod("dockShow", base::Bind(&Browser::DockShow, browser));
  dict.SetMethod("dockSetMenu", &DockSetMenu);
#endif
}

}  // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN(atom_browser_app, Initialize)
