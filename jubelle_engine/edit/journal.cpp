/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "journal.h"

#include <array>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace je::edit {
namespace {
constexpr char MAGIC[4] = { 'J', 'E', 'J', '1' };
constexpr uint32_t FORMAT = 1;
enum Kind : uint8_t { STEP = 1, UNDO = 2, REDO = 3, HISTORY = 4, MODEL = 5 };
enum Tag : uint8_t { ABSENT = 0, BOOL = 1, INT = 2, REAL = 3, TEXT = 4, FRACTION = 5, ID = 6, IDLIST = 7 };

uint32_t crc32(const std::string& data)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t {};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) {
        c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

// ---- encoding ----
void putU8(std::string& o, uint8_t v) { o.push_back(char(v)); }
void putU32(std::string& o, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        o.push_back(char((v >> (8 * i)) & 0xFF));
    }
}
void putU64(std::string& o, uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        o.push_back(char((v >> (8 * i)) & 0xFF));
    }
}
void putString(std::string& o, const std::string& s)
{
    putU32(o, uint32_t(s.size()));
    o += s;
}
void putValue(std::string& o, const model::Value& v)
{
    if (std::holds_alternative<bool>(v)) {
        putU8(o, BOOL);
        putU8(o, std::get<bool>(v) ? 1 : 0);
    } else if (std::holds_alternative<int64_t>(v)) {
        putU8(o, INT);
        putU64(o, uint64_t(std::get<int64_t>(v)));
    } else if (std::holds_alternative<double>(v)) {
        putU8(o, REAL);
        uint64_t bits = 0;
        const double d = std::get<double>(v);
        std::memcpy(&bits, &d, sizeof(bits));
        putU64(o, bits);
    } else if (std::holds_alternative<model::Text>(v)) {
        putU8(o, TEXT);
        putString(o, std::get<model::Text>(v).str());
    } else if (std::holds_alternative<Fraction>(v)) {
        putU8(o, FRACTION);
        putU64(o, uint64_t(std::get<Fraction>(v).numerator()));
        putU64(o, uint64_t(std::get<Fraction>(v).denominator()));
    } else if (std::holds_alternative<Id>(v)) {
        putU8(o, ID);
        putU64(o, std::get<Id>(v).value());
    } else if (std::holds_alternative<model::IdList>(v)) {
        putU8(o, IDLIST);
        const auto& ids = std::get<model::IdList>(v).ids();
        putU32(o, uint32_t(ids.size()));
        for (Id id : ids) {
            putU64(o, id.value());
        }
    } else {
        putU8(o, ABSENT);
    }
}

void putStep(std::string& p, const ChangeSet& step)
{
    putString(p, step.actionName);
    putU32(p, uint32_t(step.records.size()));
    for (const ChangeRecord& r : step.records) {
        putU64(p, r.object.value());
        putU32(p, uint32_t(r.field));
        putValue(p, r.before);
        putValue(p, r.after);
    }
}

// ---- decoding ----
struct Reader {
    const std::string& s;
    size_t pos = 0;
    bool bad = false;

    bool need(size_t n)
    {
        if (pos + n > s.size()) {
            bad = true;
        }
        return !bad;
    }
    uint8_t u8() { return need(1) ? uint8_t(s[pos++]) : 0; }
    uint32_t u32()
    {
        if (!need(4)) {
            return 0;
        }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= uint32_t(uint8_t(s[pos++])) << (8 * i);
        }
        return v;
    }
    uint64_t u64()
    {
        if (!need(8)) {
            return 0;
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= uint64_t(uint8_t(s[pos++])) << (8 * i);
        }
        return v;
    }
    std::string str()
    {
        const uint32_t n = u32();
        if (!need(n)) {
            return std::string();
        }
        std::string r = s.substr(pos, n);
        pos += n;
        return r;
    }
    model::Value value(uint64_t& maxId)
    {
        switch (u8()) {
        case ABSENT: return model::Value();
        case BOOL: return model::Value(u8() != 0);
        case INT: return model::Value(int64_t(u64()));
        case REAL: {
            const uint64_t bits = u64();
            double d = 0;
            std::memcpy(&d, &bits, sizeof(d));
            return model::Value(d);
        }
        case TEXT: return model::Value(model::Text(str()));
        case FRACTION: {
            const int64_t n = int64_t(u64());
            const int64_t d = int64_t(u64());
            if (d == 0) {
                bad = true;
                return model::Value();
            }
            return model::Value(Fraction(n, d));
        }
        case ID: {
            const uint64_t id = u64();
            maxId = std::max(maxId, id);
            return model::Value(Id(id));
        }
        case IDLIST: {
            const uint32_t n = u32();
            if (!need(size_t(n) * 8)) {
                return model::Value();
            }
            std::vector<Id> ids;
            ids.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                const uint64_t id = u64();
                maxId = std::max(maxId, id);
                ids.push_back(Id(id));
            }
            return model::Value(model::IdList(std::move(ids)));
        }
        default:
            bad = true;
            return model::Value();
        }
    }
};

