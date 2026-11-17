#pragma once
#include <filesystem>
#include <fstream>
#include "global/io/ifilesystem.h"
#include "qrcmap.h"
//! Minimal std::filesystem-based IFileSystem for the Qt-free engine tests.
//! ":/..." Qt resource paths are mapped to the real files (see make_qrc_map.py).
class StdFileSystem : public muse::io::IFileSystem
{
    static std::filesystem::path p(const muse::io::path_t& x) {
        const std::string s = x.toStdString();
        if (s.rfind(":/", 0) == 0) {
            auto it = QRC_MAP.find(s);
            if (it != QRC_MAP.end()) { return std::filesystem::u8path(it->second); }
        }
        return std::filesystem::u8path(s); }
public:
    muse::Ret exists(const muse::io::path_t& path) const override { return muse::make_ret(std::filesystem::exists(p(path)) ? muse::Ret::Code::Ok : muse::Ret::Code::UnknownError); }
    muse::Ret remove(const muse::io::path_t& path, bool) override { std::error_code ec; std::filesystem::remove_all(p(path), ec); return muse::make_ok(); }
    muse::Ret clear(const muse::io::path_t&) override { return muse::make_ok(); }
    muse::Ret copy(const muse::io::path_t& a, const muse::io::path_t& b, bool) override { std::error_code ec; std::filesystem::copy(p(a), p(b), std::filesystem::copy_options::overwrite_existing, ec); return muse::make_ok(); }
    muse::Ret move(const muse::io::path_t& a, const muse::io::path_t& b, bool) override { std::error_code ec; std::filesystem::rename(p(a), p(b), ec); return muse::make_ok(); }
    muse::Ret makePath(const muse::io::path_t& path) const override { std::error_code ec; std::filesystem::create_directories(p(path), ec); return muse::make_ok(); }
    muse::Ret makeLink(const muse::io::path_t&, const muse::io::path_t&) const override { return muse::make_ret(muse::Ret::Code::NotSupported); }
    muse::io::EntryType entryType(const muse::io::path_t& path) const override {
        if (std::filesystem::is_directory(p(path))) { return muse::io::EntryType::Dir; }
        if (std::filesystem::is_regular_file(p(path))) { return muse::io::EntryType::File; }
        return muse::io::EntryType::Undefined; }
    muse::RetVal<uint64_t> fileSize(const muse::io::path_t& path) const override { std::error_code ec; auto s = std::filesystem::file_size(p(path), ec); return muse::RetVal<uint64_t>::make_ok(ec ? 0 : s); }
    muse::RetVal<muse::io::paths_t> scanFiles(const muse::io::path_t&, const std::vector<std::string>&, muse::io::ScanMode) const override { return muse::RetVal<muse::io::paths_t>::make_ok({}); }
    void setAttribute(const muse::io::path_t&, Attribute) const override {}
    bool setPermissionsAllowedForAll(const muse::io::path_t&) const override { return true; }
    muse::RetVal<muse::ByteArray> readFile(const muse::io::path_t& path) const override {
        muse::ByteArray d; muse::Ret r = readFile(path, d); muse::RetVal<muse::ByteArray> rv; rv.ret = r; rv.val = d; return rv; }
    muse::Ret readFile(const muse::io::path_t& path, muse::ByteArray& data) const override {
        std::ifstream in(p(path), std::ios::binary); if (!in) { return muse::make_ret(muse::Ret::Code::UnknownError); }
        std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        data = muse::ByteArray(reinterpret_cast<const uint8_t*>(s.data()), s.size()); return muse::make_ok(); }
    muse::Ret writeFile(const muse::io::path_t& path, const muse::ByteArray& data) override {
        std::ofstream out(p(path), std::ios::binary); out.write(reinterpret_cast<const char*>(data.constData()), data.size()); return muse::make_ok(); }
    muse::RetVal<muse::io::StreamId> openStream(const muse::io::path_t&, muse::io::OpenMode) override { return muse::RetVal<muse::io::StreamId>(); }
    muse::Ret writeToStream(muse::io::StreamId, const muse::ByteArray&, uint64_t) override { return muse::make_ret(muse::Ret::Code::NotSupported); }
    muse::Ret closeStream(muse::io::StreamId) override { return muse::make_ok(); }
    muse::io::path_t canonicalFilePath(const muse::io::path_t& f) const override { return f; }
    muse::io::path_t absolutePath(const muse::io::path_t& f) const override { return muse::io::path_t(p(f).parent_path().u8string()); }
    muse::io::path_t absoluteFilePath(const muse::io::path_t& f) const override { return f; }
    muse::DateTime birthTime(const muse::io::path_t&) const override { return muse::DateTime(); }
    muse::DateTime lastModified(const muse::io::path_t&) const override { return muse::DateTime(); }
    muse::Ret isWritable(const muse::io::path_t&) const override { return muse::make_ok(); }
};
