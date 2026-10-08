
#include "common.h"
#include "proxy/Client.h"
#include "recording/RecordingSession.h"
#include "recording/RecordingCoordinator.h"
#include "Settings.h"
#include "recording/SessionController.h"
#include "catalog/Catalog.h"
#include "library/Library.h"
#include "library/Indexing.h"
#include "recording/ResourceGraph.h"
#include "recording/ResponseJournal.h"
#include <chrono>
#include <stdexcept>
#include <thread>

struct RecordingCoordinatorTestAccess {
  static void open(RecordingCoordinator& coordinator,
    RecordingSession::Settings session, const ResourceOwner& owner) {
    coordinator.open(std::move(session), owner);
  }
};

namespace {
  template<typename A, typename B>
  void eq(const A& a, const B& b) {
    if (!(a == b))
      throw std::runtime_error("Self-test assertion failed");
  }

  template<typename A, typename B>
  void named_eq(const char* name, const A& a, const B& b) {
    if (!(a == b))
      throw std::runtime_error(std::string("Self-test assertion failed: ") + name);
  }

  void test_session_controller() {
    const auto directory = std::filesystem::temp_directory_path() /
      ("hamster-controller-test-" + generate_id());
    std::filesystem::create_directories(directory);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{directory};
    RecordingSession::Settings a;
    a.url = "https://first.example/";
    a.output_file = directory / "first.zip";
    RecordingSession::Settings b = a;
    b.url = "https://second.example/";
    b.output_file = directory / "second.zip";
    SessionController controller;
    controller.open("a", a);
    controller.open("b", b);
    auto active = controller.acquire("a");
    eq(bool(active), true);
    eq(controller.close("a"), false);
    eq(bool(controller.acquire("a")), false);
    eq(bool(controller.acquire("b")), true);
    eq(std::filesystem::exists(a.output_file), false);
    bool conflict = false;
    try { controller.open("conflict", a); } catch (const std::logic_error&) { conflict = true; }
    eq(conflict, true); // A draining writer still owns its destination.
    active.reset();
    eq(controller.close("a"), true);
    eq(controller.close("a"), true);
    eq(std::filesystem::exists(a.output_file), true);
    eq(bool(controller.acquire("b")), true);
    controller.open("a-again", a);
    auto second = controller.acquire("b");
    eq(controller.close_all(), false);
    eq(bool(controller.acquire("b")), false);
    second.reset();
    eq(controller.close_all(), true);
    eq(std::filesystem::exists(b.output_file), true);
    bool stopped = false;
    try { controller.open("late", a); } catch (const std::logic_error&) { stopped = true; }
    eq(stopped, true);
  }

