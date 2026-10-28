// Unit tests for archive support. Integration tests with real zip/rar tools:
// tests/archive_integration.sh
#include <cassert>
#include <iostream>

#include "ols_archive_posix.hpp"
#include "ols_archive_scan.hpp"

using namespace olsarc;

// Minimal stored-entry ZIP writer for tests (optionally with a padding extra).
static Bytes makeZip(const std::vector<std::pair<std::string, std::string>>& files, size_t localExtra = 0,
                     bool unicodeExtra = false) {
  Bytes out, cd;
  for (const auto& f : files) {
    const uint32_t crc = crc32Of((const uint8_t*)f.second.data(), f.second.size());
    const uint64_t off = out.size();
    std::string extra(localExtra ? localExtra + 4 : 0, '\0');
    if (localExtra) { extra[0] = 0x34; extra[1] = 0x12; extra[2] = (char)localExtra; extra[3] = 0; }
    put32(out, kZipLocal); put16(out, 20); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0x21);
    put32(out, crc); put32(out, (uint32_t)f.second.size()); put32(out, (uint32_t)f.second.size());
    put16(out, (uint16_t)f.first.size()); put16(out, (uint16_t)extra.size());
    out.insert(out.end(), f.first.begin(), f.first.end());
    out.insert(out.end(), extra.begin(), extra.end());
    out.insert(out.end(), f.second.begin(), f.second.end());
    std::string cdExtra;
    if (unicodeExtra) {
      // Unicode Path field describing the raw name (as Info-ZIP writes it).
      std::string data = "\x01";
      const uint32_t ncrc = crc32Of((const uint8_t*)f.first.data(), f.first.size());
      for (int i = 0; i < 4; ++i) data += (char)(ncrc >> (8 * i));
      data += f.first;
      cdExtra += "\x75\x70";
      cdExtra += (char)data.size();
      cdExtra += '\0';
      cdExtra += data;
    }
    put32(cd, kZipCentral); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0x21);
    put32(cd, crc); put32(cd, (uint32_t)f.second.size()); put32(cd, (uint32_t)f.second.size());
    put16(cd, (uint16_t)f.first.size()); put16(cd, (uint16_t)cdExtra.size()); put16(cd, 0); put16(cd, 0); put16(cd, 0);
    put32(cd, 0); put32(cd, (uint32_t)off);
    cd.insert(cd.end(), f.first.begin(), f.first.end());
    cd.insert(cd.end(), cdExtra.begin(), cdExtra.end());
  }
  const uint64_t cdOff = out.size();
  out.insert(out.end(), cd.begin(), cd.end());
  put32(out, kZipEocd); put16(out, 0); put16(out, 0); put16(out, (uint16_t)files.size()); put16(out, (uint16_t)files.size());
  put32(out, (uint32_t)cd.size()); put32(out, (uint32_t)cdOff); put16(out, 0);
  return out;
}

struct MemRandomFile : RandomFile {
  Bytes data;
  uint64_t size() const override { return data.size(); }
  bool readAt(uint64_t off, void* dst, size_t n) override {
    if (off > data.size() || n > data.size() - off) return false;
    std::memcpy(dst, data.data() + off, n);
    return true;
  }
  bool writeAt(uint64_t off, const void* src, size_t n) override {
    if (off + n > data.size()) data.resize(off + n);
    std::memcpy(data.data() + off, src, n);
    return true;
  }
  bool truncate(uint64_t s) override { data.resize(s); return true; }
  bool flush() override { return true; }
};

