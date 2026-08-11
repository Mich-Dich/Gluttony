
#include "util/pch.h"
#include "directory_iterator.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT:::vfs {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    directory_entry::directory_entry(std::filesystem::path path)
        : m_path(std::move(path)) {}


    const std::filesystem::path& directory_entry::path() const noexcept             { return m_path; }


    bool directory_entry::exists(std::error_code& error) const noexcept             { return GLT::vfs::exists(m_path, error); }


    bool directory_entry::is_directory(std::error_code& error) const noexcept       { return GLT::vfs::is_directory(m_path, error); }


    bool directory_entry::is_regular_file(std::error_code& error) const noexcept    { return GLT::vfs::is_regular_file(m_path, error); }


    u64 directory_entry::file_size(std::error_code& error) const noexcept           { return GLT::vfs::file_size(m_path, error); }

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    directory_iterator::directory_iterator(const std::filesystem::path& p, std::error_code& error) noexcept
        : m_it(p, error)
    {
        if (!error && m_it != std::filesystem::end(m_it))
            m_current = directory_entry(m_it->path());
        else
            m_it = {}; // become end iterator
    }

    // CLASS PUBLIC ====================================================================================================

    directory_iterator::reference directory_iterator::operator*() const noexcept            { return m_current; }


    directory_iterator::pointer directory_iterator::operator->() const noexcept             { return &m_current; }


    directory_iterator& directory_iterator::operator++() noexcept {

        std::error_code error;
        increment(error);
        return *this;                                                                       // ignore error – on error we become end
    }


    directory_iterator directory_iterator::operator++(int) noexcept {

        auto tmp = *this;
        ++*this;
        return tmp;
    }


    bool operator==(const directory_iterator& a, const directory_iterator& b) noexcept      { return a.m_it == b.m_it; } // Both are end or both have same underlying iterator


    bool operator!=(const directory_iterator& a, const directory_iterator& b) noexcept      { return !(a == b); }


    directory_iterator directory_iterator::begin() const noexcept                           { return *this; }


    directory_iterator directory_iterator::end() const noexcept                             { return directory_iterator(); }


    directory_iterator& directory_iterator::increment(std::error_code& error) noexcept{

        if (m_it == std::filesystem::end(m_it)) {

            error.clear();
            return *this;
        }

        ++m_it;
        if (m_it != std::filesystem::end(m_it))
            m_current = directory_entry(m_it->path());
        else
            m_current = directory_entry{}; // clear current entry

        error.clear();
        return *this;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================





    // CLASS IMPLEMENTATION ============================================================================================

    recursive_directory_iterator::recursive_directory_iterator(const std::filesystem::path& path, directory_options opts, 
        std::error_code& error) noexcept
        : m_it(path, static_cast<std::filesystem::directory_options>(static_cast<u8>(opts)), error) {

        if (!error && m_it != std::filesystem::end(m_it))
            m_current = directory_entry(m_it->path());
        else
            m_it = {};
    }

    // CLASS PUBLIC ====================================================================================================

    recursive_directory_iterator::reference recursive_directory_iterator::operator*() const noexcept        { return m_current; }


    recursive_directory_iterator::pointer recursive_directory_iterator::operator->() const noexcept         { return &m_current; }


    recursive_directory_iterator& recursive_directory_iterator::operator++() noexcept {

        std::error_code error;
        increment(error);
        return *this;
    }


    recursive_directory_iterator recursive_directory_iterator::operator++(int) noexcept {

        auto tmp = *this;
        ++*this;
        return tmp;
    }


    bool operator==(const recursive_directory_iterator& a, const recursive_directory_iterator& b) noexcept  { return a.m_it == b.m_it; }


    bool operator!=(const recursive_directory_iterator& a, const recursive_directory_iterator& b) noexcept  { return !(a == b); }


    recursive_directory_iterator recursive_directory_iterator::begin() const noexcept                       { return *this; }


    recursive_directory_iterator recursive_directory_iterator::end() const noexcept                         { return recursive_directory_iterator(); }


    recursive_directory_iterator& recursive_directory_iterator::increment(std::error_code& error) noexcept {

        if (m_it == std::filesystem::end(m_it)) {

            error.clear();
            return *this;
        }

        ++m_it;
        if (m_it != std::filesystem::end(m_it))
            m_current = directory_entry(m_it->path());
        else
            m_current = directory_entry{};

        error.clear();
        return *this;
    }


    void recursive_directory_iterator::pop()                                                                { m_it.pop(); }


    int recursive_directory_iterator::depth() const noexcept                                                { return m_it.depth(); }


    void recursive_directory_iterator::disable_recursion_pending() noexcept                                 { m_it.disable_recursion_pending(); }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    std::error_code& recursive_directory_iterator::default_error_code() noexcept {

        static std::error_code dummy;
        return dummy;
    }

}
