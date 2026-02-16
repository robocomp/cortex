#pragma once

#include <iostream>
#include <tuple>
#include <utility>
#include <dsr/api/dsr_graph_settings.h>

// ANSI color codes
#define DSR_COLOR_RESET   "\033[0m"
#define DSR_COLOR_DEBUG   "\033[36m"   // Cyan
#define DSR_COLOR_INFO    "\033[32m"   // Green
#define DSR_COLOR_WARNING "\033[33m"   // Yellow
#define DSR_COLOR_ERROR   "\033[31m"   // Red

// Logging macros for use inside DSRGraph (where log_level is a member)
#define DSR_LOG_DEBUG(...)   do { if (log_level <= DSR::GraphSettings::LOGLEVEL::DEBUGL)   { std::cout << DSR_COLOR_DEBUG   "[DSR DEBUG] "   DSR_COLOR_RESET; DSR_LOG_PRINT(__VA_ARGS__); } } while(0)
#define DSR_LOG_INFO(...)    do { if (log_level <= DSR::GraphSettings::LOGLEVEL::INFOL)    { std::cout << DSR_COLOR_INFO    "[DSR INFO] "    DSR_COLOR_RESET; DSR_LOG_PRINT(__VA_ARGS__); } } while(0)
#define DSR_LOG_WARNING(...) do { if (log_level <= DSR::GraphSettings::LOGLEVEL::WARNINGL) { std::cout << DSR_COLOR_WARNING "[DSR WARNING] " DSR_COLOR_RESET; DSR_LOG_PRINT(__VA_ARGS__); } } while(0)
#define DSR_LOG_ERROR(...)   do { if (log_level <= DSR::GraphSettings::LOGLEVEL::ERRORL)   { std::cout << DSR_COLOR_ERROR   "[DSR ERROR] "   DSR_COLOR_RESET; DSR_LOG_PRINT(__VA_ARGS__); } } while(0)

// Logging macros with explicit log level parameter (for use outside DSRGraph, e.g. signal emitter)
#define DSR_LOG_DEBUG_L(lvl, ...)   do { if ((lvl) <= DSR::GraphSettings::LOGLEVEL::DEBUGL)   { std::cout << DSR_COLOR_DEBUG   "[DSR DEBUG] "   DSR_COLOR_RESET; DSR_LOG_PRINT(__VA_ARGS__); } } while(0)
#define DSR_LOG_INFO_L(lvl, ...)    do { if ((lvl) <= DSR::GraphSettings::LOGLEVEL::INFOL)    { std::cout << DSR_COLOR_INFO    "[DSR INFO] "    DSR_COLOR_RESET; DSR_LOG_PRINT(__VA_ARGS__); } } while(0)
#define DSR_LOG_WARNING_L(lvl, ...) do { if ((lvl) <= DSR::GraphSettings::LOGLEVEL::WARNINGL) { std::cout << DSR_COLOR_WARNING "[DSR WARNING] " DSR_COLOR_RESET; DSR_LOG_PRINT(__VA_ARGS__); } } while(0)

// Internal: print helpers for composite types
namespace dsr_log_detail {

template<typename T1, typename T2>
inline void print_value(std::ostream& os, const std::pair<T1, T2>& p) {
    os << '(' << p.first << ", " << p.second << ')';
}

template<typename... Ts>
inline void print_value(std::ostream& os, const std::tuple<Ts...>& t) {
    os << '(';
    std::apply([&os](const auto&... args) {
        size_t n = 0;
        ((os << (n++ ? ", " : "") << args), ...);
    }, t);
    os << ')';
}

inline void print_value(std::ostream& os, bool v) {
    os << (v ? "true" : "false");
}

template<typename T>
inline void print_value(std::ostream& os, const T& v) {
    os << v;
}

} // namespace dsr_log_detail

// Internal: print variadic args separated by spaces
inline void dsr_log_print_impl() { std::cout << std::endl; }

template<typename T, typename... Args>
inline void dsr_log_print_impl(const T& first, const Args&... rest) {
    dsr_log_detail::print_value(std::cout, first);
    if constexpr (sizeof...(rest) > 0) {
        std::cout << ' ';
        dsr_log_print_impl(rest...);
    } else {
        std::cout << std::endl;
    }
}

#define DSR_LOG_PRINT(...) dsr_log_print_impl(__VA_ARGS__)
