#include "config_section.h"
#include "ini.h"

#include <cstring>
#include <iostream>
#include <algorithm>
#include <regex>

namespace xxx {

const std::regex config_key::pattern("^([a-zA-z_]+[^\\.]*\\.)?([0-9]+\\.)?([^\\.]+)$");

int config_key::precision_score() const
{
  //
  //  3    prod.100.key            HIGHEST
  //  2                 100.key
  //  1        prod.key
  //  0                     key    LOWEST
  return (instid? 2 : 0) + (env? 1 : 0);
}

std::ostream& operator<<(std::ostream& os, const config_key& k){
  os << k.to_string();
  return os;
}

std::string config_key::to_string() const
{
  std::string rv;
  if (env) {
    rv += env.value();
    rv += ".";
  }
  if (instid) {
    rv += std::to_string(instid.value());
    rv += ".";
  }
  rv += name;
  return rv;
}


config_key config_key::parse(const std::string& s)
{
  config_key rv;

  std::smatch m;

  if (std::regex_match(s, m, config_key::pattern)) {
    if (m[1].matched) {
      std::string tmp = m[1];
      rv.env = tmp.substr(0, tmp.size()-1);
    }
    if (m[2].matched) {
      std::string tmp = m[2];
      rv.instid = std::stoi(tmp.substr(0, tmp.size()-1));
    }
    if (m[3].matched)
      rv.name = m[3];
    else
      throw config_error("config key missing name segment");
  }
  else
    throw config_error("config key has invalid format");

  return rv;
}

struct ini_handler_context {
  config_section * root;
  std::string env;
  int instance;
};


static int ini_handler(void* user, const char* section, const char* name,
                       const char* value)
{
  // std::cout << "section: " << section << ", "
  //           << "name: " << name << ", "
  //           << "value: " << value << ", "
  //           << std::endl;

  ini_handler_context* context = (ini_handler_context*) user;
  config_section* root_section = context->root;

  config_section* parent = root_section;

  config_section* confsection = nullptr;

  if (*section == '\0')
    confsection = root_section;
  else
  {
    try {
      confsection = & parent->get_last_section(section);
    }
    catch (config_error&)
    {
      parent->add({section});
      confsection = & parent->get_last_section(section);
    }
  }

  try {
    config_key key = config_key::parse(name);

    if (key.env && context->env != key.env.value())
      return 1;

    if (key.instid && context->instance != key.instid.value())
      return 1;

    confsection->add(std::move(key), value);
  }
  catch (const config_error& e) {
    std::ostringstream os;
    os << "config parse failed for key=["<<name<<"] value=["<<value<<"] : " << e.what();
    throw config_error(os.str());
  }

  return 1; /* success */
}

config_error::config_error(std::string error)
  : std::runtime_error(std::move(error))
{}


static void auto_key(ini_handler_context& context,
                        std::string name,
                        std::string value)
{
  config_section* cfg = context.root;
  if (!cfg->has_key(name)){
    config_key key {context.env, context.instance, name};
    cfg->add({key}, std::move(value));
  }
  else {
    std::ostringstream os;
    os << "cannot provide auto key '"<<name<<"' because is already defined; remove definition from config file";
    throw config_error(os.str());
  }
}

config_section config_section::parse_ini_file(const std::string& filename,
                                              const std::string& env,
                                              int instance)
{
  config_section cfg("root");

  if (env.empty())
    throw config_error("env cannot be empty");

  ini_handler_context context;
  context.root = &cfg;
  context.env = env;
  context.instance=instance;

  if (context.env.empty())
    throw config_error("config environment cannot be empty");

  if (ini_parse(filename.c_str(), ini_handler, &context) < 0)
    throw std::runtime_error("cannot parse config file '" + filename + "'");

  auto_key(context, "env", context.env);
  auto_key(context, "instance", std::to_string(context.instance));
  return cfg;
}

config_section::config_section(std::string name)
  : m_name(std::move(name))
{
}

bool config_section::has_key(const std::string& name) const
{
  return m_items.find(name) != end(m_items);
}

bool config_section::has_section(const std::string& name) const {
  for (auto&item : m_sections)
    if (item.m_name == name)
      return true;
  return false;
}

config_section& config_section::get_first_section(const std::string& name) {
  for (auto&item : m_sections)
    if (item.m_name == name)
      return item;
  throw config_error("configuration section not found '"+name+"'");
}

config_section& config_section::get_last_section(const std::string& name) {
  for (size_t i = m_sections.size(); i > 0; i--)
  {
    auto&item = m_sections[i-1];
    if (item.m_name == name)
      return item;
  }
  throw config_error("configuration section not found '"+name+"'");
}

void config_section::add(config_key key, std::string value)
{
  auto iter = m_items.find(key.name);

  if (iter == m_items.end()) {
    m_items[key.name] = {std::move(key), std::move(value)};
  }
  else
  {
    const int exist_score = iter->second.key.precision_score();
    if (key.precision_score() > exist_score) {
      iter->second.key = std::move(key);
      iter->second.value = std::move(value);
    }
    else if (key.precision_score() == exist_score)
      throw config_error("key already exists");
  }

}

void config_section::add(config_section cs)
{
  m_sections.push_back( std::move(cs) );
}

wampcc::json_value config_section::to_json() const
{
  wampcc::json_object nvpairs;
  for (auto & item : m_items)
    nvpairs[item.second.key.to_string()] = item.second.value;

  wampcc::json_array subsections;
  for (auto& item : m_sections)
    subsections.push_back( item.to_json() );

  wampcc::json_array container {
    name(),
      wampcc::json_value(nvpairs),
      std::move(subsections)
      };

  return container;
}


static bool str_to_bool(const std::string& s)
{
  if (strcasecmp(s.c_str(), "true") == 0)
    return true;
  else if (strcasecmp(s.c_str(),"false") == 0)
    return false;
  else
    throw config_error("invalid boolean value, '"+s+"'");
}


bool config_section::get_as_bool(const std::string& name) const
{
  auto iter = m_items.find(name);
  if (iter != end(m_items))
    return str_to_bool(iter->second.value);
  else
    throw config_error("configuration item not found '"+name+"'");
}


bool config_section::get_as_bool(const std::string& name, bool default_value) const
{
  auto iter = m_items.find(name);
  if (iter != end(m_items))
    return str_to_bool(iter->second.value);
  else
    return default_value;
}


int config_section::get_as_int(const std::string& name) const
{
  auto iter = m_items.find(name);
  if (iter != end(m_items))
    return std::stoi(iter->second.value);
  else
    throw config_error("configuration item not found '"+name+"'");
}

int config_section::get_as_int(const std::string& name, int default_value) const
{
  auto iter = m_items.find(name);
  if (iter != end(m_items))
    return std::stoi(iter->second.value);
  else
    return default_value;
}

const std::string& config_section::get_as_string(const std::string& name) const
{
  auto iter = m_items.find(name);
  if (iter != end(m_items))
    return iter->second.value;
  else
    throw config_error("configuration item not found '"+name+"'");
}

std::string config_section::get_as_string(const std::string& name, const std::string& default_value) const
{
  auto iter = m_items.find(name);
  if (iter != end(m_items))
    return iter->second.value;
  else
    return default_value;
}


std::vector<std::string> config_section::section_names() const
{
  std::vector<std::string> rv;

  std::for_each(begin(m_sections), end(m_sections),
                [&rv](const decltype(m_sections)::value_type& v){
                  rv.push_back(v.name());
                });

  return rv;
}


std::vector<config_section> config_section::sections() const
{
  std::vector<config_section> rv;

  std::for_each(begin(m_sections), end(m_sections),
                [&rv](const decltype(m_sections)::value_type& v){
                  rv.push_back(v);
                });

  return rv;
}


} // namespace xxx
