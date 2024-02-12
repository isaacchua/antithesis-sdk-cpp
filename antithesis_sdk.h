#pragma once

#include <cstdio>
#include <cstdint>
#include <string>
#include <array>
#include <source_location>
#include <map>
#include <variant>
#include <sstream>
#include <iomanip>

namespace antithesis {
    struct AssertionState {
        uint8_t not_reached : 1;
        uint8_t false_not_seen : 1;
        uint8_t true_not_seen : 1;
        uint8_t rest : 5;

        AssertionState() : not_reached(true), false_not_seen(true), true_not_seen(true), rest(0)  {}
    };

    struct Details;

    typedef std::variant<std::string, double, Details> ValueType;

    struct Details : std::map<std::string, ValueType> {
        Details( std::initializer_list<std::pair<const std::string, ValueType>> args) : std::map<std::string, ValueType>(args) {}
    };

    static std::ostream& operator<<(std::ostream& out, const Details& details);
    static std::ostream& operator<<(std::ostream& out, const ValueType& value) {
        std::visit([&](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                out << std::quoted(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, Details>) {
                out << arg;
            } else {
                static_assert(false, "non-exhaustive visitor!");
            }
        }, value);

        return out;
    }

    static std::ostream& operator<<(std::ostream& out, const Details& details) {
        out << "{ ";

        bool first = true;
        for (auto [key, value] : details) {
            if (!first) {
                out << ", ";
            }
            out << std::quoted(key) << ": " << value;
            first = false;
        }

        out << " }";
        return out;
    }


    struct Assertion {
        AssertionState state;
        const char* message;
        const char* function_name;
        const char* file_name;
        const int line;
        std::string id;

        Assertion(const char* message, const char* function_name, const char* file_name, const int line) : 
            state(), message(message), function_name(function_name), file_name(file_name), line(line), id(std::string(message) + " in " + function_name) {
            this->add_to_catalog();
        }

        void add_to_catalog() const {
            printf("There is an assertion with ID `%s` at %s:%d in `%s` with message: `%s`\n", id.c_str(), file_name, line, function_name, message);
        }

        [[clang::always_inline]] inline void check_assertion(bool cond, const Details& details) {
            if (__builtin_expect(state.not_reached || state.false_not_seen || state.true_not_seen, false)) {
                check_assertion_internal(cond, details);
            }
        }

        private:
        void check_assertion_internal(bool cond, const Details& details) {
            if (state.not_reached) {
                printf("The assertion with ID `%s` was reached\n", id.c_str());
                print_details(details);
                state.not_reached = false;  // TODO: is the race OK?  If not, use a static initialization instead
            }

            if (!cond && state.false_not_seen) {
                printf("The assertion with ID `%s` saw its first false: %s\n", id.c_str(), message);
                print_details(details);
                state.false_not_seen = false;   // TODO: is the race OK?
            }

            if (cond && state.true_not_seen) {
                printf("The assertion with ID `%s` saw its first true: %s\n", id.c_str(), message);
                print_details(details);
                state.true_not_seen = false;   // TODO: is the race OK?
            }
        }

        void print_details(const Details& d) {
            std::ostringstream out;
            out << d;
            printf("Details: %s\n", out.str().c_str());
        }
    };


}

namespace {
    template <std::size_t N>
    struct fixed_string {
        std::array<char, N> contents;
        constexpr fixed_string() {
            for(int i=0; i<N; i++) contents[i] = 0;
        }
        constexpr fixed_string( const char (&arr)[N] )
        {
            for(int i=0; i<N; i++) contents[i] = arr[i];
        }
        static constexpr fixed_string from_c_str( const char* foo ) {
            fixed_string<N> it;
            for(int i=0; i<N && foo[i]; i++)
                it.contents[i] = foo[i];
            return it;
        }
        const char* c_str() const { return contents.data(); }
    };

    static constexpr size_t string_length( const char * s ) {
        for(int l = 0; ; l++)
            if (!s[l])
                return l;
    }

    template <fixed_string message, fixed_string file_name, fixed_string function_name, int line>
    struct CatalogEntry {
        [[clang::always_inline]] static inline antithesis::Assertion create() {
            return antithesis::Assertion(message.c_str(), function_name.c_str(), file_name.c_str(), line);
        }

        static inline antithesis::Assertion assertion = create();
    };
}

#define FIXED_STRING_FROM_C_STR(s) (fixed_string<string_length(s)+1>::from_c_str(s))

#define ANTITHESIS_ASSERT_RAW(cond, message, ...) ( \
    CatalogEntry< \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line() \
    >::assertion.check_assertion(cond, (antithesis::Details(__VA_ARGS__)) ) )

#define ALWAYS(cond, message, ...) ANTITHESIS_ASSERT_RAW(cond, message, __VA_ARGS__)
#define ALWAYS_OR_UNREACHABLE(cond, message, ...) ANTITHESIS_ASSERT_RAW(cond, message, __VA_ARGS__)
#define SOMETIMES(cond, message, ...) ANTITHESIS_ASSERT_RAW(cond, message, __VA_ARGS__)
#define REACHABLE(cond, message, ...) ANTITHESIS_ASSERT_RAW(cond, message, __VA_ARGS__)
#define UNREACHABLE(cond, message, ...) ANTITHESIS_ASSERT_RAW(cond, message, __VA_ARGS__)