int main() {
  // vint and RAR4 Unicode name round trips.
  for (uint64_t v : {0ull, 1ull, 127ull, 128ull, 16383ull, 16384ull, 0xFFFFFFFFull, 1ull << 40}) {
    Bytes b;
    putVint(b, v);
    size_t p = 0;
    uint64_t back = 0;
    assert(readVint(b.data(), b.size(), p, back) && back == v && p == b.size());
  }
  for (const ws& name : {ws(L"Audi A4 EDC15 (88).ols"), ws(L"M\x00fcller/\x041c\x0430\x043f\x0430 (3).ols"), ws(L"a"), ws(L"abcd"), ws(L"abcde")}) {
    const std::string field = encodeRar4UnicodeName(name);
    const size_t zero = field.find('\0');
    const ws decoded = decodeRar4Unicode(field.substr(0, zero), (const uint8_t*)field.data() + zero + 1, field.size() - zero - 1);
    assert(decoded == name);
  }

  // ZIP parse + in-place rename with padding + undo journal.
  {
    const std::string body(4096, 'x');
    MemRandomFile f;
    f.data = makeZip({{"maps/Old Project Name That Is Long.ols", body}, {"readme.txt", "hi"}});
    ZipArchive z;
    ws err;
    assert(parseZip(f, z, err) && z.entries.size() == 2 && z.problem.empty());
    const auto plan = zipPlan(z, {{0, "New.ols"}});
    std::vector<ZipInPlacePatch> patches;
    ws why;
    assert(zipPlanInPlace(f, z, plan, patches, why));
    Bytes journal;
    assert(zipRenameInPlace(f, z, plan, patches, [&](const Bytes& j) { journal = j; return true; }, err));
    ZipArchive after;
    assert(parseZip(f, after, err) && after.entries[0].nameUtf8 == "maps/New.ols");
    assert(verifyZipRename(f, z, plan, err));
    // The undo journal restores the original bytes exactly.
    uint64_t originalSize = 0;
    std::vector<std::pair<uint64_t, Bytes>> regions;
    assert(parseUndoJournal(journal, originalSize, regions));
    assert(applyUndo(f, originalSize, regions));
    assert(f.data == makeZip({{"maps/Old Project Name That Is Long.ols", body}, {"readme.txt", "hi"}}));
    // A torn journal is rejected.
    journal[journal.size() / 2] ^= 0xff;
    assert(!parseUndoJournal(journal, originalSize, regions));
  }
  // Longer name: in place impossible, copy rewrite works and verifies.
  {
    MemRandomFile f;
    f.data = makeZip({{"a.ols", std::string(1000, 'y')}, {"b.bin", "data"}}, 0, true);
    ZipArchive z;
    ws err, why;
    assert(parseZip(f, z, err));
    assert(z.entries[0].nameFromUnicodeExtra);
    const auto plan = zipPlan(z, {{0, "A much longer name (12).ols"}});
    std::vector<ZipInPlacePatch> patches;
    assert(!zipPlanInPlace(f, z, plan, patches, why));
    MemorySink sink;
    std::vector<CopyRecord> records;
    assert(zipRewriteCopy(f, sink, z, plan, nullptr, records, err));
    MemorySource out(sink.out.data(), sink.out.size());
    assert(verifyCopyRecords(out, records, nullptr, err));
    assert(verifyZipRename(out, z, plan, err));
    ZipArchive after;
    assert(parseZip(out, after, err));
    assert(after.entries[0].nameUtf8 == "A much longer name (12).ols" && !after.entries[0].nameFromUnicodeExtra);
    assert(after.entries[1].nameFromUnicodeExtra && after.entries[1].nameUtf8 == "b.bin");  // untouched entry keeps its field
    // Non-ASCII new name sets the UTF-8 flag.
    const auto plan2 = zipPlan(z, {{0, "Pe\xc3\xa7" "as (1).ols"}});
    MemorySink sink2;
    assert(zipRewriteCopy(f, sink2, z, plan2, nullptr, records, err));
    MemorySource out2(sink2.out.data(), sink2.out.size());
    ZipArchive after2;
    assert(parseZip(out2, after2, err) && (after2.entries[0].flags & 0x0800) && after2.entries[0].nameUtf8 == "Pe\xc3\xa7" "as (1).ols");
  }
  // Slack of 1-3 bytes cannot hold a padding field.
  {
    MemRandomFile f;
    f.data = makeZip({{"abcd.ols", "z"}});
    ZipArchive z;
    ws err, why;
    assert(parseZip(f, z, err));
    std::vector<ZipInPlacePatch> patches;
    assert(!zipPlanInPlace(f, z, zipPlan(z, {{0, "ab.ols"}}), patches, why));
    assert(zipPlanInPlace(f, z, zipPlan(z, {{0, "abcd.ols"}}), patches, why));  // same length
  }
  // Streaming collector keeps only what the load plan asks for.
  {
    PrefixCollector c(5u << 20);
    Bytes junk(1u << 20, 0x11);
    for (int i = 0; i < 5; ++i) c.feed(junk.data(), junk.size());
    assert(c.done && c.buf.size() == (5u << 20));  // no provable boundary: whole project kept
  }
  // Duplicate names are detected (never renamed into a clash).
  {
    MemRandomFile f;
    f.data = makeZip({{"x.ols", "1"}, {"X.OLS", "2"}});
    ZipArchive z;
    ws err;
    assert(parseZip(f, z, err) && !z.problem.empty());
  }
  // Crash safety of the in-place path.
  {
    // (a) A write failure after the journal is stored restores the archive.
    struct FailingFile : MemRandomFile {
      int writeNo = 0, failAt = -1;  // exactly one write fails (a transient disk error)
      bool writeAt(uint64_t off, const void* src, size_t n) override {
        if (writeNo++ == failAt) return false;
        return MemRandomFile::writeAt(off, src, n);
      }
    };
    FailingFile f;
    const Bytes original = makeZip({{"Old Long Project Name.ols", std::string(3000, 'q')}, {"Another Long Name.ols", "w"}});
    f.data = original;
    ZipArchive z;
    ws err, why;
    assert(parseZip(f, z, err));
    const auto plan = zipPlan(z, {{0, "A.ols"}, {1, "B.ols"}});
    std::vector<ZipInPlacePatch> patches;
    assert(zipPlanInPlace(f, z, plan, patches, why));
    f.failAt = 1;  // first header patch succeeds, the second write fails
    assert(!zipRenameInPlace(f, z, plan, patches, [](const Bytes&) { return true; }, err));
    assert(f.data == original);
  }
  {
    // (b) Power loss mid-rename: a journal left on disk is rolled back on the next scan.
    char dirTemplate[] = "/tmp/olsarc-test-XXXXXX";
    const std::string dir = mkdtemp(dirTemplate);
    const ws path = fromUtf8(dir + "/crash.zip");
    PosixFileOps ops;
    const Bytes original = makeZip({{"Old Long Project Name.ols", std::string(3000, 'q')}});
    assert(ops.writeDurable(path, original));
    auto src = ops.openRead(path);
    ZipArchive z;
    ws err, why;
    assert(parseZip(*src, z, err));
    const auto plan = zipPlan(z, {{0, "Short.ols"}});
    std::vector<ZipInPlacePatch> patches;
    assert(zipPlanInPlace(*src, z, plan, patches, why));
    src.reset();
    auto rw = ops.openReadWrite(path);
    assert(zipRenameInPlace(*rw, z, plan, patches, [&](const Bytes& j) { return ops.writeDurable(undoJournalPath(path), j); }, err));
    rw.reset();
    // Simulate the crash: the journal was never removed and the archive is modified.
    Bytes modified;
    assert(ops.readAll(path, modified) && modified != original && ops.exists(undoJournalPath(path)));
    ws msg;
    assert(recoverInterruptedRename(ops, path, msg));
    Bytes restored;
    assert(ops.readAll(path, restored) && restored == original && !ops.exists(undoJournalPath(path)));
    // A torn journal (crash while writing it) means the archive was never touched.
    Bytes torn = undoJournal(10, {{0, Bytes(4, 1)}});
    torn.resize(torn.size() - 3);
    assert(ops.writeDurable(undoJournalPath(path), torn));
    assert(recoverInterruptedRename(ops, path, msg));
    assert(ops.readAll(path, restored) && restored == original && !ops.exists(undoJournalPath(path)));
    ops.remove(path);
    ::rmdir(dir.c_str());
  }
  // Scan -> plan -> rename through the public API on a real file.
  {
    char dirTemplate[] = "/tmp/olsarc-test-XXXXXX";
    const std::string dir = mkdtemp(dirTemplate);
    const ws path = fromUtf8(dir + "/plan.zip");
    PosixFileOps ops;
    assert(ops.writeDurable(path, makeZip({{"x/a.ols", "1"}, {"x/b.ols", "2"}, {"x/keep.txt", "3"}})));
    auto src = ops.openRead(path);
    ArchiveListing listing;
    ws err;
    assert(listArchive(*src, ArchiveKind::Zip, listing, err) && listing.projects.size() == 2);
    src.reset();
    // The archive changed after the scan: rename must refuse.
    assert(ops.writeDurable(path, makeZip({{"x/a.ols", "1"}, {"x/b2.ols", "2"}, {"x/keep.txt", "3"}})));
    ArchiveRenameOptions options;
    auto res = renameInArchive(ops, path, listing, {{0, "c.ols"}}, options, nullptr);
    assert(!res.ok && res.error.find(L"changed since") != ws::npos);
    ops.remove(path);
    ::rmdir(dir.c_str());
  }
  // Extraction: byte-identical output, never overwrites, no partial files.
  {
    char dirTemplate[] = "/tmp/olsarc-test-XXXXXX";
    const std::string dir = mkdtemp(dirTemplate);
    const ws archive = fromUtf8(dir + "/x.zip");
    PosixFileOps ops;
    const std::string body(70000, 'k');
    assert(ops.writeDurable(archive, makeZip({{"sub/a.ols", body}, {"b.ols", "tiny"}})));
    auto src = ops.openRead(archive);
    ArchiveListing listing;
    ws err;
    assert(listArchive(*src, ArchiveKind::Zip, listing, err) && listing.projects.size() == 2);
    src.reset();
    const ws outA = fromUtf8(dir + "/a.ols"), outB = fromUtf8(dir + "/b.ols");
    assert(ops.writeDurable(outB, Bytes{'o', 'l', 'd'}));  // destination already taken
    std::vector<ExtractResult> results;
    assert(extractProjects(ops, archive, listing, {{0, outA}, {1, outB}},
                           [&](ExtractResult&& r) { results.push_back(std::move(r)); }, nullptr, err));
    assert(results.size() == 2 && results[0].ok && !results[1].ok);
    Bytes got;
    assert(ops.readAll(outA, got) && std::string(got.begin(), got.end()) == body);
    assert(ops.readAll(outB, got) && std::string(got.begin(), got.end()) == "old");  // not overwritten
    assert(!ops.exists(partPath(outA)) && !ops.exists(partPath(outB)));
    // A corrupted entry is rejected by the CRC check and leaves no file behind.
    Bytes zip;
    assert(ops.readAll(archive, zip));
    zip[30 + 9 + 100] ^= 0xff;  // inside a.ols data (stored)
    assert(ops.writeDurable(archive, zip));
    const ws outBad = fromUtf8(dir + "/bad.ols");
    results.clear();
    extractProjects(ops, archive, listing, {{0, outBad}}, [&](ExtractResult&& r) { results.push_back(std::move(r)); }, nullptr, err);
    assert(results.size() == 1 && !results[0].ok && !ops.exists(outBad) && !ops.exists(partPath(outBad)));
    for (const ws& f : {archive, outA, outB}) ops.remove(f);
    ::rmdir(dir.c_str());
  }
  std::cout << "archive unit checks passed\n";
}
