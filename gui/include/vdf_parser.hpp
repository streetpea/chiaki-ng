// MIT License
//
// Copyright(c) 2016 Matthias Moeller
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef __TYTI_STEAM_VDF_PARSER_H__
#define __TYTI_STEAM_VDF_PARSER_H__

#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <exception>
#include <system_error>

// for wstring support
#include <locale>
#include <string>

// internal
#include <stack>

// VS < 2015 has only partial C++11 support
#if defined(_MSC_VER) && _MSC_VER < 1900
#ifndef CONSTEXPR
#define CONSTEXPR
#endif

#ifndef NOEXCEPT
#define NOEXCEPT
#endif
#else
#ifndef CONSTEXPR
#define CONSTEXPR constexpr
#define TYTI_UNDEF_CONSTEXPR
#endif

#ifndef NOEXCEPT
#define NOEXCEPT noexcept
#define TYTI_UNDEF_NOEXCEPT
#endif

#endif


namespace tyti
{
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args)
    {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
namespace vdf
{
namespace detail
{
///////////////////////////////////////////////////////////////////////////
//  Helper functions selecting the right encoding (char/wchar_T)
///////////////////////////////////////////////////////////////////////////

template <typename T> struct literal_macro_help
{
    static CONSTEXPR const char *result(const char *c, const wchar_t *) NOEXCEPT
    {
        return c;
    }
    static CONSTEXPR const char result(const char c, const wchar_t) NOEXCEPT
    {
        return c;
    }
};

template <> struct literal_macro_help<wchar_t>
{
    static CONSTEXPR const wchar_t *result(const char *,
                                           const wchar_t *wc) NOEXCEPT
    {
        return wc;
    }
    static CONSTEXPR const wchar_t result(const char, const wchar_t wc) NOEXCEPT
    {
        return wc;
    }
};
#define TYTI_L(type, text)                                                     \
    vdf::detail::literal_macro_help<type>::result(text, L##text)

inline std::string string_converter(const std::string &w) NOEXCEPT { return w; }

// utility wrapper to adapt locale-bound facets for wstring/wbuffer convert
// from cppreference
template <class Facet> struct deletable_facet : Facet
{
    template <class... Args>
    deletable_facet(Args &&...args) : Facet(std::forward<Args>(args)...)
    {
    }
    ~deletable_facet() {}
};

inline std::string
string_converter(const std::wstring &w) // todo: use us-locale
{
    std::wstring_convert<
        deletable_facet<std::codecvt<wchar_t, char, std::mbstate_t>>>
        conv1;
    return conv1.to_bytes(w);
}

///////////////////////////////////////////////////////////////////////////
//  Writer helper functions
///////////////////////////////////////////////////////////////////////////

template <typename charT> class tabs
{
    const size_t t;

  public:
    explicit CONSTEXPR tabs(size_t i) NOEXCEPT : t(i) {}
    std::basic_string<charT> print() const
    {
        return std::basic_string<charT>(t, TYTI_L(charT, '\t'));
    }
    inline CONSTEXPR tabs operator+(size_t i) const NOEXCEPT
    {
        return tabs(t + i);
    }
};

template <typename oStreamT>
oStreamT &operator<<(oStreamT &s, const tabs<typename oStreamT::char_type> t)
{
    s << t.print();
    return s;
}
} // end namespace detail

///////////////////////////////////////////////////////////////////////////
//  Interface
///////////////////////////////////////////////////////////////////////////

/// custom objects and their corresponding write functions

/// basic object node. Every object has a name and can contains attributes saved
/// as key_value pairs or childrens
template <typename CharT> struct basic_object
{
    typedef CharT char_type;
    std::basic_string<char_type> name;
    std::unordered_map<std::basic_string<char_type>,
                       std::basic_string<char_type>>
        attribs;
    std::unordered_map<std::basic_string<char_type>,
                       std::shared_ptr<basic_object<char_type>>>
        childs;

    void add_attribute(std::basic_string<char_type> key,
                       std::basic_string<char_type> value)
    {
        attribs.emplace(std::move(key), std::move(value));
    }
    void add_child(std::unique_ptr<basic_object<char_type>> child)
    {
        std::shared_ptr<basic_object<char_type>> obj{child.release()};
        childs.emplace(obj->name, obj);
    }
    void set_name(std::basic_string<char_type> n) { name = std::move(n); }
};

template <typename CharT> struct basic_multikey_object
{
    typedef CharT char_type;
    std::basic_string<char_type> name;
    std::unordered_multimap<std::basic_string<char_type>,
                            std::basic_string<char_type>>
        attribs;
    std::unordered_multimap<std::basic_string<char_type>,
                            std::shared_ptr<basic_multikey_object<char_type>>>
        childs;

    void add_attribute(std::basic_string<char_type> key,
                       std::basic_string<char_type> value)
    {
        attribs.emplace(std::move(key), std::move(value));
    }
    void add_child(std::unique_ptr<basic_multikey_object<char_type>> child)
    {
        std::shared_ptr<basic_multikey_object<char_type>> obj{child.release()};
        childs.emplace(obj->name, obj);
    }
    void set_name(std::basic_string<char_type> n) { name = std::move(n); }
};

typedef basic_object<char> object;
typedef basic_object<wchar_t> wobject;
typedef basic_multikey_object<char> multikey_object;
typedef basic_multikey_object<wchar_t> wmultikey_object;

struct Options
{
    bool strip_escape_symbols;
    bool ignore_all_platform_conditionals;
    bool ignore_includes;

    Options()
        : strip_escape_symbols(true), ignore_all_platform_conditionals(false),
          ignore_includes(false)
    {
    }
};

// forward decls
// forward decl
template <typename OutputT, typename iStreamT>
OutputT read(iStreamT &inStream, const Options &opt = Options{});

/** \brief writes given object tree in vdf format to given stream.
Output is prettyfied, using tabs
*/
template <typename oStreamT, typename T>
void write(oStreamT &s, const T &r,
           const detail::tabs<typename oStreamT::char_type> tab =
               detail::tabs<typename oStreamT::char_type>(0))
{
    typedef typename oStreamT::char_type charT;
    using namespace detail;
    s << tab << TYTI_L(charT, '"') << r.name << TYTI_L(charT, "\"\n") << tab
      << TYTI_L(charT, "{\n");
    for (const auto &i : r.attribs)
        s << tab + 1 << TYTI_L(charT, '"') << i.first
          << TYTI_L(charT, "\"\t\t\"") << i.second << TYTI_L(charT, "\"\n");
    for (const auto &i : r.childs)
        if (i.second)
            write(s, *i.second, tab + 1);
    s << tab << TYTI_L(charT, "}\n");
}

namespace detail
{
template <typename iStreamT>
std::basic_string<typename iStreamT::char_type> read_file(iStreamT &inStream)
{
    // cache the file
    typedef typename iStreamT::char_type charT;
    std::basic_string<charT> str;
    inStream.seekg(0, std::ios::end);
    str.resize(static_cast<size_t>(inStream.tellg()));
    if (str.empty())
        return str;

    inStream.seekg(0, std::ios::beg);
    inStream.read(&str[0], str.size());
    return str;
}

/** \brief Read VDF formatted sequences defined by the range [first, last).
If the file is mailformatted, parser will try to read it until it can.
@param first            begin iterator
@param end              end iterator
@param exclude_files    list of files which cant be included anymore.
                        prevents circular includes

can thow:
        - "std::runtime_error" if a parsing error occured
        - "std::bad_alloc" if not enough memory coup be allocated
*/
template <typename OutputT, typename IterT>
std::vector<std::unique_ptr<OutputT>> read_internal(
    IterT first, const IterT last,
    std::unordered_set<
        std::basic_string<typename std::iterator_traits<IterT>::value_type>>
        &exclude_files,
    const Options &opt)
{
    static_assert(std::is_default_constructible<OutputT>::value,
                  "Output Type must be default constructible (provide "
                  "constructor without arguments)");
    static_assert(std::is_move_constructible<OutputT>::value,
                  "Output Type must be move constructible");

    typedef typename std::iterator_traits<IterT>::value_type charT;

    const std::basic_string<charT> comment_end_str = TYTI_L(charT, "*/");
    const std::basic_string<charT> whitespaces = TYTI_L(charT, " \n\v\f\r\t");

#ifdef WIN32
    std::function<bool(const std::basic_string<charT> &)> is_platform_str =
        [](const std::basic_string<charT> &in)
    {
        return in == TYTI_L(charT, "$WIN32") || in == TYTI_L(charT, "$WINDOWS");
    };
#elif __APPLE__
    // WIN32 stands for pc in general
    std::function<bool(const std::basic_string<charT> &)> is_platform_str =
        [](const std::basic_string<charT> &in)
    {
        return in == TYTI_L(charT, "$WIN32") || in == TYTI_L(charT, "$POSIX") ||
               in == TYTI_L(charT, "$OSX");
    };

#elif __linux__
    // WIN32 stands for pc in general
    std::function<bool(const std::basic_string<charT> &)> is_platform_str =
        [](const std::basic_string<charT> &in)
    {
        return in == TYTI_L(charT, "$WIN32") || in == TYTI_L(charT, "$POSIX") ||
               in == TYTI_L(charT, "$LINUX");
    };
#else
    std::function<bool(const std::basic_string<charT> &)> is_platform_str =
        [](const std::basic_string<charT> &in) { return false; };
#endif

    if (opt.ignore_all_platform_conditionals)
        is_platform_str = [](const std::basic_string<charT> &)
        { return false; };

    // function for skipping a comment block
    // iter: iterator poition to the position after a '/'
    auto skip_comments = [&comment_end_str](IterT iter,
                                            const IterT &last) -> IterT
    {
        ++iter;
        if (iter == last)
            return last;

        if (*iter == TYTI_L(charT, '/'))
        {
            // line comment, skip whole line
            iter = std::find(iter + 1, last, TYTI_L(charT, '\n'));
            if (iter == last)
                return last;
        }

        if (*iter == '*')
        {
            // block comment, skip until next occurance of "*\"
            iter = std::search(iter + 1, last, std::begin(comment_end_str),
                               std::end(comment_end_str));
            if (std::distance(iter, last) <= 2)
                return last;
            iter += 2;
        }

        return iter;
    };

    auto end_quote = [](IterT iter, const IterT &last) -> IterT
    {
        const auto begin = iter;
        auto last_esc = iter;
        if (iter == last)
            throw std::runtime_error{"quote was opened but not closed."};
        do
        {
            ++iter;
            iter = std::find(iter, last, TYTI_L(charT, '\"'));
            if (iter == last)
                break;

            last_esc = std::prev(iter);
            while (last_esc != begin && *last_esc == '\\')
                --last_esc;
        } while (!(std::distance(last_esc, iter) % 2) && iter != last);
        if (iter == last)
            throw std::runtime_error{"quote was opened but not closed."};
        return iter;
    };

    auto end_word = [&whitespaces](IterT iter, const IterT &last) -> IterT
    {
        const auto begin = iter;
        auto last_esc = iter;
        if (iter == last)
            throw std::runtime_error{"quote was opened but not closed."};
        do
        {
            ++iter;
            iter = std::find_first_of(iter, last, std::begin(whitespaces),
                                      std::end(whitespaces));
            if (iter == last)
                break;

            last_esc = std::prev(iter);
            while (last_esc != begin && *last_esc == '\\')
                --last_esc;
        } while (!(std::distance(last_esc, iter) % 2) && iter != last);
        if (iter == last)
            throw std::runtime_error{"word wasnt properly ended"};
        return iter;
    };

    auto skip_whitespaces = [&whitespaces](IterT iter,
                                           const IterT &last) -> IterT
    {
        if (iter == last)
            return iter;
        iter = std::find_if_not(iter, last,
                                [&whitespaces](charT c)
                                {
                                    // return true if whitespace
                                    return std::any_of(std::begin(whitespaces),
                                                       std::end(whitespaces),
                                                       [c](charT pc)
                                                       { return pc == c; });
                                });
        return iter;
    };

    std::function<void(std::basic_string<charT> &)> strip_escape_symbols =
        [](std::basic_string<charT> &s)
    {
        auto quote_searcher = [&s](size_t pos)
        { return s.find(TYTI_L(charT, "\\\""), pos); };
        auto p = quote_searcher(0);
        while (p != s.npos)
        {
            s.replace(p, 2, TYTI_L(charT, "\""));
            p = quote_searcher(p);
        }
        auto searcher = [&s](size_t pos)
        { return s.find(TYTI_L(charT, "\\\\"), pos); };
        p = searcher(0);
        while (p != s.npos)
        {
            s.replace(p, 2, TYTI_L(charT, "\\"));
            p = searcher(p);
        }
    };

    if (!opt.strip_escape_symbols)
        strip_escape_symbols = [](std::basic_string<charT> &) {};

    auto conditional_fullfilled =
        [&skip_whitespaces, &is_platform_str](IterT &iter, const IterT &last)
    {
        iter = skip_whitespaces(iter, last);
        if (iter == last)
            return true;
        if (*iter == '[')
        {
            ++iter;
            if (iter == last)
                throw std::runtime_error("conditional not closed");
            const auto end = std::find(iter, last, ']');
            if (end == last)
                throw std::runtime_error("conditional not closed");
            const bool negate = *iter == '!';
            if (negate)
                ++iter;
            auto conditional = std::basic_string<charT>(iter, end);

            const bool is_platform = is_platform_str(conditional);
            iter = end + 1;

            return static_cast<bool>(is_platform ^ negate);
        }
        return true;
    };

    // read header
    //  first, quoted name
    std::unique_ptr<OutputT> curObj = nullptr;
    std::vector<std::unique_ptr<OutputT>> roots;
    std::stack<std::unique_ptr<OutputT>> lvls;
    auto curIter = first;

    while (curIter != last && *curIter != '\0')
    {
        //  find first starting attrib/child, or ending
        curIter = skip_whitespaces(curIter, last);
        if (curIter == last || *curIter == '\0')
            break;
        if (*curIter == TYTI_L(charT, '/'))
        {
            curIter = skip_comments(curIter, last);
            if (curIter == last || *curIter == '\0')
                throw std::runtime_error("Unexpected eof");
        }
        else if (*curIter != TYTI_L(charT, '}'))
        {
            // get key
            const auto keyEnd = (*curIter == TYTI_L(charT, '\"'))
                                    ? end_quote(curIter, last)
                                    : end_word(curIter, last);
            if (*curIter == TYTI_L(charT, '\"'))
                ++curIter;
            std::basic_string<charT> key(curIter, keyEnd);
            strip_escape_symbols(key);
            curIter = keyEnd + ((*keyEnd == TYTI_L(charT, '\"')) ? 1 : 0);
            if (curIter == last)
                throw std::runtime_error{"key opened, but never closed"};

            curIter = skip_whitespaces(curIter, last);

            auto conditional = conditional_fullfilled(curIter, last);
            if (!conditional)
                continue;
            if (curIter == last)
                throw std::runtime_error{"key declared, but no value"};

            while (*curIter == TYTI_L(charT, '/'))
            {

                curIter = skip_comments(curIter, last);
                if (curIter == last || *curIter == '}')
                    throw std::runtime_error{"key declared, but no value"};
                curIter = skip_whitespaces(curIter, last);
                if (curIter == last || *curIter == '}')
                    throw std::runtime_error{"key declared, but no value"};
            }
            // get value
            if (*curIter != '{')
            {
                if (curIter == last)
                    throw std::runtime_error{"key declared, but no value"};
                const auto valueEnd = (*curIter == TYTI_L(charT, '\"'))
                                          ? end_quote(curIter, last)
                                          : end_word(curIter, last);
                if (valueEnd == last)
                    throw std::runtime_error("No closed word");
                if (*curIter == TYTI_L(charT, '\"'))
                    ++curIter;
                if (curIter == last)
                    throw std::runtime_error("No closed word");

                auto value = std::basic_string<charT>(curIter, valueEnd);
                strip_escape_symbols(value);
                curIter =
                    valueEnd + ((*valueEnd == TYTI_L(charT, '\"')) ? 1 : 0);

                auto conditional = conditional_fullfilled(curIter, last);
                if (!conditional)
                    continue;

                // process value
                if (key != TYTI_L(charT, "#include") &&
                    key != TYTI_L(charT, "#base"))
                {
                    if (curObj)
                    {
                        curObj->add_attribute(std::move(key), std::move(value));
                    }
                    else
                    {
                        throw std::runtime_error{
                            "unexpected key without object"};
                    }
                }
                else
                {
                    if (!opt.ignore_includes &&
                        exclude_files.find(value) == exclude_files.end())
                    {
                        exclude_files.insert(value);
                        std::basic_ifstream<charT> i(
                            detail::string_converter(value));
                        auto str = read_file(i);
                        auto file_objs = read_internal<OutputT>(
                            str.begin(), str.end(), exclude_files, opt);
                        for (auto &n : file_objs)
                        {
                            if (curObj)
                                curObj->add_child(std::move(n));
                            else
                                roots.push_back(std::move(n));
                        }
                        exclude_files.erase(value);
                    }
                }
            }
            else if (*curIter == '{')
            {
                if (curObj)
                    lvls.push(std::move(curObj));
                curObj = make_unique<OutputT>();
                curObj->set_name(std::move(key));
                ++curIter;
            }
        }
        // end of new object
        else if (curObj && *curIter == TYTI_L(charT, '}'))
        {
            if (!lvls.empty())
            {
                // get object before
                std::unique_ptr<OutputT> prev{std::move(lvls.top())};
                lvls.pop();

                // add finished obj to obj before and release it from processing
                prev->add_child(std::move(curObj));
                curObj = std::move(prev);
            }
            else
            {
                roots.push_back(std::move(curObj));
                curObj.reset();
            }
            ++curIter;
        }
        else
        {
            throw std::runtime_error{"unexpected '}'"};
        }
    }
    if (curObj != nullptr || !lvls.empty())
    {
        throw std::runtime_error{"object is not closed with '}'"};
    }

    return roots;
}

} // namespace detail

/** \brief Read VDF formatted sequences defined by the range [first, last).
If the file is mailformatted, parser will try to read it until it can.
@param first begin iterator
@param end end iterator

can thow:
        - "std::runtime_error" if a parsing error occured
        - "std::bad_alloc" if not enough memory coup be allocated
*/
template <typename OutputT, typename IterT>
OutputT read(IterT first, const IterT last, const Options &opt = Options{})
{
    auto exclude_files = std::unordered_set<
        std::basic_string<typename std::iterator_traits<IterT>::value_type>>{};
    auto roots =
        detail::read_internal<OutputT>(first, last, exclude_files, opt);

    OutputT result;
    if (roots.size() > 1)
    {
        for (auto &i : roots)
            result.add_child(std::move(i));
    }
    else if (roots.size() == 1)
        result = std::move(*roots[0]);

    return result;
}

/** \brief Read VDF formatted sequences defined by the range [first, last).
If the file is mailformatted, parser will try to read it until it can.
@param first begin iterator
@param end end iterator
@param ec output bool. 0 if ok, otherwise, holds an system error code

Possible error codes:
std::errc::protocol_error: file is mailformatted
std::errc::not_enough_memory: not enough space
std::errc::invalid_argument: iterators throws e.g. out of range
*/
template <typename OutputT, typename IterT>
OutputT read(IterT first, IterT last, std::error_code &ec,
             const Options &opt = Options{}) NOEXCEPT

{
    ec.clear();
    OutputT r{};
    try
    {
        r = read<OutputT>(first, last, opt);
    }
    catch (std::runtime_error &)
    {
        ec = std::make_error_code(std::errc::protocol_error);
    }
    catch (std::bad_alloc &)
    {
        ec = std::make_error_code(std::errc::not_enough_memory);
    }
    catch (...)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
    }
    return r;
}

/** \brief Read VDF formatted sequences defined by the range [first, last).
If the file is mailformatted, parser will try to read it until it can.
@param first begin iterator
@param end end iterator
@param ok output bool. true, if parser successed, false, if parser failed
*/
template <typename OutputT, typename IterT>
OutputT read(IterT first, const IterT last, bool *ok,
             const Options &opt = Options{}) NOEXCEPT
{
    std::error_code ec;
    auto r = read<OutputT>(first, last, ec, opt);
    if (ok)
        *ok = !ec;
    return r;
}

template <typename IterT>
inline auto read(IterT first, const IterT last, bool *ok,
                 const Options &opt = Options{}) NOEXCEPT
    ->basic_object<typename std::iterator_traits<IterT>::value_type>
{
    return read<basic_object<typename std::iterator_traits<IterT>::value_type>>(
        first, last, ok, opt);
}

template <typename IterT>
inline auto read(IterT first, IterT last, std::error_code &ec,
                 const Options &opt = Options{}) NOEXCEPT
    ->basic_object<typename std::iterator_traits<IterT>::value_type>
{
    return read<basic_object<typename std::iterator_traits<IterT>::value_type>>(
        first, last, ec, opt);
}

template <typename IterT>
inline auto read(IterT first, const IterT last, const Options &opt = Options{})
    -> basic_object<typename std::iterator_traits<IterT>::value_type>
{
    return read<basic_object<typename std::iterator_traits<IterT>::value_type>>(
        first, last, opt);
}

/** \brief Loads a stream (e.g. filestream) into the memory and parses the vdf
   formatted data. throws "std::bad_alloc" if file buffer could not be allocated
*/
template <typename OutputT, typename iStreamT>
OutputT read(iStreamT &inStream, std::error_code &ec,
             const Options &opt = Options{})
{
    // cache the file
    typedef typename iStreamT::char_type charT;
    std::basic_string<charT> str = detail::read_file(inStream);

    // parse it
    return read<OutputT>(str.begin(), str.end(), ec, opt);
}

template <typename iStreamT>
inline basic_object<typename iStreamT::char_type>
read(iStreamT &inStream, std::error_code &ec, const Options &opt = Options{})
{
    return read<basic_object<typename iStreamT::char_type>>(inStream, ec, opt);
}

/** \brief Loads a stream (e.g. filestream) into the memory and parses the vdf
   formatted data. throws "std::bad_alloc" if file buffer could not be allocated
    ok == false, if a parsing error occured
*/
template <typename OutputT, typename iStreamT>
OutputT read(iStreamT &inStream, bool *ok, const Options &opt = Options{})
{
    std::error_code ec;
    const auto r = read<OutputT>(inStream, ec, opt);
    if (ok)
        *ok = !ec;
    return r;
}

template <typename iStreamT>
inline basic_object<typename iStreamT::char_type>
read(iStreamT &inStream, bool *ok, const Options &opt = Options{})
{
    return read<basic_object<typename iStreamT::char_type>>(inStream, ok, opt);
}

/** \brief Loads a stream (e.g. filestream) into the memory and parses the vdf
   formatted data. throws "std::bad_alloc" if file buffer could not be allocated
    throws "std::runtime_error" if a parsing error occured
*/
template <typename OutputT, typename iStreamT>
OutputT read(iStreamT &inStream, const Options &opt)
{

    // cache the file
    typedef typename iStreamT::char_type charT;
    std::basic_string<charT> str = detail::read_file(inStream);
    // parse it
    return read<OutputT>(str.begin(), str.end(), opt);
}

template <typename iStreamT>
inline basic_object<typename iStreamT::char_type>
read(iStreamT &inStream, const Options &opt = Options{})
{
    return read<basic_object<typename iStreamT::char_type>>(inStream, opt);
}

} // namespace vdf
} // namespace tyti
#ifndef TYTI_NO_L_UNDEF
#undef TYTI_L
#endif

#ifdef TYTI_UNDEF_CONSTEXPR
#undef CONSTEXPR
#undef TYTI_NO_L_UNDEF
#endif

#ifdef TYTI_UNDEF_NOTHROW
#undef NOTHROW
#undef TYTI_UNDEF_NOTHROW
#endif

#endif //__TYTI_STEAM_VDF_PARSER_H__