  void test_catalog() {
    const auto directory = std::filesystem::temp_directory_path() /
      ("hamster-catalog-test-" + generate_id());
    std::filesystem::create_directories(directory);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{directory};
    const auto database = directory / "catalog.sqlite";
    const auto first = std::vector<Bookmark>{
      {"https://second.example/", "Folder/Second"},
      {"https://first.example/path", "Folder/First"},
      {"https://first.example/path/sub/#section", "Folder/Sub"},
    };
    auto library = Library(directory);
    {
      Catalog catalog(database, library);
      catalog.replace_bookmarks(first);
      const auto snapshot = catalog.get_bookmarks();
      eq(snapshot.size(), size_t{3});
      eq(snapshot[0].path, "Folder/First");
      eq(catalog.find_bookmark("Folder/Second").value(), first[0]);
      eq(catalog.find_bookmark("missing").has_value(), false);
      const auto matches = catalog.find_bookmarks_by_url("https://first.example/path");
      eq(matches.size(), size_t{1});
      eq(matches[0].path, "Folder/First");
      eq(catalog.find_bookmarks_by_url("https://first.example/path?view=1").size(), size_t{1});
      eq(catalog.find_bookmarks_by_url("https://first.example/pathology").empty(), true);
      const auto descendants =
        catalog.find_bookmarks_by_url("https://first.example/path/sub/page.html?view=1");
      eq(descendants.size(), size_t{2});
      eq(descendants[0].path, "Folder/First");
      eq(descendants[1].path, "Folder/Sub");
      const auto root_descendant =
        catalog.find_bookmarks_by_url("https://second.example/games.html");
      eq(root_descendant.size(), size_t{1});
      eq(root_descendant[0].path, "Folder/Second");
      eq(catalog.contains_bookmark_for_https_authority("first.example:443"), true);
      eq(catalog.contains_bookmark_for_https_authority("FIRST.EXAMPLE:443"), true);
      eq(catalog.contains_bookmark_for_https_authority("first.example:8443"), false);
      eq(catalog.contains_bookmark_for_https_authority("untracked.example:443"), false);
      catalog.update_bookmarks(
        {"Folder/Second"}, {{"https://other.example/", "Other"}});
      eq(catalog.find_bookmark("Folder/Second").has_value(), false);
      eq(catalog.find_bookmark("Other").value().url, "https://other.example/");

      auto rejected = false;
      try { catalog.replace_bookmarks(
        {{"https://a.example/", "Duplicate"},
         {"https://b.example/", "Duplicate"}}); }
      catch (const std::invalid_argument&) { rejected = true; }
      eq(rejected, true);
    }
    {
      Catalog reopened(database, library);
      const auto snapshot = reopened.get_bookmarks();
      eq(snapshot, std::vector<Bookmark>({
        {"https://first.example/path", "Folder/First"},
        {"https://first.example/path/sub/#section", "Folder/Sub"},
        {"https://other.example/", "Other"},
      }));
    }
  }

