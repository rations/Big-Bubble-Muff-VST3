// BigBubbleMuff — build tool: writes the LV2 bundle's manifest.ttl and
// bigbubblemuff.ttl, and refuses to write either if the port table disagrees with
// the plug-in.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// After the owner's rations-amp tools/rations_ttlgen.cpp. A hand-written TTL would
// be a second copy of every range, default and name; this instantiates the real
// controller, asks it, and writes down what it says. Before writing it checks:
//   * the controller declares exactly the kParams table plus the host bypass;
//   * each port's range, step count, default and name match the controller's;
//   * the bypass parameter carries kIsBypass (it becomes the lv2:enabled port);
//   * every lv2:symbol is unique.
//
//   bbm_ttlgen <bundle directory>
#include "lv2/bbmlv2.h"

#include "plugin/controller.h"
#include "plugin/ids.h"
#include "version.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace bbm;

namespace {

int gFailures = 0;

void fail(const std::string &what) {
  std::fprintf(stderr, "bbm_ttlgen: %s\n", what.c_str());
  ++gFailures;
}

std::string ascii(const Vst::TChar *s) {
  std::string out;
  for (int i = 0; s != nullptr && s[i] != 0 && i < 128; ++i)
    out.push_back(s[i] < 0x80 ? static_cast<char>(s[i]) : '?');
  return out;
}

// An lv2:symbol from a title: lower-case alphanumerics, anything else folded to '_'.
std::string symbolOf(const std::string &title) {
  std::string out;
  for (const char c : title) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
      out.push_back(c);
    else if (c >= 'A' && c <= 'Z')
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    else if (!out.empty() && out.back() != '_')
      out.push_back('_');
  }
  while (!out.empty() && out.back() == '_')
    out.pop_back();
  if (out.empty() || (out[0] >= '0' && out[0] <= '9'))
    out.insert(0, "p");
  return out;
}

// Shortest round-trip decimal, which Turtle reads as a number.
std::string number(double v) {
  std::array<char, 32> buf{};
  const auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
  return ec == std::errc() ? std::string(buf.data(), ptr) : std::string("0");
}

bool near(double a, double b) {
  return std::fabs(a - b) <= 1e-9;
}

struct Port {
  std::string symbol, name, unit;
  double min = 0.0, max = 1.0, def = 0.0;
  bool toggled = false;
};

std::vector<Port> collect(Controller &ctl) {
  std::vector<Port> ports;
  const int32 count = ctl.getParameterCount();
  if (count != kParamCount + 1)
    fail("the controller declares " + std::to_string(count) + " parameters; expected " +
         std::to_string(kParamCount + 1));

  for (const ParamSpec &spec : kParams) {
    Vst::Parameter *param = ctl.getParameterObject(spec.id);
    if (param == nullptr) {
      fail("parameter " + std::to_string(spec.id) + " is not declared");
      continue;
    }
    const Vst::ParameterInfo &info = param->getInfo();
    Port p;
    p.name = ascii(info.title);
    p.symbol = symbolOf(p.name);
    p.unit = ascii(info.units);
    p.min = spec.min;
    p.max = spec.max;
    p.def = spec.def;
    p.toggled = spec.kind == ParamKind::Toggle;
    if (p.name != spec.title)
      fail(p.symbol + ": title '" + p.name + "' differs from kParams '" + spec.title +
           "'");
    if (!near(param->toPlain(0.0), spec.min) || !near(param->toPlain(1.0), spec.max))
      fail(p.symbol + ": range [" + number(param->toPlain(0.0)) + ", " +
           number(param->toPlain(1.0)) + "] differs from kParams");
    if (info.stepCount != (p.toggled ? 1 : 0))
      fail(p.symbol + ": step count " + std::to_string(info.stepCount));
    if (!near(param->toPlain(info.defaultNormalizedValue), spec.def))
      fail(p.symbol + ": default " + number(param->toPlain(info.defaultNormalizedValue)) +
           " differs from kParams " + number(spec.def));
    if ((info.flags & Vst::ParameterInfo::kCanAutomate) == 0)
      fail(p.symbol + ": not automatable");
    ports.push_back(p);
  }

  Vst::Parameter *bypass = ctl.getParameterObject(kBypassId);
  if (bypass == nullptr || (bypass->getInfo().flags & Vst::ParameterInfo::kIsBypass) == 0)
    fail("no kIsBypass parameter at the bypass ID");

  std::set<std::string> symbols{"in", "out_l", "out_r", "enabled", "latency"};
  for (const Port &p : ports)
    if (!symbols.insert(p.symbol).second)
      fail("two ports share the symbol \"" + p.symbol + "\"");
  return ports;
}

