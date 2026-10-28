// Analyses the WinOLS projects inside a ZIP or RAR archive in memory.
#pragma once
#include "ols_archive.hpp"
#include "ols_unrar.hpp"

namespace olsarc {

struct ProjectAnalysis {
  size_t project = 0;  // index into ArchiveListing::projects
  bool ok = false;
  ols::Analysis analysis;
  ws error;
};

// ZIP: one entry, random access. Safe to call concurrently on separate sources.
inline ProjectAnalysis analyzeZipProject(ByteSource& src, const ArchiveListing& listing, size_t project) {
  ProjectAnalysis r;
  r.project = project;
  const ArchiveProject& p = listing.projects[project];
  if (p.size > (1ull << 30)) { r.error = L"Project is larger than 1 GB."; return r; }
  PrefixCollector collector;
  if (!readZipProject(src, listing.zip.entries[p.index], collector, r.error)) return r;
  r.analysis = collector.analyze();
  if (r.analysis.needsFullFile) {
    // Boundary not provable from the prefix: decode the whole project.
    PrefixCollector all;
    all.reset(p.size);
    all.target = p.size;
    if (!readZipProject(src, listing.zip.entries[p.index], all, r.error)) return r;
    r.analysis = ols::analyzeOls(all.buf.data(), all.buf.size(), p.size);
  }
  r.ok = true;
  return r;
}

inline std::string slashNormalized(std::string s) {
  for (auto& c : s) if (c == '\\') c = '/';
  return s;
}

// RAR: one sequential pass through the archive (solid archives must be decoded
// in order). Results are delivered per project through `done`.
inline bool analyzeRarProjects(const ws& archivePath, const ArchiveListing& listing,
                               const std::function<void(ProjectAnalysis&&)>& done,
                               const std::function<bool()>& cancelled, ws& error) {
  std::map<size_t, size_t> byFile;  // file-header index -> project
  for (size_t i = 0; i < listing.projects.size(); ++i) byFile[listing.projects[i].index] = i;
  std::map<size_t, PrefixCollector> collectors;
  std::set<size_t> reported;
  bool mismatch = false;
  olsunrar::Callbacks cb;
  cb.want = [&](const olsunrar::EntryInfo& e) {
    auto it = byFile.find(e.index);
    if (it == byFile.end()) return false;
    const ArchiveProject& p = listing.projects[it->second];
    if (slashNormalized(e.nameUtf8) != slashNormalized(p.innerPath) || e.unpackedSize != p.size) {
      mismatch = true;
      return false;
    }
    if (p.size > (1ull << 30)) return false;
    // The collector follows the same load plan as plain files: it keeps the
    // map/ID area and first ROM image, or the whole project when the boundary
    // cannot be proven, so no second decoding pass is ever needed.
    collectors[e.index].reset(p.size);
    return true;
  };
  cb.data = [&](size_t index, const uint8_t* data, size_t size) {
    auto it = collectors.find(index);
    if (it != collectors.end()) {
      PrefixCollector& c = it->second;
      if (!c.done) c.feed(data, size);
    }
    return true;
  };
  cb.finished = [&](size_t index, bool ok, const ws& why) {
    auto f = byFile.find(index);
    if (f == byFile.end()) return;
    ProjectAnalysis r;
    r.project = f->second;
    PrefixCollector& c = collectors[index];
    if (!ok) {
      r.error = why;
    } else {
      r.analysis = c.analyze();
      r.ok = !r.analysis.needsFullFile;
      if (!r.ok) r.error = L"Map/ID area boundary could not be proven (project needs a full read).";
    }
    collectors.erase(index);
    reported.insert(r.project);
    done(std::move(r));
  };
  cb.cancelled = cancelled;
  const bool ok = olsunrar::streamEntries(archivePath, cb, error);
  if (mismatch) {
    error = L"UnRAR and the header parser disagree on the archive contents.";
    return false;
  }
  for (size_t i = 0; i < listing.projects.size(); ++i) {
    if (reported.count(i)) continue;
    ProjectAnalysis r;
    r.project = i;
    r.error = listing.projects[i].encrypted ? ws(L"Encrypted entry: contents cannot be analysed without the password.")
                                            : (ok ? ws(L"Entry was not decoded.") : error);
    done(std::move(r));
  }
  return ok;
}

// Final names for the projects of one archive, using the same rules as plain
// files: partial identity keeps the old stem, collisions are resolved per
// folder inside the archive (case-insensitively), unchanged names are skipped.
struct ArchiveNamePlan {
  std::vector<ws> finalName;           // per project ("" = not renamed)
  std::vector<RenameRequest> requests;
};

inline ArchiveNamePlan planArchiveRenames(const ArchiveListing& listing, const std::vector<ProjectAnalysis>& results) {
  ArchiveNamePlan plan;
  plan.finalName.assign(listing.projects.size(), ws());
  std::map<ws, std::set<ws>> taken;  // lower(folder) -> lower(base names)
  for (const auto& name : listing.allNames) {
    std::string folder, base;
    splitInnerPath(slashNormalized(name), folder, base);
    taken[ols::lower(fromUtf8(folder))].insert(ols::lower(fromUtf8(base)));
  }
  std::vector<const ProjectAnalysis*> byProject(listing.projects.size(), nullptr);
  for (const auto& r : results)
    if (r.project < byProject.size()) byProject[r.project] = &r;
  // Only projects that may be renamed release their current names; every
  // other entry keeps its name reserved.
  auto renamable = [&](const ProjectAnalysis* r) { return r && r->ok && r->analysis.ok && r->analysis.renameSafe; };
  for (size_t i = 0; i < listing.projects.size(); ++i) {
    if (!renamable(byProject[i])) continue;
    std::string folder, base;
    splitInnerPath(slashNormalized(listing.projects[i].innerPath), folder, base);
    taken[ols::lower(fromUtf8(folder))].erase(ols::lower(fromUtf8(base)));
  }
  for (size_t i = 0; i < listing.projects.size(); ++i) {
    const ProjectAnalysis* r = byProject[i];
    if (!renamable(r)) continue;
    std::string folder, base;
    splitInnerPath(slashNormalized(listing.projects[i].innerPath), folder, base);
    const ws current = fromUtf8(base);
    auto& folderTaken = taken[ols::lower(fromUtf8(folder))];
    const ws wanted = ols::finalProposedName(r->analysis, current);
    const ws final = ols::disambiguate(wanted, folderTaken);
    if (final == current) continue;
    plan.finalName[i] = final;
    plan.requests.push_back({listing.projects[i].index, toUtf8(final)});
  }
  return plan;
}

}  // namespace olsarc
