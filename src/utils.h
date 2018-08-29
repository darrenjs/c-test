#ifndef XXX_UTILS_H
#define XXX_UTILS_H

#include <functional>
#include <random>

#include "wampcc/wampcc.h"

#define STRINGIZE2(s) #s
#define STRINGIFY(s) STRINGIZE2(s)

namespace xxx
{


/* Read environment variable, and throw exception if not defined */
std::string mandatory_getenv(const char * varname);

class global_scope_id_generator
{
public:
  static const uint64_t m_min = 0;
  static const uint64_t m_max = 9007199254740992ull;

  global_scope_id_generator() : m_next(0) {}

  uint64_t next()
  {
    if (m_next > m_max)
      m_next = 0;

    return m_next++;
  }

private:
  uint64_t m_next;
};

/** Return if the string is a wamp strict URI */
bool is_strict_uri(const char*) noexcept;

// replace with optional<> if C++17 present
template <typename T> struct maybe
{
  maybe() = default;

  maybe(T t)
    : m_value(std::move(t), true) {}

  maybe& operator=(T v)
  {
    m_value.first = std::move(v);
    m_value.second = true;
    return *this;
  }
  const T& value() const { return m_value.first; }
  T& value() { return m_value.first; }
  constexpr operator bool() const { return m_value.second; }
private:
  std::pair<T,bool> m_value;
};


class scope_guard
{
public:
  template <class Callable>
  scope_guard(Callable&& undo_func)
    : m_fn(std::forward<Callable>(undo_func))
  {
  }

  scope_guard(scope_guard&& other) : m_fn(std::move(other.m_fn))
  {
    other.m_fn = nullptr;
  }

  ~scope_guard()
  {
    if (m_fn)
      m_fn(); // must not throw
  }

  void dismiss() noexcept { m_fn = nullptr; }

  scope_guard(const scope_guard&) = delete;
  void operator=(const scope_guard&) = delete;

private:
  std::function<void()> m_fn;
};

  std::string local_timestamp();

enum class HMACSHA256_Mode { HEX, BASE64 };

int compute_HMACSHA256(const char* key, int keylen, const char* msg, int msglen,
                       char* dest, unsigned int* destlen,
                       HMACSHA256_Mode output_mode);

std::string compute_salted_password(const char* password,
                                    const char* salt,
                                    int iterations,
                                    int keylen);



/* Generate a random string of ascii printables of length 'len' */
std::string random_ascii_string(const size_t len,
                                unsigned int seed = std::random_device()());

/* Generate iso8601 timestamp, like YYYY-MM-DDThh:mm:ss.sssZ */
std::string iso8601_utc_timestamp();

/* Extract list of strings from a wamp json_array */
std::vector<std::string> strings(wampcc::json_array);

}

#endif