void controlPort(std::ostream &o, const Port &p, std::uint32_t index) {
  o << "[\n\t\ta lv2:InputPort ,\n\t\t\tlv2:ControlPort ;\n"
    << "\t\tlv2:index " << index << " ;\n"
    << "\t\tlv2:symbol \"" << p.symbol << "\" ;\n"
    << "\t\tlv2:name \"" << p.name << "\" ;\n"
    << "\t\tlv2:default " << number(p.def) << " ;\n"
    << "\t\tlv2:minimum " << number(p.min) << " ;\n"
    << "\t\tlv2:maximum " << number(p.max);
  if (p.toggled)
    o << " ;\n\t\tlv2:portProperty lv2:toggled ,\n\t\t\tlv2:integer";
  if (p.unit == "dB")
    o << " ;\n\t\tunits:unit units:db";
  o << "\n\t]";
}

std::string pluginTtl(const std::vector<Port> &ports) {
  using namespace bbm::lv2;
  std::ostringstream o;
  o << "# GENERATED by tools/bbm_ttlgen.cpp from the plug-in's own controller. Do not "
       "edit.\n\n"
       "@prefix bufsz: <http://lv2plug.in/ns/ext/buf-size#> .\n"
       "@prefix doap:  <http://usefulinc.com/ns/doap#> .\n"
       "@prefix foaf:  <http://xmlns.com/foaf/0.1/> .\n"
       "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .\n"
       "@prefix opts:  <http://lv2plug.in/ns/ext/options#> .\n"
       "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .\n"
       "@prefix state: <http://lv2plug.in/ns/ext/state#> .\n"
       "@prefix ui:    <http://lv2plug.in/ns/extensions/ui#> .\n"
       "@prefix units: <http://lv2plug.in/ns/extensions/units#> .\n"
       "@prefix urid:  <http://lv2plug.in/ns/ext/urid#> .\n\n";

  // The UI. ui:idleInterface is required both ways: as a FEATURE because the editor
  // owns no thread and idle() is its run loop, and as EXTENSION DATA because that is
  // how the host gets the callback. Control inputs notify the UI by default, so no
  // ui:portNotification is needed. ui:resize is optional: the editor is resizable
  // and states its limits in the X size hints on its own window.
  o << "<" << kUiUri << ">\n\ta ui:X11UI ;\n"
    << "\tlv2:binary <" << kUiBinary << "> ;\n"
    << "\tlv2:requiredFeature ui:idleInterface ,\n\t\tui:parent ;\n"
    << "\tlv2:optionalFeature ui:resize ;\n"
    << "\tlv2:extensionData ui:idleInterface .\n\n";

  // The maintainer is stated on the plug-in (where hosts read it) and on the project
  // (where doap:maintainer's rdfs:domain puts it), as rations-amp found necessary.
  o << "<" << kPluginUri << ">\n\ta lv2:Plugin ,\n\t\t" << kPluginClass
    << " ,\n\t\tdoap:Project ;\n"
    << "\tdoap:name \"" << stringPluginName << "\" ;\n"
    << "\tdoap:license <http://opensource.org/licenses/MIT> ;\n"
    << "\tlv2:minorVersion " << kMinorVersion << " ;\n"
    << "\tlv2:microVersion " << kMicroVersion << " ;\n"
    << "\tdoap:maintainer [\n\t\ta foaf:Person ;\n"
    << "\t\tfoaf:name \"" << stringCompanyName << "\" ;\n"
    << "\t\tfoaf:homepage <" << stringCompanyWeb << ">\n\t] ;\n"
    << "\tlv2:project [\n\t\ta doap:Project ;\n"
    << "\t\tdoap:name \"" << stringPluginName << "\" ;\n"
    << "\t\tdoap:homepage <" << stringCompanyWeb << "> ;\n"
    << "\t\tdoap:maintainer [\n\t\t\ta foaf:Person ;\n"
    << "\t\t\tfoaf:name \"" << stringCompanyName << "\" ;\n"
    << "\t\t\tfoaf:homepage <" << stringCompanyWeb << ">\n\t\t]\n\t] ;\n"
    << "\tlv2:requiredFeature urid:map ;\n"
    << "\tlv2:optionalFeature lv2:hardRTCapable ,\n"
    << "\t\tbufsz:boundedBlockLength ,\n\t\topts:options ;\n"
    << "\topts:supportedOption bufsz:maxBlockLength ;\n"
    << "\tlv2:extensionData state:interface ;\n"
    << "\tui:ui <" << kUiUri << "> ;\n"
    << "\tlv2:port ";

  o << "[\n\t\ta lv2:AudioPort ,\n\t\t\tlv2:InputPort ;\n\t\tlv2:index " << kPortAudioIn
    << " ;\n\t\tlv2:symbol \"in\" ;\n\t\tlv2:name \"Input\"\n\t] , ";
  o << "[\n\t\ta lv2:AudioPort ,\n\t\t\tlv2:OutputPort ;\n\t\tlv2:index "
    << kPortAudioOutL
    << " ;\n\t\tlv2:symbol \"out_l\" ;\n\t\tlv2:name \"Output Left\"\n\t] , ";
  o << "[\n\t\ta lv2:AudioPort ,\n\t\t\tlv2:OutputPort ;\n\t\tlv2:index "
    << kPortAudioOutR
    << " ;\n\t\tlv2:symbol \"out_r\" ;\n\t\tlv2:name \"Output Right\"\n\t] , ";

  for (std::size_t i = 0; i < ports.size(); ++i) {
    controlPort(o, ports[i], kPortControlFirst + static_cast<std::uint32_t>(i));
    o << " , ";
  }

  // The host bypass as lv2:enabled, so a host's own bypass button drives it.
  o << "[\n\t\ta lv2:InputPort ,\n\t\t\tlv2:ControlPort ;\n\t\tlv2:index " << kPortEnabled
    << " ;\n\t\tlv2:symbol \"enabled\" ;\n\t\tlv2:name \"Enabled\" ;\n"
    << "\t\tlv2:designation lv2:enabled ;\n"
    << "\t\tlv2:default 1 ;\n\t\tlv2:minimum 0 ;\n\t\tlv2:maximum 1 ;\n"
    << "\t\tlv2:portProperty lv2:toggled ,\n\t\t\tlv2:integer\n\t] , ";

  // Latency: the same figure the VST3 reports through getLatencySamples().
  o << "[\n\t\ta lv2:OutputPort ,\n\t\t\tlv2:ControlPort ;\n\t\tlv2:index "
    << kPortLatency << " ;\n\t\tlv2:symbol \"latency\" ;\n\t\tlv2:name \"Latency\" ;\n"
    << "\t\tlv2:designation lv2:latency ;\n"
    << "\t\tlv2:portProperty lv2:reportsLatency ,\n\t\t\tlv2:integer ;\n"
    << "\t\tlv2:minimum 0 ;\n\t\tlv2:maximum 65536\n\t] .\n";
  return o.str();
}

