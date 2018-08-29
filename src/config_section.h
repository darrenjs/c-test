#ifndef XXX_CONFIG_SECTION_H
#define XXX_CONFIG_SECTION_H

#include "wampcc/json.h"

#include "utils.h"

#include <string>
#include <map>
#include <list>
#include <regex>


namespace xxx {


struct config_key {

  static const std::regex pattern;

  maybe<std::string> env;
  maybe<int> instid;
  std::string name;

  std::string to_string() const;

  static config_key parse(const std::string&);

  int precision_score() const;
};
std::ostream& operator<<(std::ostream&, const config_key&);

struct config_item
{
  config_key key;
  std::string value;
};

struct config_error : std::runtime_error
{
  config_error(std::string error);
};

class config_section
{
public:
  config_section() = default;
  config_section(std::string name);

  int get_as_int(const std::string&) const;
  int get_as_int(const std::string&, int default_value) const;

  bool get_as_bool(const std::string&) const;
  bool get_as_bool(const std::string&, bool default_value) const;

  const std::string& get_as_string(const std::string&) const;
  std::string get_as_string(const std::string&, const std::string& default_value) const;

  /** Return a list of the names of available sections */
  std::vector<std::string> section_names() const;

  /** Return the sections */
  std::vector<config_section> sections() const;


  bool has_section(const std::string& name) const;

  bool has_key(const std::string& name) const;

  config_section& get_first_section(const std::string& name);
  config_section& get_last_section(const std::string& name);

  /** Insert a name / value pair. Any existing pair will be overwritten.*/
  void add(config_key key, std::string value);

  /** Insert a subsection */
  void add(config_section);

  wampcc::json_value to_json() const;

  const std::string& name() const { return m_name; }

  // TODO: add env, instance
  static config_section parse_ini_file(const std::string& filename,
                                       const std::string& env,
                                       int instance);

private:

  std::string m_name;
  std::map<std::string, config_item> m_items;
  std::vector<config_section> m_sections;

};

} // namespace xxx

#endif
