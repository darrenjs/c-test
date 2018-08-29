#include "utils.h"

#include <sys/time.h>
#include <time.h>

#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <sstream>
#include <string.h>
#include <assert.h>

namespace xxx
{

struct time_val
{
#ifndef _WIN32
  typedef long type_type;
#else
  typedef __time64_t type_type;
#endif
  type_type sec;  /* seconds */
  type_type usec; /* micros */
};


time_val time_now()
{
#ifndef _WIN32
  timeval epoch;
  gettimeofday(&epoch, nullptr);
  return {epoch.tv_sec, epoch.tv_usec};
#else
  SYSTEMTIME systime;
  GetSystemTime(&systime); // obtain milliseconds
  time_val::type_type now;
  time(&now); // seconds elapsed since midnight January 1, 1970
  time_val tv_systime{now, systime.wMilliseconds * 1000};

  /* C++11 chrono approach */
  /*
  auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
  time_val tv_chrono{ts / 1000000LL, ts % 1000000LL};
  */

  /* Windows FILETIME approach, has actual usec accuracy */
  /*
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  time_val::type_type tt = ft.dwHighDateTime;
  tt <<= 32;
  tt |= ft.dwLowDateTime;
  tt /= 10;
  tt -= 11644473600000000ULL;
  time_val tv_filetime{tt / 1000000LL, tt % 1000000LL};
  */

  return tv_systime;
#endif
}



std::string mandatory_getenv(const char * varname)
{
  const char* var = getenv(varname);
  if (!var) {
    std::ostringstream os;
    os << "environment variable '"<<varname<<"' not defined";
    throw std::runtime_error(os.str());
  }
  else
    return var;
}


std::string local_timestamp()
{
  static constexpr char format[] = "20170527-00:29:48.796000"; // 24

  char timestamp[32] = {0};
  struct tm parts;
  timeval epoch;
  gettimeofday(&epoch, nullptr);
  int ec;

  static_assert(sizeof timestamp > sizeof format, "buffer too short");

  localtime_r(&epoch.tv_sec, &parts);
  ec = snprintf(timestamp, sizeof(timestamp),
                "%02d%02d%02d-%02d:%02d:%02d.%06lu", parts.tm_year + 1900,
                parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min,
                parts.tm_sec, epoch.tv_usec);


  if (ec < 0)
    return "";

  timestamp[sizeof timestamp - 1] = '\0';
  return timestamp;
}

static bool is_valid_char(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || (c == '_');
}

/* Check URI conformance directly, rather than use regex.  This is to avoid
 * compilers with broken regex implementations (eg, some gcc 4.x). */
bool is_strict_uri(const char* p) noexcept
{
  enum class state {
    component,
    component_or_delim,
    fail
  } st = state::component;

  while (st != state::fail && *p) {
    switch (st) {
      case state::component: {
        if (is_valid_char(*p))
          st = state::component_or_delim;
        else
          st = state::fail;
        break;
      }
      case state::component_or_delim: {
        if (*p == '.')
          st = state::component;
        else if (!is_valid_char(*p))
          st = state::fail;
        break;
      }
      case state::fail:
        break;
    };
    p++;
  }

  return st == state::component_or_delim;
}


std::string compute_salted_password(const char* pwd,
                                    const char* salt,
                                    int iterations,
                                    int keylen)
{
  const char* hexalphabet = "0123456789abcdef";

  unsigned char * md = (unsigned char *) malloc(sizeof(unsigned char) * keylen);

  char dest[1000];
  memset(dest,0,sizeof(dest));

  if(PKCS5_PBKDF2_HMAC(pwd,  strlen(pwd),
                       (const unsigned char *)salt, strlen(salt),
                       iterations,
                       EVP_sha256(),
                       keylen, md) == 0)
    throw std::runtime_error("PKCS5_PBKDF2_HMAC failed");


   // TODO: convert to hex
  int i,j;
  for (i = 0, j=0; i < keylen; ++i, j+=2) {
    dest[j] = hexalphabet[(md[i] >> 4) & 0xF];
    dest[j + 1] = hexalphabet[md[i] & 0xF];
  }

  return std::string(dest, keylen*2);
}


/*
  Compute the HMAC-SHA256 using a secret over a message.

  On success, zero is returned.  On error, -1 is returned.
 */
int compute_HMACSHA256(const char* key, int keylen, const char* msg, int msglen,
                       char* dest, unsigned int* destlen,
                       HMACSHA256_Mode output_mode)
{
  const char* hexalphabet = "0123456789abcdef";
  const char* base64 =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  int retval = -1; /* success=0, fail=-1 */

  /* initialise HMAC context */
  HMAC_CTX ctx;
  HMAC_CTX_init(&ctx);

  unsigned char md[EVP_MAX_MD_SIZE + 1]; // EVP_MAX_MD_SIZE=64
  memset(md, 0, sizeof(md));
  unsigned int mdlen;

  HMAC(EVP_sha256(), key, keylen, (const unsigned char*)msg, msglen, md,
       &mdlen);

  if (output_mode == HMACSHA256_Mode::HEX) {
    // convert to hex representation in the output buffer
    if ((mdlen * 2) > *destlen) {
      // cannot encode
    } else {
      unsigned int i, j;
      for (i = 0, j = 0; i < mdlen; ++i, j += 2) {
        dest[j] = hexalphabet[(md[i] >> 4) & 0xF];
        dest[j + 1] = hexalphabet[md[i] & 0xF];
      }
      if (*destlen > (mdlen * 2) + 1) {
        dest[mdlen * 2] = '\0';
        *destlen = mdlen * 2 + 1;
      } else {
        *destlen = mdlen * 2;
      }
      retval = 0;
    }
  } else if (output_mode == HMACSHA256_Mode::BASE64) {
    /* Base 64 */
    unsigned int i = 0;
    int j = 0;
    const int jmax = *destlen;

    while (i < mdlen) {
      char t[3]; // we encode three bytes at a time
      t[0] = 0;
      t[1] = 0;
      t[2] = 0;

      unsigned int b = 0;
      for (b = 0; b < 3 && i < mdlen;) {
        if (i < mdlen)
          t[b] = md[i];
        b++;
        i++;
      }

      // b is now count of input bytes
      int idx[4];
      idx[0] = (t[0] & 0xFC) >> 2;
      idx[1] = ((t[0] & 0x3) << 4) | (t[1] >> 4 & 0xF);
      idx[2] = ((t[1] & 0xF) << 2) | ((t[2] & 0xC0) >> 6);
      idx[3] = (t[2] & 0x3F);

      switch (b) {
        case 1: {
          if (j < jmax) {
            dest[j] = base64[idx[0] & 0x3F];
            j++;
          }
          if (j < jmax) {
            dest[j] = base64[idx[1] & 0x3F];
            j++;
          }
          if (j < jmax) {
            dest[j] = '=';
            j++;
          }
          if (j < jmax) {
            dest[j] = '=';
            j++;
          }
          break;
        }
        case 2: {
          if (j < jmax) {
            dest[j] = base64[idx[0] & 0x3F];
            j++;
          }
          if (j < jmax) {
            dest[j] = base64[idx[1] & 0x3F];
            j++;
          }
          if (j < jmax) {
            dest[j] = base64[idx[2] & 0x3F];
            j++;
          }
          if (j < jmax) {
            dest[j] = '=';
            j++;
          }
          break;
        }
        case 3: {
          if (j < jmax) {
            dest[j] = base64[idx[0] & 0x3F];
            j++;
          }
          if (j < jmax) {
            dest[j] = base64[idx[1] & 0x3F];
            j++;
          }
          if (j < jmax) {
            dest[j] = base64[idx[2] & 0x3F];
            j++;
          }
          if (j < jmax) {
            dest[j] = base64[idx[3] & 0x3F];
            j++;
          }
          break;
        }
      }
    }
    if (j < jmax) {
      dest[j] = '\0';
      retval = 0;
      *destlen = j + 1;
    }
  }

  /* cleanup HMAC */
  HMAC_CTX_cleanup(&ctx);

  return retval;
}


std::string random_ascii_string(const size_t len, unsigned int seed)
{
  std::string temp(len, 'x'); //  gets overwritten below

  std::mt19937 engine(seed);
  std::uniform_int_distribution<> distr('!', '~'); // asci printables

  for (auto& x : temp)
    x = distr(engine);

  return temp;
}


std::string iso8601_utc_timestamp()
{
  static constexpr char full_format[] = "2017-05-21T07:51:17.000Z"; // 24
  static constexpr char short_format[] = "2017-05-21T07:51:17";     // 19
  static constexpr int short_len = 19;

  static_assert(short_len == (sizeof short_format - 1),
                "short_len check failed");

  time_val tv = time_now();

  char buf[32] = {0};
  assert(sizeof buf > (sizeof full_format));
  assert(sizeof full_format > sizeof short_format);

  struct tm parts;
  time_t rawtime = tv.sec;

#ifndef _WIN32
  gmtime_r(&rawtime, &parts);
#else
  gmtime_s(&parts, &rawtime);
#endif

  if (0 == strftime(buf, sizeof buf - 1, "%FT%T", &parts))
    return ""; // strftime not successful

  // append milliseconds
  int ec;
#ifndef _WIN32
  ec = snprintf(&buf[short_len], sizeof(buf) - short_len, ".%03dZ",
                (int)tv.usec / 1000);
#else
  ec = sprintf_s(&buf[short_len], sizeof(buf) - short_len, ".%03dZ",
                 (int)tv.usec / 1000);
#endif
  if (ec < 0)
    return "";

  buf[sizeof full_format - 1] = '\0';
  return buf;
}


/* Extract list of strings from a wamp json_array */
std::vector<std::string> strings(wampcc::json_array ja)
{
  std::vector<std::string> rv;

  for (auto & x : ja)
    if (x.is_string())
      rv.push_back(std::move(x.as_string()));

  return rv;

}


}
