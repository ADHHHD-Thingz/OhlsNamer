// Command-line driver for archive support (Linux/WSL; also used by CI).
//   archive_cli scan   ARCHIVE...           list projects, map counts and planned names
//   archive_cli rename ARCHIVE... [--copy|--no-rewrite]  in place (ZIP, names that fit) or rewrite
//   archive_cli testrename ARCHIVE [--shorter|--unicode] [--copy|--no-rewrite]
//                                           writer self-test: renames EVERY file entry
//   archive_cli extract ARCHIVE DESTDIR      writes every project out (archive unchanged)
// Build: see build_archive_cli.sh
#include <chrono>
#include <cstdio>
#include <cstring>

#include "ols_archive_posix.hpp"
#include "ols_archive_scan.hpp"

using namespace olsarc;

static bool scanArchive(const ws& path, ArchiveListing& listing, std::vector<ProjectAnalysis>& results) {
  PosixFileOps ops;
  ws msg;
  if (recoverInterruptedRename(ops, path, msg)) printf("  recovery: %s\n", toUtf8(msg).c_str());
  ws err;
  const ArchiveKind kind = archiveKindForName(path);
  if (!listArchiveFile(ops, path, kind, listing, err)) { printf("  error: %s\n", toUtf8(err).c_str()); return false; }
  auto src = ops.openRead(path);
  if (!src) { printf("  cannot open\n"); return false; }
  results.clear();
  if (kind == ArchiveKind::Zip) {
    for (size_t i = 0; i < listing.projects.size(); ++i) results.push_back(analyzeZipProject(*src, listing, i));
  } else {
    if (!analyzeRarProjects(path, listing, [&](ProjectAnalysis&& r) { results.push_back(std::move(r)); },
                            [] { return false; }, err))
      printf("  unrar: %s\n", toUtf8(err).c_str());
    std::sort(results.begin(), results.end(), [](const ProjectAnalysis& a, const ProjectAnalysis& b) { return a.project < b.project; });
  }
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: archive_cli scan|rename ARCHIVE... [--copy]\n");
    return 2;
  }
  const std::string cmd = argv[1];
  bool copyOnly = false, shorter = false, unicode = false, noRewrite = false;
  std::vector<std::string> paths;
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--copy")) copyOnly = true;
    else if (!std::strcmp(argv[i], "--shorter")) shorter = true;
    else if (!std::strcmp(argv[i], "--unicode")) unicode = true;
    else if (!std::strcmp(argv[i], "--no-rewrite")) noRewrite = true;
    else paths.push_back(argv[i]);
  }
  if (cmd == "extract") {
    if (paths.size() != 2) { fprintf(stderr, "usage: archive_cli extract ARCHIVE DESTDIR\n"); return 2; }
    const ws path = fromUtf8(paths[0]);
    std::string dest = paths[1];
    if (!dest.empty() && dest.back() != '/') dest += '/';
    PosixFileOps ops;
    ArchiveListing listing;
    ws err;
    if (!listArchiveFile(ops, path, archiveKindForName(path), listing, err)) {
      printf("error: %s\n", toUtf8(err).c_str());
      return 1;
    }
    std::vector<ExtractRequest> requests;
    std::set<ws> taken;
    for (size_t i = 0; i < listing.projects.size(); ++i) {
      std::string folder, base;
      splitInnerPath(slashNormalized(listing.projects[i].innerPath), folder, base);
      ws name = fromUtf8(base);
      // Free name: existing files and earlier picks in this batch.
      ws candidate = name;
      int n = 2;
      while (taken.count(ols::lower(candidate)) || ops.exists(fromUtf8(dest) + candidate)) {
        const size_t dot = name.find_last_of(L'.');
        candidate = (dot == ws::npos ? name : name.substr(0, dot)) + L" " + std::to_wstring(n++) +
                    (dot == ws::npos ? ws() : name.substr(dot));
      }
      taken.insert(ols::lower(candidate));
      requests.push_back({i, fromUtf8(dest) + candidate});
    }
    int bad = 0;
    const bool ok = extractProjects(ops, path, listing, requests, [&](ExtractResult&& r) {
      printf("  %s %s -> %s %s\n", r.ok ? "ok    " : "FAILED", listing.projects[r.project].innerPath.c_str(),
             toUtf8(requests[r.project].target).c_str(), toUtf8(r.error).c_str());
      if (!r.ok) ++bad;
    }, nullptr, err);
    if (!ok) { printf("error: %s\n", toUtf8(err).c_str()); ++bad; }
    return bad ? 1 : 0;
  }
  if (cmd == "testrename") {
    // Renames every file entry regardless of content: exercises the writers on
    // archives that hold no WinOLS projects (format test corpora).
    int bad = 0;
    for (const auto& p : paths) {
      const ws path = fromUtf8(p);
      PosixFileOps ops;
      auto src = ops.openRead(path);
      ArchiveListing listing;
      ws err;
      printf("%s\n", p.c_str());
      if (!src || !listArchive(*src, archiveKindForName(path), listing, err)) {
        printf("  error: %s\n", toUtf8(err).c_str());
        ++bad;
        continue;
      }
      src.reset();
      std::vector<RenameRequest> requests;
      const bool isZip = listing.kind == ArchiveKind::Zip;
      const size_t count = isZip ? listing.zip.entries.size() : listing.rar.files.size();
      for (size_t k = 0; k < count; ++k) {
        const bool dir = isZip ? listing.zip.entries[k].isDirectory() : listing.rar.blocks[listing.rar.files[k]].directory;
        if (dir) continue;
        std::string folder, base;
        splitInnerPath(isZip ? listing.zip.entries[k].nameUtf8 : listing.rar.blocks[listing.rar.files[k]].name, folder, base);
        std::string nb;
        if (shorter) {
          const size_t dot = base.find_last_of('.');
          nb = "r" + std::to_string(k) + (dot == std::string::npos ? std::string() : base.substr(dot));
        } else {
          nb = (unicode ? std::string("\xc3\x9cn\xc3\xaf_") : std::string("Renamed_")) + base;
        }
        requests.push_back({k, nb});
      }
      ArchiveRenameOptions options;
      options.allowInPlace = !copyOnly;
      options.allowRewrite = !noRewrite;
      const auto res = renameInArchive(ops, path, listing, requests, options, nullptr);
      printf("  testrename: %s method=%s renamed=%zu skipped=%zu %s%s\n", res.ok ? "OK" : "FAILED", toUtf8(res.method).c_str(),
             requests.size() - res.skipped.size(), res.skipped.size(), toUtf8(res.error).c_str(), toUtf8(res.notes).c_str());
      if (!res.ok) ++bad;
    }
    return bad ? 1 : 0;
  }
  int failures = 0;
  for (const auto& p : paths) {
    const ws path = fromUtf8(p);
    printf("%s\n", p.c_str());
    ArchiveListing listing;
    std::vector<ProjectAnalysis> results;
    auto t0 = std::chrono::steady_clock::now();
    if (!scanArchive(path, listing, results)) { ++failures; continue; }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const ArchiveNamePlan plan = planArchiveRenames(listing, results);
    printf("  kind=%s projects=%zu entries=%zu renamable=%s scan=%.2fs%s\n",
           listing.kind == ArchiveKind::Zip ? "zip" : "rar", listing.projects.size(), listing.allNames.size(),
           listing.problem.empty() ? "yes" : toUtf8(listing.problem).c_str(), secs,
           listing.kind == ArchiveKind::Rar ? (listing.rar.solid ? " solid" : " non-solid") : "");
    for (const auto& r : results) {
      const ArchiveProject& pr = listing.projects[r.project];
      if (!r.ok) {
        printf("  [%zu] %s  ERROR %s\n", r.project, pr.innerPath.c_str(), toUtf8(r.error).c_str());
        continue;
      }
      printf("  [%zu] %s  maps=%zu defined=%d safe=%d bytesRead=%llu/%llu -> %s\n", r.project, pr.innerPath.c_str(),
             r.analysis.mapCount, r.analysis.definedCount, r.analysis.renameSafe ? 1 : 0,
             (unsigned long long)r.analysis.bytesLoaded, (unsigned long long)r.analysis.fileSize,
             plan.finalName[r.project].empty() ? "(unchanged)" : toUtf8(plan.finalName[r.project]).c_str());
    }
    if (cmd != "rename") continue;
    PosixFileOps ops;
    ArchiveRenameOptions options;
    options.allowInPlace = !copyOnly;
    options.allowRewrite = !noRewrite;
    auto t1 = std::chrono::steady_clock::now();
    const auto res = renameInArchive(ops, path, listing, plan.requests, options, nullptr);
    const double rsecs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    printf("  rename: %s method=%s renamed=%zu skipped=%zu time=%.2fs %s%s\n", res.ok ? "OK" : "FAILED", toUtf8(res.method).c_str(),
           plan.requests.size() - res.skipped.size(), res.skipped.size(), rsecs, toUtf8(res.error).c_str(), toUtf8(res.notes).c_str());
    if (!res.ok) ++failures;
  }
  return failures ? 1 : 0;
}
