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

// UnRAR's name for an entry against the header parser's name for the same
// position (sizes are compared separately). Two legitimate differences:
// RAR5 file versions (WinRAR -ver) get ";N" appended by UnRAR, and legacy
// RAR4 names without the Unicode flag are decoded with the OEM code page,
// which differs between systems for non-ASCII bytes.
inline bool sameEntryName(const std::string& unrarName, const std::string& parserName) {
  const std::string a = slashNormalized(unrarName), b = slashNormalized(parserName);
  if (a == b) return true;
  if (a.size() > b.size() + 1 && a.compare(0, b.size(), b) == 0 && a[b.size()] == ';' &&
      a.find_first_not_of("0123456789", b.size() + 1) == std::string::npos)
    return true;
  auto ascii = [](const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return false;
    return true;
  };
  return !(ascii(a) && ascii(b));  // encoding ambiguity: position and size decide
}

// Lists an archive for reading (scan, map counts, extraction). RAR archives
// that the built-in header walk refuses (damage, an unfinished download, or a
// layout it does not know) are listed through UnRAR instead: reading and
// extraction then work for every entry UnRAR can reach, and renaming inside
// the archive is disabled (listing.problem says why). Entry indices from both
// sources are the file-header order that streamEntries reports.
inline bool listArchiveFile(ArchiveFileOps& ops, const ws& path, ArchiveKind kind, ArchiveListing& out, ws& error) {
  auto src = ops.openRead(path);
  if (!src) { error = L"Cannot open the archive."; return false; }
  ws parserError;
  if (listArchive(*src, kind, out, parserError)) return true;
  src.reset();
  if (kind != ArchiveKind::Rar) { error = parserError; return false; }
  ArchiveListing fb;
  fb.kind = ArchiveKind::Rar;
  olsunrar::ListResult lr;
  ws unrarError;
  const bool complete = olsunrar::listEntries(path, [&](const olsunrar::EntryInfo& e) {
    fb.allNames.push_back(e.nameUtf8);
    if (!e.directory && endsWithOlsName(e.nameUtf8)) fb.projects.push_back({e.index, e.nameUtf8, e.unpackedSize, e.encrypted});
  }, lr, unrarError);
  if (lr.volume) { error = parserError + L" Multi-volume RAR sets are not supported."; return false; }
  if (lr.entries == 0) { error = parserError + (unrarError.empty() ? ws() : L" UnRAR: " + unrarError); return false; }
  fb.rar.solid = lr.solid;
  fb.unrarListing = true;
  fb.problem = L"Read-only (listed by UnRAR; renaming inside this archive is disabled). Header check: " + parserError;
  if (!complete) fb.problem += L" UnRAR stopped after " + std::to_wstring(lr.entries) + L" entries: " + unrarError;
  out = std::move(fb);
  return true;
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
    if (!sameEntryName(e.nameUtf8, p.innerPath) || e.unpackedSize != p.size) {
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
              : !ok                                   ? error
              : listing.projects[i].size > (1ull << 30) ? ws(L"Project is larger than 1 GB.")
                                                      : ws(L"UnRAR did not return this entry.");
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

// --------------------------------------------------------------- extraction
// Writes selected projects out of an archive to their own files. Each project
// is decoded straight into "<target>.olsrenamer-part", checked (ZIP: size and
// CRC-32; RAR: UnRAR's CRC/BLAKE2 check), flushed and only then renamed to
// the target, which is never overwritten. The archive itself is not modified.
struct ExtractRequest {
  size_t project = 0;  // index into ArchiveListing::projects
  ws target;           // full destination path (caller picks a free name)
};

struct ExtractResult {
  size_t project = 0;
  bool ok = false;
  ws error;
  uint64_t bytes = 0;
};

inline ws partPath(const ws& target) { return target + L".olsrenamer-part"; }

// Re-reads the archive headers and maps each project of `old` (a listing from
// an earlier scan) to the same entry in the archive as it is now. Entry order
// is preserved by every rename path, so the match is by entry position plus
// uncompressed size; names and offsets may have changed (renamed or
// rewritten since the scan). projectMap[oldProject] = new project or npos.
inline bool refreshListing(ArchiveFileOps& ops, const ws& archivePath, const ArchiveListing& old,
                           ArchiveListing& fresh, std::vector<size_t>& projectMap, ws& error) {
  fresh = ArchiveListing();
  if (!listArchiveFile(ops, archivePath, old.kind, fresh, error)) return false;
  std::map<size_t, size_t> byEntry;
  for (size_t i = 0; i < fresh.projects.size(); ++i) byEntry[fresh.projects[i].index] = i;
  projectMap.assign(old.projects.size(), (size_t)-1);
  for (size_t i = 0; i < old.projects.size(); ++i) {
    auto f = byEntry.find(old.projects[i].index);
    if (f != byEntry.end() && fresh.projects[f->second].size == old.projects[i].size) projectMap[i] = f->second;
  }
  return true;
}

inline bool extractProjectsCurrent(ArchiveFileOps& ops, const ws& archivePath, const ArchiveListing& listing,
                                   const std::vector<ExtractRequest>& requests,
                                   const std::function<void(ExtractResult&&)>& done,
                                   const std::function<bool(uint64_t bytesWritten)>& keepGoing, ws& error);

// `listing` may be stale (from the scan); the archive is re-listed first.
// Results report project indices of `listing`.
inline bool extractProjects(ArchiveFileOps& ops, const ws& archivePath, const ArchiveListing& listing,
                            const std::vector<ExtractRequest>& requests,
                            const std::function<void(ExtractResult&&)>& done,
                            const std::function<bool(uint64_t bytesWritten)>& keepGoing, ws& error) {
  ArchiveListing fresh;
  std::vector<size_t> map;
  if (!refreshListing(ops, archivePath, listing, fresh, map, error)) {
    for (const auto& rq : requests) {
      ExtractResult r;
      r.project = rq.project;
      r.error = L"Cannot read the archive: " + error;
      done(std::move(r));
    }
    return false;
  }
  std::vector<ExtractRequest> mapped;
  std::map<size_t, size_t> back;  // new project -> old project
  for (const auto& rq : requests) {
    if (rq.project < map.size() && map[rq.project] != (size_t)-1) {
      mapped.push_back({map[rq.project], rq.target});
      back[map[rq.project]] = rq.project;
    } else {
      ExtractResult r;
      r.project = rq.project;
      r.error = L"The archive changed since the scan and this entry is no longer there; rescan.";
      done(std::move(r));
    }
  }
  if (mapped.empty()) return true;
  return extractProjectsCurrent(ops, archivePath, fresh, mapped, [&](ExtractResult&& r) {
    r.project = back[r.project];
    done(std::move(r));
  }, keepGoing, error);
}

inline bool extractProjectsCurrent(ArchiveFileOps& ops, const ws& archivePath, const ArchiveListing& listing,
                                   const std::vector<ExtractRequest>& requests,
                                   const std::function<void(ExtractResult&&)>& done,
                                   const std::function<bool(uint64_t bytesWritten)>& keepGoing, ws& error) {
  uint64_t written = 0;
  auto finishFile = [&](std::unique_ptr<ArchiveFileOps::WriteFile>& out, const ExtractRequest& rq, bool decodedOk,
                        const ws& why, uint64_t bytes) {
    ExtractResult r;
    r.project = rq.project;
    r.bytes = bytes;
    bool ok = decodedOk && out && bytes == listing.projects[rq.project].size && out->flush();
    out.reset();
    if (ok && !ops.renameNoReplace(partPath(rq.target), rq.target)) {
      ok = false;
      r.error = L"A file with that name appeared in the destination; nothing was overwritten.";
    } else if (!ok) {
      r.error = !why.empty() ? why : (decodedOk ? ws(L"Write error (disk full?).") : ws(L"Decoding failed."));
    }
    if (!ok) ops.remove(partPath(rq.target));
    r.ok = ok;
    done(std::move(r));
  };

  if (listing.kind == ArchiveKind::Zip) {
    auto src = ops.openRead(archivePath);
    if (!src) { error = L"Cannot open the archive."; return false; }
    for (const auto& rq : requests) {
      if (rq.project >= listing.projects.size()) continue;
      const ZipEntry& en = listing.zip.entries[listing.projects[rq.project].index];
      auto out = ops.create(partPath(rq.target));
      if (!out) {
        ExtractResult r;
        r.project = rq.project;
        r.error = L"Cannot create the destination file.";
        done(std::move(r));
        continue;
      }
      uint64_t bytes = 0;
      bool writeOk = true, cancelled = false;
      ws why;
      const bool decoded = decodeZipEntry(*src, en, [&](const uint8_t* p, size_t n) {
        if (!out->write(p, n)) { writeOk = false; return false; }
        bytes += n;
        written += n;
        if (keepGoing && !keepGoing(written)) { cancelled = true; return false; }
        return true;
      }, why);
      if (!writeOk) why = L"Write error (disk full?).";
      finishFile(out, rq, decoded && writeOk, why, bytes);
      if (cancelled) { error = L"Cancelled."; return false; }
    }
    return true;
  }

  // RAR: one sequential pass; each wanted entry streams into its own file.
  std::map<size_t, const ExtractRequest*> byFile;
  for (const auto& rq : requests)
    if (rq.project < listing.projects.size()) byFile[listing.projects[rq.project].index] = &rq;
  std::map<size_t, std::unique_ptr<ArchiveFileOps::WriteFile>> open;
  std::map<size_t, uint64_t> sizes;
  std::map<size_t, bool> writeFailed;
  std::set<size_t> reported;
  std::map<size_t, std::string> otherName;  // entry index -> name UnRAR reported instead
  olsunrar::Callbacks cb;
  cb.want = [&](const olsunrar::EntryInfo& e) {
    auto it = byFile.find(e.index);
    if (it == byFile.end()) return false;
    const ArchiveProject& p = listing.projects[it->second->project];
    if (!sameEntryName(e.nameUtf8, p.innerPath) || e.unpackedSize != p.size) { otherName[e.index] = e.nameUtf8; return false; }
    open[e.index] = ops.create(partPath(it->second->target));
    sizes[e.index] = 0;
    writeFailed[e.index] = !open[e.index];
    return true;
  };
  cb.data = [&](size_t index, const uint8_t* p, size_t n) {
    auto it = open.find(index);
    if (it == open.end() || !it->second || writeFailed[index]) return true;
    if (!it->second->write(p, n)) { writeFailed[index] = true; return true; }
    sizes[index] += n;
    written += n;
    return !(keepGoing && !keepGoing(written));
  };
  cb.finished = [&](size_t index, bool ok, const ws& why) {
    auto rq = byFile.find(index);
    if (rq == byFile.end()) return;
    finishFile(open[index], *rq->second, ok && !writeFailed[index],
               writeFailed[index] ? ws(L"Cannot write the destination file.") : why, sizes[index]);
    reported.insert(index);
    open.erase(index);
  };
  const bool ok = olsunrar::streamEntries(archivePath, cb, error);
  for (auto& o : open) {  // cancelled or aborted mid-entry
    o.second.reset();
    ops.remove(partPath(byFile[o.first]->target));
  }
  for (const auto& rq : byFile) {
    if (reported.count(rq.first)) continue;
    ExtractResult r;
    r.project = rq.second->project;
    auto other = otherName.find(rq.first);
    r.error = listing.projects[rq.second->project].encrypted ? ws(L"Encrypted entry: password required.")
              : !ok                    ? error
              : other != otherName.end() ? L"UnRAR reports a different entry at this position (\"" + fromUtf8(other->second) + L"\"); rescan."
                                       : ws(L"UnRAR did not return this entry (" + fromUtf8(listing.projects[rq.second->project].innerPath) + L").");
    done(std::move(r));
  }
  return ok;
}

}  // namespace olsarc