  void test_file_listing_while_recording() {
    const auto directory = std::filesystem::temp_directory_path() /
      ("hamster-library-test-" + generate_id());
    std::filesystem::create_directories(directory);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{directory};

    const auto archive_path = directory / "active.zip";
    const auto url = std::string("https://example.test/index.html");
    {
      auto writer = ArchiveWriter{};
      eq(writer.open(archive_path), true);
      auto headers = HeaderStore{};
      headers.write(url, StatusCode::success_ok, Header{{"Content-Type", "text/html"}});
      eq(writer.write("url", as_byte_view(url)), true);
      eq(writer.write("uid", as_byte_view(std::string("1234"))), true);
      eq(writer.write("headers", as_byte_view(headers.serialize())), true);
      eq(writer.write(to_local_filename(url),
        as_byte_view(std::string("<html><title>Indexed page</title><body>needle</body></html>"))),
        true);
      eq(writer.close(), true);
    }

    auto library = Library(directory);
    auto catalog = Catalog(directory / ".hamster.sqlite", library);
    library.opened("active", archive_path, archive_path);
    const auto listing = catalog.get_archive_files(library.path("active.zip"));
    eq(listing.size(), size_t{1});
    eq(listing[0].url, url);
    catalog.update_search_index(library.committed("active"));
    for (auto attempt = 0; attempt != 100; ++attempt) {
      if (!catalog.search_index_busy())
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto matches = catalog.execute_search("needle", false, 16, 5);
    eq(matches.size(), size_t{1});
    eq(matches[0].url, url);
  }

  void test_close_recording_owner() {
    const auto directory = std::filesystem::temp_directory_path() /
      ("hamster-close-owner-test-" + generate_id());
    std::filesystem::create_directories(directory);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{directory};

    const auto url = std::string("https://example.test/index.html");
    auto settings = Settings{};
    settings.library_root = directory;
    auto library = Library(directory);
    auto catalog = Catalog(directory / ".hamster.sqlite", library);
    auto recordings = RecordingCoordinator(settings, library, catalog);
    auto session = RecordingSession::Settings{};
    session.url = url;
    session.output_file = "active.zip";
    const auto owner = ResourceOwner{"active.zip"};
    RecordingCoordinatorTestAccess::open(recordings, std::move(session), owner);
    eq(recordings.close_owner(owner), true);
    eq(std::filesystem::exists(directory / "active.zip"), true);
  }

  void test_resource_graph() {
    auto graph = ResourceGraph{};
    const auto first = ResourceOwner{"Folder/First"};
    const auto second = ResourceOwner{"Folder/Second"};
    const auto third = ResourceOwner{"Other"};
    eq(graph.claim(first, "https://cdn.example/app.js#fragment"), true);
    eq(graph.claim(first, "https://cdn.example/app.js"), false);
    eq(graph.claim(second, "https://cdn.example/app.js"), true);
    eq(graph.claim(third, "https://cdn.example/app.js"), true);
    eq(graph.claim(first, "https://first.example/style.css"), true);
    eq(graph.contains_https_authority("cdn.example:443"), true);
    eq(graph.contains_https_authority("CDN.EXAMPLE:443"), true);
    eq(graph.contains_https_authority("cdn.example:8443"), false);
    eq(graph.resource_count(), size_t{2});

    const auto owners = graph.owners("https://cdn.example/app.js#ignored");
    eq(owners.size(), size_t{3});
    eq(owners[0], first);
    eq(owners[1], second);
    eq(owners[2], third);

    graph.release(first);
    eq(graph.resource_count(), size_t{1});
    eq(graph.owners("https://cdn.example/app.js").size(), size_t{2});
    graph.release(second);
    graph.release(third);
    eq(graph.resource_count(), size_t{0});

    auto rejected = false;
    try { graph.claim(first, "data:text/plain,no-network"); }
    catch (const std::invalid_argument&) { rejected = true; }
    eq(rejected, true);
  }

  void test_resource_indexing() {
    named_eq("302 redirect classification", is_redirect(StatusCode::redirection_found), true);
    named_eq("304 redirect classification", is_redirect(StatusCode::redirection_not_modified), false);
    named_eq("relative URL", resolve_http_url("https://example.test/a/b/page.html?old=1", "../img/p.png#part").value(),
      "https://example.test/a/img/p.png");
    named_eq("network URL", resolve_http_url("https://example.test/a/page.html", "//cdn.test/lib.js").value(),
      "https://cdn.test/lib.js");
    named_eq("absolute normalized URL",
      resolve_http_url("https://example.test/a/page.html", "HTTPS://cdn.test/a/../lib.js").value(),
      "https://cdn.test/lib.js");
    named_eq("query URL", resolve_http_url("https://example.test/a/page.html", "?new=1").value(),
      "https://example.test/a/page.html?new=1");
    named_eq("data URL", resolve_http_url("https://example.test/a/page.html", "data:text/plain,no").has_value(),
      false);
    named_eq("fragment URL", resolve_http_url("https://example.test/a/page.html", "#local").has_value(), false);

    const auto css = R"CSS(
      /* url(ignored.png) */
      @import "nested/theme.css" screen;
      @import url('../print.css');
      body { background: url(images/background.png#fragment) }
      @font-face { src: url("fonts\2f main.woff2") format("woff2") }
      p::before { content: "url(not-a-resource.png)" }
    )CSS";
    auto css_resources = std::vector<std::string>{};
    for_each_css_resources("https://example.test/assets/main.css", css,
      [&](std::string resource) { css_resources.push_back(std::move(resource)); });
    named_eq("CSS resources", css_resources, std::vector<std::string>({
        "https://example.test/assets/fonts/main.woff2",
        "https://example.test/assets/images/background.png",
        "https://example.test/assets/nested/theme.css",
        "https://example.test/print.css",
      }));

    const auto html = R"HTML(
      <!doctype html><html><head>
      <base href="/assets/">
      <link rel="stylesheet" href="site.css">
      <link rel="canonical" href="ignored.html">
      <script src="js/app.js"></script>
      <style>.hero { background-image: url('hero.webp') }</style>
      </head><body style="background:url(images/body.png)">
      <a href="navigation.html">not a subresource</a>
      <img src="images/one.png"
        srcset="data:image/png;base64,AAAA 1x, images/two.png 2x, images/three.png 3x">
      <iframe src="//frames.test/inside.html"></iframe>
      <object data="movie.bin"></object>
      </body></html>
    )HTML";
    auto html_resources = std::vector<std::string>{};
    for_each_html_resource("https://example.test/path/page.html", html,
      [&](std::string resource) { html_resources.push_back(std::move(resource)); });
    named_eq("HTML resources", html_resources, std::vector<std::string>({
        "https://example.test/assets/hero.webp",
        "https://example.test/assets/images/body.png",
        "https://example.test/assets/images/one.png",
        "https://example.test/assets/images/three.png",
        "https://example.test/assets/images/two.png",
        "https://example.test/assets/js/app.js",
        "https://example.test/assets/movie.bin",
        "https://example.test/assets/site.css",
        "https://frames.test/inside.html",
      }));

  }

  void test_response_journal() {
    auto journal = ResponseJournal(std::chrono::seconds(10), 1024, 4, 512);
    const auto body = std::string("late response");
    journal.store("https://cdn.example/dynamic.js#ignored", StatusCode::success_ok,
      Header{{"Content-Type", "text/javascript"}}, as_byte_view(body), 123);
    eq(journal.size(), size_t{1});
    const auto first = journal.find("https://cdn.example/dynamic.js");
    const auto second = journal.find("https://cdn.example/dynamic.js#other");
    eq(first.has_value(), true);
    eq(second.has_value(), true); // A response can be copied to multiple recordings.
    eq(std::string(as_string_view(*first->data)), body);
    eq(first->response_time, time_t{123});
    journal.clear();
    eq(journal.size(), size_t{});

    auto expired = ResponseJournal(std::chrono::milliseconds(0), 1024, 4, 512);
    expired.store("https://cdn.example/expired.js", StatusCode::success_ok,
      {}, as_byte_view(body), 123);
    eq(expired.find("https://cdn.example/expired.js").has_value(), false);
  }

  void test_response_transform() {
    auto observed = std::string{};
    auto sent = std::string{};
    auto observed_cache = std::string{};
    auto sent_cache = std::string{};
    auto request = ProxyRequest("GET", "/", "", {}, {},
      [&](StatusCode, const Header& header, ByteView data,
          std::shared_ptr<void>, std::function<void()>) {
        sent = std::string(as_string_view(data));
        sent_cache = header.find("Cache-Control")->second;
      }, "https://example.test/");
    request.observe_response([&](StatusCode, const Header& header, ByteView data) {
      observed = std::string(as_string_view(data));
      observed_cache = header.find("Cache-Control")->second;
    });
    request.transform_response([](StatusCode, const Header& header, ByteView) {
      const auto replacement = std::string("browser response");
      const auto bytes = as_byte_view(replacement);
      return std::optional<ProxyRequest::TransformedResponse>(
        ProxyRequest::TransformedResponse{header, ByteVector(bytes.begin(), bytes.end())});
    });
    request.transform_response_headers([](Header& header) {
      header.erase("Cache-Control");
      header.emplace("Cache-Control", "no-store");
    });
    request.send_response(StatusCode::success_ok, {{"Cache-Control", "max-age=3600"}},
      as_byte_view(std::string("archive response")));
    eq(observed, std::string("archive response"));
    eq(sent, std::string("browser response"));
    eq(observed_cache, std::string("max-age=3600"));
    eq(sent_cache, std::string("no-store"));
  }

  void test_settings() {
    const char* defaults[] = { "hamster" };
    auto settings = Settings{};
    eq(interpret_commandline(settings, 1, defaults), true);
    const auto directory = std::filesystem::absolute("hamster").parent_path();
    eq(settings.ca_directory, directory / "ca");
    eq(settings.library_root, directory / "library");
    eq(settings.impersonate, "firefox147");

    const char* configured[] = {
      "hamster", "--library-root", "custom-library", "--impersonate", "chrome150"
    };
    eq(interpret_commandline(settings, 5, configured), true);
    eq(settings.library_root, std::filesystem::absolute("custom-library"));
    eq(settings.impersonate, "chrome150");
  }

  void test_impersonate_profiles() {
    for (const auto* profile : {"firefox144", "chrome150"})
      Client("", "", profile);
    try {
      Client("", "", "not-a-browser");
      throw std::runtime_error("Invalid impersonation profile accepted");
    } catch (const std::invalid_argument&) {
    }
  }

  void test_recording_sessions() {
    const auto directory = std::filesystem::temp_directory_path() /
      ("hamster-session-test-" + generate_id());
    std::filesystem::create_directories(directory / "nested");
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{directory};
    RecordingSession::Settings first_settings;
    first_settings.url = "https://first.example/path";
    first_settings.output_file = directory / "nested" / "first.zip";
    RecordingSession::Settings second_settings;
    second_settings.url = "https://second.example/";
    second_settings.output_file = directory / "second.zip";
    RecordingSession first(first_settings);
    RecordingSession second(second_settings);
    const auto shared_url = std::string("https://cdn.example/shared.js");
    const auto shared_data = std::string("shared-response");
    second.record_response(shared_url, StatusCode::success_ok,
      Header{{"Content-Type", "text/javascript"}}, as_byte_view(shared_data), std::time(nullptr));
    first_settings.url = "https://changed.example/";
    first_settings.output_file = directory / "wrong.zip";
    eq(first.url(), "https://first.example/path");
    first.finish();
    first.finish();
    eq(std::filesystem::exists(directory / "wrong.zip"), false);
    eq(std::filesystem::exists(second_settings.output_file), false);
    {
      ArchiveReader archive;
      eq(archive.open(directory / "nested" / "first.zip"), true);
      eq(std::string(as_string_view(archive.read("url"))), first.url());
      eq(archive.read("uid").empty(), false);
    }
    second.finish();
    {
      ArchiveReader archive;
      eq(archive.open(second_settings.output_file), true);
      eq(std::string(as_string_view(archive.read("url"))), second.url());
      eq(std::string(as_string_view(archive.read(to_local_filename(shared_url)))), shared_data);
    }
    RecordingSession::Settings replay_settings;
    replay_settings.input_file = directory / "nested" / "first.zip";
    RecordingSession replay(replay_settings);
    eq(replay.url(), first.url());
    replay.finish();
  }

  void test_redirect_archive_normalization() {
    const auto directory = std::filesystem::temp_directory_path() /
      ("hamster-redirect-test-" + generate_id());
    std::filesystem::create_directories(directory);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{directory};

    const auto clean_url = std::string("https://example.test/view/item");
    const auto final_url = clean_url + "?temporary-token";
    const auto provisional_data = std::string("provisional document");
    const auto final_data = std::string("final document");
    const auto provisional_header = Header{{"Content-Type", "text/html"}};
    const auto final_header = Header{{"Content-Type", "text/html"}};

    const auto captured_file = directory / "captured.zip";
    auto captured_settings = RecordingSession::Settings{};
    captured_settings.url = clean_url;
    captured_settings.output_file = captured_file;
    {
      auto captured = RecordingSession(captured_settings);
      captured.record_response(clean_url, static_cast<StatusCode>(403), provisional_header,
        as_byte_view(provisional_data), std::time(nullptr));
      captured.record_response(final_url, StatusCode::success_ok, final_header,
        as_byte_view(final_data), std::time(nullptr));
      captured.finish();
    }
    {
      auto archive = ArchiveReader{};
      eq(archive.open(captured_file), true);
      eq(std::string(as_string_view(archive.read(to_local_filename(clean_url)))), final_data);
      eq(archive.read(to_local_filename(final_url)).empty(), true);
      auto headers = HeaderStore{};
      headers.deserialize(as_string_view(archive.read("headers")));
      eq(headers.read(clean_url)->status_code, StatusCode::success_ok);
      eq(headers.read(final_url), nullptr);
    }

    const auto redirected_file = directory / "redirected.zip";
    auto redirected_settings = RecordingSession::Settings{};
    redirected_settings.url = clean_url;
    redirected_settings.output_file = redirected_file;
    {
      auto redirected = RecordingSession(redirected_settings);
      redirected.record_response(clean_url, static_cast<StatusCode>(302),
        Header{{"Location", "/step"}}, {}, std::time(nullptr));
      redirected.record_response("https://example.test/step", static_cast<StatusCode>(301),
        Header{{"Location", "/final"}}, {}, std::time(nullptr));
      redirected.record_response("https://example.test/final", StatusCode::success_ok,
        final_header, as_byte_view(final_data), std::time(nullptr));
      redirected.finish();
    }
    {
      auto archive = ArchiveReader{};
      eq(archive.open(redirected_file), true);
      eq(std::string(as_string_view(archive.read(to_local_filename(clean_url)))), final_data);
      eq(archive.read(to_local_filename("https://example.test/step")).empty(), true);
      eq(archive.read(to_local_filename("https://example.test/final")).empty(), true);
      auto headers = HeaderStore{};
      headers.deserialize(as_string_view(archive.read("headers")));
      eq(headers.read(clean_url)->status_code, StatusCode::success_ok);
      eq(headers.read("https://example.test/final"), nullptr);
    }
  }

  void test_cache_info() {
    const auto now = std::time(nullptr);
    const auto date = format_time(now - 60);
    const auto modified = format_time(now - 600);
    Header response{ { "Date", date }, { "Cache-Control", "max-age=3600" },
      { "ETag", "\"revision-1\"" }, { "Last-Modified", modified } };
    auto info = get_cache_info(StatusCode::success_ok, response, { });
    eq(info.has_value(), true);
    eq(info->expired, false);
    eq(info->etag, "\"revision-1\"");
    eq(info->last_modified_time, now - 600);

    response.erase("Cache-Control");
    response.emplace("Cache-Control", "max-age=30");
    eq(get_cache_info(StatusCode::success_ok, response, { })->expired, true);
    eq(get_cache_info(StatusCode::success_ok, response,
      { { "Cache-Control", "no-store" } }).has_value(), false);
    response.erase("Cache-Control");
    response.emplace("Cache-Control", "max-age=3600");
    eq(get_cache_info(StatusCode::success_ok, response,
      { { "Cache-Control", "no-cache" } })->expired, true);
    response.erase("Cache-Control");
    response.emplace("Cache-Control", "no-store");
    eq(get_cache_info(StatusCode::success_ok, response, { }).has_value(), false);

    response.erase("Cache-Control");
    response.emplace("Expires", format_time(now + 3600));
    eq(get_cache_info(StatusCode::success_ok, response, { })->expired, false);
    response.erase("Expires");
    response.emplace("Expires", format_time(now - 30));
    eq(get_cache_info(StatusCode::success_ok, response, { })->expired, true);
  }

  void test_common() {
    auto observed = false;
    auto sent = false;
    auto proxy_request = ProxyRequest("GET", "/", "", {}, {},
      [&](StatusCode status, const Header&, ByteView data, std::shared_ptr<void>,
          std::function<void()>) {
        sent = status == StatusCode::success_ok && as_string_view(data) == "response";
      }, "https://example.test/");
    proxy_request.observe_response([&](StatusCode status, const Header&, ByteView data) {
      observed = status == StatusCode::success_ok && as_string_view(data) == "response";
    });
    const auto response = std::string("response");
    proxy_request.send_response(StatusCode::success_ok, {}, as_byte_view(response));
    named_eq("proxy response observer", observed, true);
    named_eq("proxy response sent", sent, true);

    eq(format_time(0), "Thu, 01 Jan 1970 00:00:00 GMT");
    eq(parse_time("Thu, 01 Jan 1970 00:00:00 GMT"), 0);
    eq(parse_time(format_time(1588262656)), 1588262656);

    eq(trim(" abc "), "abc");
    eq(trim("\t abc \n"), "abc");

    eq(to_local_filename("http://www.a.com"), "http/www.a.com");
    eq(to_local_filename("http://www.a.com/"), "http/www.a.com/index");
    eq(to_local_filename("http://www.a.com/file.txt"), "http/www.a.com/file.txt");
    eq(to_local_filename("http://www.a.com/sub"), "http/www.a.com/sub");
    eq(to_local_filename("http://www.a.com/sub/"), "http/www.a.com/sub/index");
    eq(to_local_filename("http://www.a.com//file.txt"), "http/www.a.com/file.txt");
    eq(to_local_filename("http://www.a.com/sub//"), "http/www.a.com/sub/index");

    eq(url_from_input("http://www.a.com"), "http://www.a.com");
    eq(url_from_input("https://www.a.com"), "https://www.a.com");
    eq(url_from_input("www.a.com"), "http://www.a.com");
    eq(url_from_input("www.a.com/file.txt"), "http://www.a.com/file.txt");

    eq(is_relative_url("http://www.a.com"), false);
    eq(is_relative_url("http://www.a.com/"), false);
    eq(is_relative_url("http://www.a.com/file.txt"), false);
    eq(is_relative_url("file.txt"), true);
    eq(is_relative_url("/http://www.a.com"), true);
    eq(is_relative_url("/file.txt"), true);
    eq(is_relative_url("./file.txt"), true);
    eq(is_relative_url("../file.txt"), true);

    eq(get_scheme("http://www.a.com"), "http");
    eq(get_scheme("https://www.a.com"), "https");
    eq(get_scheme("data:123"), "data");
    eq(get_scheme("javascript:123"), "javascript");

    eq(get_hostname_port("http://www.a.com"), "www.a.com");
    eq(get_hostname_port("http://www.a.com/"), "www.a.com");
    eq(get_hostname_port("http://www.a.com/sub/"), "www.a.com");
    eq(get_hostname_port("http://www.a.com:8080"), "www.a.com:8080");
    eq(get_hostname_port("http://www.a.com:8080/"), "www.a.com:8080");
    eq(get_hostname_port("http://www.a.com:8080/sub"), "www.a.com:8080");

    eq(get_scheme_hostname_port_path("http://www.a.com"), "http://www.a.com");
    eq(get_scheme_hostname_port_path("http://www.a.com/"), "http://www.a.com/");
    eq(get_scheme_hostname_port_path("http://www.a.com/sub/"), "http://www.a.com/sub/");
    eq(get_scheme_hostname_port_path("http://www.a.com/sub/file"), "http://www.a.com/sub/file");
    eq(get_scheme_hostname_port_path("http://www.a.com/file?query"), "http://www.a.com/file");
    eq(get_scheme_hostname_port_path("http://www.a.com/file#fragment"), "http://www.a.com/file");
    eq(get_scheme_hostname_port_path("http://www.a.com:8080"), "http://www.a.com:8080");
    eq(get_scheme_hostname_port_path("http://www.a.com:8080/"), "http://www.a.com:8080/");
    eq(get_scheme_hostname_port_path("http://www.a.com:8080/sub/"), "http://www.a.com:8080/sub/");
    eq(get_scheme_hostname_port_path("http://www.a.com:8080/sub/file"), "http://www.a.com:8080/sub/file");
    eq(get_scheme_hostname_port_path("http://www.a.com:8080/file?query"), "http://www.a.com:8080/file");
    eq(get_scheme_hostname_port_path("http://www.a.com:8080/file#fragment"), "http://www.a.com:8080/file");

    eq(get_file_extension("http://www.a.com:8080/file?query"), "");
    eq(get_file_extension("http://www.a.com:8080/file.txt?query"), "txt");
    eq(get_file_extension("http://www.a.com:8080/file.PNG?query"), "PNG");
    eq(get_file_extension("http://www.a.com:8080/file.1234?query"), "1234");
    eq(get_file_extension("http://www.a.com:8080/file.12345?query"), "");
    eq(get_file_extension("http://www.a.com:8080/test.xyz/file?query"), "");
  }

  void test_logic() {
    auto settings = RecordingSession::Settings{ };

    const auto check = [&settings](bool archived, bool expired,
        const FileRequestAction& expected) {
      const auto result = get_file_request_action(settings, archived, expired);
      eq(result.serve, expected.serve);
      eq(result.write, expected.write);
      eq(result.download, expected.download);
    };
    const auto check_not_archived = [&](const FileRequestAction& expected) { check(false, true, expected); };
    const auto check_expired      = [&](const FileRequestAction& expected) { check(true, true, expected); };
    const auto check_not_expired  = [&](const FileRequestAction& expected) { check(true, false, expected); };

    // standard
    settings.download_policy = DownloadPolicy::standard;
    settings.serve_policy = ServePolicy::latest;
    settings.archive_policy = ArchivePolicy::latest;
    check_not_archived({ false, false, true });
    check_expired({      false, false, true });
    check_not_expired({  true,  true,  false });

    // download always
    settings.download_policy = DownloadPolicy::always;
    check_not_archived({ false, false, true });
    check_expired({      false, false, true });
    check_not_expired({  false, false, true });

    // download never
    settings.download_policy = DownloadPolicy::never;
    check_not_archived({ false, false, false });
    check_expired({      true,  true,  false });
    check_not_expired({  true,  true,  false });

    // standard, serve last archived
    settings.download_policy = DownloadPolicy::standard;
    settings.serve_policy = ServePolicy::last_archived;
    settings.archive_policy = ArchivePolicy::latest;
    check_not_archived({ false, false, true });
    check_expired({      true,  false, true });
    check_not_expired({  true,  true,  false });

    // download always, serve last archived
    settings.download_policy = DownloadPolicy::always;
    check_not_archived({ false, false, true });
    check_expired({      true,  false, true });
    check_not_expired({  true,  false, true });

    // download never, serve last archived
    settings.download_policy = DownloadPolicy::never;
    check_not_archived({ false, false, false });
    check_expired({      true,  true,  false });
    check_not_expired({  true,  true,  false });

    // standard, serve first archived
    settings.download_policy = DownloadPolicy::standard;
    settings.serve_policy = ServePolicy::first_archived;
    settings.archive_policy = ArchivePolicy::latest;
    check_not_archived({ false, false, true });
    check_expired({      true,  false, false });
    check_not_expired({  true,  false, false });

    // download always, serve first archived (download always has no effect)
    settings.download_policy = DownloadPolicy::always;
    check_not_archived({ false, false, true });
    check_expired({      true,  false, false });
    check_not_expired({  true,  false, false });

    // download never, serve first archived
    settings.download_policy = DownloadPolicy::never;
    check_not_archived({ false, false, false });
    check_expired({      true,  false, false });
    check_not_expired({  true,  false, false });
  }
} // namepace

void tests() {
  test_catalog();
  test_resource_graph();
  test_resource_indexing();
  test_response_journal();
  test_response_transform();
  test_common();
  test_logic();
  test_cache_info();
  test_settings();
  test_impersonate_profiles();
  test_recording_sessions();
  test_redirect_archive_normalization();
  test_session_controller();
  test_file_listing_while_recording();
  test_close_recording_owner();
}