bool readStep(Reader& rec, ChangeSet& step, uint64_t& maxId)
{
    step.actionName = rec.str();
    const uint32_t n = rec.u32();
    if (!rec.need(size_t(n) * 14)) {   // the smallest change record
        return false;
    }
    step.records.reserve(n);
    for (uint32_t i = 0; i < n && !rec.bad; ++i) {
        ChangeRecord cr;
        const uint64_t object = rec.u64();
        maxId = std::max(maxId, object);
        cr.object = Id(object);
        cr.field = Field(int32_t(rec.u32()));
        cr.before = rec.value(maxId);
        cr.after = rec.value(maxId);
        step.records.push_back(std::move(cr));
    }
    return !rec.bad;
}

bool readAll(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string header(const JournalBase& base)
{
    std::string h(MAGIC, 4);
    putU32(h, FORMAT);
    putString(h, base.file);
    putU64(h, base.size);
    putU64(h, base.hash);
    return h;
}

//! The records of a journal file: calls `fn(kind, payload reader)` for each complete one
template<typename Fn>
RecoveryReport readJournal(const std::string& path, Fn&& fn)
{
    RecoveryReport r;
    std::string data;
    if (!readAll(path, data)) {
        r.error = "the journal can't be read: " + path;
        return r;
    }
    Reader h { data };
    if (data.size() < 8 || data.compare(0, 4, std::string(MAGIC, 4)) != 0) {
        r.error = "not a Jubelle journal: " + path;
        return r;
    }
    h.pos = 4;
    if (h.u32() != FORMAT) {
        r.error = "a journal of another format version: " + path;
        return r;
    }
    r.base.file = h.str();
    r.base.size = h.u64();
    r.base.hash = h.u64();
    if (h.bad) {
        r.error = "the journal's header is incomplete: " + path;
        return r;
    }
    size_t pos = h.pos;
    while (pos < data.size()) {
        if (pos + 8 > data.size()) {
            r.truncated = true;
            break;
        }
        Reader len { data, pos };
        const uint32_t n = len.u32();
        const uint32_t crc = len.u32();
        if (pos + 8 + n > data.size()) {
            r.truncated = true;
            break;
        }
        const std::string payload = data.substr(pos + 8, n);
        if (crc32(payload) != crc || payload.empty()) {
            r.truncated = true;   // written only in part
            break;
        }
        Reader rec { payload };
        const uint8_t kind = rec.u8();
        if (!fn(kind, rec, r)) {
            return r;
        }
        pos += 8 + n;
    }
    r.ok = true;
    return r;
}
}

bool describeFile(const std::string& path, JournalBase& out)
{
    std::string data;
    if (!readAll(path, data)) {
        return false;
    }
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : data) {
        h = (h ^ c) * 1099511628211ull;
    }
    out.file = path;
    out.size = data.size();
    out.hash = h;
    return true;
}

Journal::~Journal()
{
    close(false);
}

bool Journal::start(const std::string& path, const JournalBase& base, const model::Snapshot& model, const History& history,
                    std::string& error)
{
    close(false);
    m_path = path;
    m_file = std::fopen(path.c_str(), "wb");
    if (!m_file) {
        error = "the journal can't be written: " + path;
        return false;
    }
#ifdef _WIN32
    m_fd = _fileno(m_file);
#else
    m_fd = fileno(m_file);
#endif
    m_syncFailed = false;
    m_bytes = 0;
    if (!writeStart(base, model, history) || !sync()) {
        error = "the journal can't be written: " + path;
        close(true);
        return false;
    }
    return true;
}

