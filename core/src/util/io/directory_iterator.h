
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::vfs {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Recursive directory iterator – similar interface but with extra options
    enum class directory_options : u8 {

        none                                            = 0,
        follow_directory_symlink                        = BIT(0),
        skip_permission_denied                          = BIT(1),
    };


    inline directory_options operator|(directory_options a, directory_options b) noexcept {

        return static_cast<directory_options>(static_cast<u8>(a) | static_cast<u8>(b));
    }


    inline bool operator&(directory_options a, directory_options b) noexcept {

        return (static_cast<u8>(a) & static_cast<u8>(b)) != 0;
    }


    // A lightweight entry returned by directory iterators
    struct directory_entry {
    public:

        directory_entry() = default;

        explicit directory_entry(std::filesystem::path path);

        const std::filesystem::path& path() const noexcept;

        // Convenience: forward to the global VFS functions
        bool exists(std::error_code& error) const noexcept;

        bool is_directory(std::error_code& error) const noexcept;

        bool is_regular_file(std::error_code& error) const noexcept;

        u64 file_size(std::error_code& error) const noexcept;

    private:

        std::filesystem::path                           m_path;

    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class directory_iterator {
    public:

        // Iterator traits (public helper)
        using difference_type                           = std::ptrdiff_t;
        using valueType                                 = directory_entry;
        using pointer                                   = const directory_entry*;
        using reference                                 = const directory_entry&;
        using iteratorCategory                          = std::input_iterator_tag;

        // Constructors
        directory_iterator() noexcept = default;                         // end iterator
        explicit directory_iterator(const std::filesystem::path& p, std::error_code& error) noexcept;     // start iteration

        // Copy / move
        directory_iterator(const directory_iterator& other) = default;
        directory_iterator(directory_iterator&& other) noexcept = default;
        directory_iterator& operator=(const directory_iterator& other) = default;
        directory_iterator& operator=(directory_iterator&& other) noexcept = default;
        ~directory_iterator() = default;

        // Dereference
        reference operator*() const noexcept;
        pointer operator->() const noexcept;
        directory_iterator& operator++() noexcept;                // prefix
        directory_iterator operator++(int) noexcept;              // postfix

        // For range-based for support
        directory_iterator begin() const noexcept;
        directory_iterator end() const noexcept;
        directory_iterator& increment(std::error_code& error) noexcept;

        // Comparison (only end vs non‑end)
        friend bool operator==(const directory_iterator& a, const directory_iterator& b) noexcept;
        friend bool operator!=(const directory_iterator& a, const directory_iterator& b) noexcept;

    private:

        std::filesystem::directory_iterator             m_it;
        directory_entry                                 m_current;

    };


    
    class recursive_directory_iterator {
    public:

        using difference_type                           = std::ptrdiff_t;
        using valueType                                 = directory_entry;
        using pointer                                   = const directory_entry*;
        using reference                                 = const directory_entry&;
        using iteratorCategory                          = std::input_iterator_tag;

        recursive_directory_iterator() noexcept = default;

        explicit recursive_directory_iterator(const std::filesystem::path& path,
            directory_options opts = directory_options::skip_permission_denied, std::error_code& error = default_error_code()) noexcept;

        recursive_directory_iterator(const recursive_directory_iterator&) = default;
        recursive_directory_iterator(recursive_directory_iterator&&) noexcept = default;
        recursive_directory_iterator& operator=(const recursive_directory_iterator&) = default;
        recursive_directory_iterator& operator=(recursive_directory_iterator&&) noexcept = default;

        reference operator*() const noexcept;
        pointer   operator->() const noexcept;
        recursive_directory_iterator& operator++() noexcept;
        recursive_directory_iterator  operator++(int) noexcept;

        recursive_directory_iterator begin() const noexcept;
        recursive_directory_iterator end() const noexcept;
        recursive_directory_iterator& increment(std::error_code& error) noexcept;

        // Recursion control
        void pop();                                   // go up one level
        int  depth() const noexcept;                  // current recursion depth
        void disable_recursion_pending() noexcept;

        friend bool operator==(const recursive_directory_iterator& a, const recursive_directory_iterator& b) noexcept;
        friend bool operator!=(const recursive_directory_iterator& a, const recursive_directory_iterator& b) noexcept;

    private:

        static std::error_code& default_error_code() noexcept;

        std::filesystem::recursive_directory_iterator   m_it;
        directory_entry                                 m_current;

    };
}