std::string manifestTtl() {
  using namespace bbm::lv2;
  std::ostringstream o;
  o << "# GENERATED by tools/bbm_ttlgen.cpp. Do not edit.\n\n"
       "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n"
       "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
       "@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .\n\n"
    << "<" << kPluginUri << ">\n\ta lv2:Plugin ;\n\tlv2:binary <" << kDspBinary
    << "> ;\n\trdfs:seeAlso <bigbubblemuff.ttl> .\n\n"
    // The UI is declared here as well: hosts discover UIs while scanning manifests,
    // before any plug-in's full description is read. Both lv2:binary and the
    // deprecated ui:binary, because loaders differ in which they look for.
    << "<" << kUiUri << ">\n\ta ui:X11UI ;\n\tlv2:binary <" << kUiBinary
    << "> ;\n\tui:binary <" << kUiBinary << "> ;\n\trdfs:seeAlso <bigbubblemuff.ttl> .\n";
  return o.str();
}

bool writeFile(const std::string &path, const std::string &text) {
  const std::string temp = path + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out.flush())
      return false;
  }
  return std::rename(temp.c_str(), path.c_str()) == 0;
}

} // namespace

int main(int argc, char **argv) {
  const std::span<char *> args(argv, static_cast<std::size_t>(argc));
  if (args.size() != 2) {
    std::fputs("usage: bbm_ttlgen <bundle directory>\n", stderr);
    return 2;
  }
  const std::string dir(args[1]);

  const IPtr<Controller> ctl = owned(new Controller());
  if (ctl->initialize(nullptr) != kResultOk) {
    std::fputs("bbm_ttlgen: the controller refused to initialise\n", stderr);
    return 1;
  }
  const std::vector<Port> ports = collect(*ctl);
  ctl->terminate();
  if (gFailures > 0) {
    std::fprintf(stderr, "bbm_ttlgen: %d problem(s); no TTL written\n", gFailures);
    return 1;
  }
  if (!writeFile(dir + "/manifest.ttl", manifestTtl()) ||
      !writeFile(dir + "/bigbubblemuff.ttl", pluginTtl(ports))) {
    std::fprintf(stderr, "bbm_ttlgen: cannot write into %s\n", dir.c_str());
    return 1;
  }
  std::printf("bbm_ttlgen: %u ports -> %s\n", static_cast<unsigned>(bbm::lv2::kPortCount),
              dir.c_str());
  return 0;
}