bool Journal::writeStart(const JournalBase& base, const model::Snapshot& model, const History& history)
{
    const std::string h = header(base);
    if (std::fwrite(h.data(), 1, h.size(), m_file) != h.size()) {
        return false;
    }
    m_bytes = h.size();
    std::string p;
    p.reserve(model.objectCount() * 64);
    putU8(p, MODEL);
    putU64(p, model.score().value());
    putU32(p, uint32_t(model.objectCount()));
    model.forEachObject([&](const model::Object& o) {
        putU64(p, o.id().value());
        putU32(p, uint32_t(o.fieldCount()));
        o.forEachField([&](Field f, const model::Value& v) {
            putU32(p, uint32_t(f));
            putValue(p, v);
        });
    });
    if (!write(p)) {
        return false;
    }
    if (history.size() == 0) {
        return true;
    }
    p.clear();
    putU8(p, HISTORY);
    putU32(p, uint32_t(history.size()));
    putU32(p, uint32_t(history.index()));
    for (size_t i = 0; i < history.size(); ++i) {
        putStep(p, history.step(i));
    }
    return write(p);
}

bool Journal::append(const std::string& path, std::string& error)
{
    close(false);
    m_path = path;
    m_file = std::fopen(path.c_str(), "ab");
    if (!m_file) {
        error = "the journal can't be written: " + path;
        return false;
    }
#ifdef _WIN32
    m_fd = _fileno(m_file);
#else
    m_fd = fileno(m_file);
#endif
    m_syncFailed = false;
    std::fseek(m_file, 0, SEEK_END);
    m_bytes = uint64_t(std::ftell(m_file));
    return true;
}

bool Journal::write(const std::string& payload)
{
    if (!m_file) {
        return false;
    }
    std::string rec;
    putU32(rec, uint32_t(payload.size()));
    putU32(rec, crc32(payload));
    rec += payload;
    if (std::fwrite(rec.data(), 1, rec.size(), m_file) != rec.size() || std::fflush(m_file) != 0) {
        return false;
    }
    m_bytes += rec.size();
    return true;
}

bool Journal::recordStep(const ChangeSet& step)
{
    if (step.empty()) {
        return true;
    }
    std::string p;
    putU8(p, STEP);
    putStep(p, step);
    return write(p);
}

bool Journal::recordUndo()
{
    return write(std::string(1, char(UNDO)));
}

bool Journal::recordRedo()
{
    return write(std::string(1, char(REDO)));
}

namespace {
bool syncFd(int fd)
{
#ifdef _WIN32
    return _commit(fd) == 0;
#else
    return fsync(fd) == 0;
#endif
}
}

bool Journal::sync()
{
    if (!m_file || std::fflush(m_file) != 0) {
        return false;
    }
    return syncFd(m_fd);
}

void Journal::syncLater()
{
    if (!m_file) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = true;
        if (!m_syncer.joinable()) {
            m_stop = false;
            m_syncer = std::thread([this] { syncLoop(); });
        }
    }
    m_wake.notify_one();
}

void Journal::syncLoop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [this] { return m_pending || m_stop; });
        if (m_stop) {
            return;
        }
        m_pending = false;
        const int fd = m_fd;
        lock.unlock();
        // the records were handed to the system as they were written (fflush): this only waits for the disk
        if (!syncFd(fd)) {
            m_syncFailed = true;
        }
        lock.lock();
    }
}

void Journal::stopSyncer()
{
    if (!m_syncer.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_one();
    m_syncer.join();
    m_pending = false;
    m_stop = false;
}

bool Journal::restart(const JournalBase& base, const model::Snapshot& model, const History& history, std::string& error)
{
    const std::string path = m_path;
    return start(path, base, model, history, error);
}

void Journal::close(bool removeFile)
{
    stopSyncer();
    if (m_file && !removeFile) {
        sync();   // left for recovery: all of it on the disk
    }
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    if (removeFile && !m_path.empty()) {
        std::remove(m_path.c_str());
    }
}

RecoveryReport inspectJournal(const std::string& path)
{
    RecoveryReport r = readJournal(path, [](uint8_t kind, Reader& rec, RecoveryReport& r) {
        r.steps += kind == STEP;
        r.undos += kind == UNDO;
        r.redos += kind == REDO;
        if (kind == HISTORY) {
            r.historySteps = rec.u32();
        } else if (kind == MODEL) {
            rec.u64();
            r.objects = rec.u32();
        }
        return true;
    });
    if (r.ok) {
        JournalBase now;
        r.baseChanged = !describeFile(r.base.file, now) || !(now == r.base);
    }
    return r;
}

RecoveryReport replayJournal(const std::string& path, model::Model& model, History& history, IdGenerator& ids)
{
    history.clear();
    model.replaceAll({}, Id());
    uint64_t maxId = 0;
    bool started = false;
    RecoveryReport r = readJournal(path, [&](uint8_t kind, Reader& rec, RecoveryReport& rep) {
        if (!started && kind != MODEL) {
            rep.error = "the journal doesn't start with the score";
            return false;
        }
        if (kind == MODEL) {
            if (started) {
                rep.error = "the journal starts twice";
                return false;
            }
            started = true;
            const uint64_t score = rec.u64();
            const uint32_t n = rec.u32();
            if (!rec.need(size_t(n) * 12)) {
                rep.error = "the score in the journal is damaged";
                return false;
            }
            std::vector<model::Object> objects;
            objects.reserve(n);
            for (uint32_t i = 0; i < n && !rec.bad; ++i) {
                const uint64_t id = rec.u64();
                const uint32_t nf = rec.u32();
                maxId = std::max(maxId, id);
                std::vector<std::pair<Field, model::Value>> props;
                props.reserve(nf);
                for (uint32_t k = 0; k < nf && !rec.bad; ++k) {
                    const Field f = Field(int32_t(rec.u32()));
                    props.emplace_back(f, rec.value(maxId));
                }
                objects.emplace_back(Id(id), std::move(props));
            }
            if (rec.bad) {
                rep.error = "the score in the journal is damaged";
                return false;
            }
            try {
                model.replaceAll(std::move(objects), Id(score));
            } catch (const std::exception& e) {
                rep.error = std::string("the score in the journal is damaged: ") + e.what();
                return false;
            }
            rep.objects = n;
        } else if (kind == STEP) {
            ChangeSet step;
            if (!readStep(rec, step, maxId)) {
                rep.error = "a damaged step in the journal";
                return false;
            }
            try {
                apply(model, step);
            } catch (const std::exception& e) {
                rep.error = std::string("a step in the journal doesn't fit the score: ") + e.what();
                return false;
            }
            history.push(std::move(step));
            ++rep.steps;
        } else if (kind == HISTORY) {
            if (history.size() != 0 || rep.steps) {
                rep.error = "the journal's history isn't at its start";
                return false;
            }
            const uint32_t n = rec.u32();
            const uint32_t index = rec.u32();
            if (index > n || !rec.need(size_t(n) * 8)) {
                rep.error = "a damaged history in the journal";
                return false;
            }
            std::vector<ChangeSet> steps(n);
            for (uint32_t i = 0; i < n; ++i) {
                if (!readStep(rec, steps[i], maxId)) {
                    rep.error = "a damaged history in the journal";
                    return false;
                }
            }
            history.restore(std::move(steps), index);
            rep.historySteps = n;
        } else if (kind == UNDO) {
            if (!history.canUndo()) {
                rep.error = "the journal undoes a step it doesn't have";
                return false;
            }
            history.undo(model);
            ++rep.undos;
        } else if (kind == REDO) {
            if (!history.canRedo()) {
                rep.error = "the journal redoes a step it doesn't have";
                return false;
            }
            history.redo(model);
            ++rep.redos;
        } else {
            rep.error = "an unknown record in the journal";
            return false;
        }
        return true;
    });
    if (r.ok && !started) {
        r.ok = false;
        r.error = "the journal doesn't hold the score (it was cut short as it started)";
    }
    if (!r.ok) {
        history.clear();
        model.replaceAll({}, Id());
        return r;
    }
    if (maxId) {
        ids.reserve(Id(maxId));
    }
    JournalBase now;
    r.baseChanged = !describeFile(r.base.file, now) || !(now == r.base);
    return r;
}
}
