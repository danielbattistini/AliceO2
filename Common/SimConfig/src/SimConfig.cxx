// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <SimConfig/SimConfig.h>
#include <SimConfig/DetectorLists.h>
#include <DetectorsCommonDataFormats/DetID.h>
#include <SimulationDataFormat/DigitizationContext.h>
#include <boost/program_options.hpp>
#include <iostream>
#include <fairlogger/Logger.h>
#include <thread>
#include <cmath>
#include <chrono>
#include <regex>

using namespace o2::conf;
namespace bpo = boost::program_options;

void SimConfig::initOptions(boost::program_options::options_description& options, bool isUpgrade)
{
  // some default args might depend on whether Run3 or Run5
  // can be updated here:
  std::string defaultGeomList{"ALICE2"};
  if (isUpgrade == true) {
    defaultGeomList = "ALICE3";
  }

  int nsimworkersdefault = std::max(1u, std::thread::hardware_concurrency() / 2);
  options.add_options()(
    "mcEngine,e", bpo::value<std::string>()->default_value("TGeant4"), "VMC backend to be used.")(
    "generator,g", bpo::value<std::string>()->default_value("boxgen"), "Event generator to be used.")(
    "trigger,t", bpo::value<std::string>()->default_value(""), "Event generator trigger to be used.")(
    "modules,m", bpo::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>({"all"}), "all modules"), "list of modules included in geometry")(
    "skipModules", bpo::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>({""}), ""), "list of modules excluded in geometry (precendence over -m")(
    "readoutDetectors", bpo::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(), ""), "list of detectors creating hits, all if not given; added to to active modules")(
    "skipReadoutDetectors", bpo::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(), ""), "list of detectors to skip hit creation (precendence over --readoutDetectors")(
    "detectorList", bpo::value<std::string>()->default_value(defaultGeomList),
    "Use a specific version of ALICE, e.g., a predefined list."
    "There is an 'official' list provided with:"
    "\nALICE2  : The default configuration for Run 3"
    "\nALICE2.1: The future configuration for Run 4"
    "\nALICE3  : The far-future configuration for Run 5-6"
    "\nAdditionally one can provide their own custom list of modules which should be included in the geometry."
    "\nBy specifiying LIST:JSONFILE where LIST is a list present in JSONFILE.")(
    "nEvents,n", bpo::value<unsigned int>()->default_value(0), "number of events")(
    "startEvent", bpo::value<unsigned int>()->default_value(0), "index of first event to be used (when applicable)")(
    "extKinFile", bpo::value<std::string>()->default_value("Kinematics.root"),
    "name of kinematics file for event generator from file (when applicable)")(
    "embedIntoFile", bpo::value<std::string>()->default_value(""),
    "filename containing the reference events to be used for the embedding")(
    "bMax,b", bpo::value<float>()->default_value(0.), "maximum value for impact parameter sampling (when applicable)")(
    "isMT", bpo::value<bool>()->default_value(false), "multi-threaded mode (Geant4 only")(
    "outPrefix,o", bpo::value<std::string>()->default_value("o2sim"), "prefix of output files")(
    "logseverity", bpo::value<std::string>()->default_value("INFO"), "severity level for FairLogger")(
    "logverbosity", bpo::value<std::string>()->default_value("medium"), "level of verbosity for FairLogger (low, medium, high, veryhigh)")(
    "configKeyValues", bpo::value<std::string>()->default_value(""), "semicolon separated key=value strings (e.g.: 'TPC.gasDensity=1;...")(
    "configFile", bpo::value<std::string>()->default_value(""), "Path to an INI or JSON configuration file")(
    "chunkSize", bpo::value<unsigned int>()->default_value(500), "max size of primary chunk (subevent) distributed by server")(
    "chunkSizeI", bpo::value<int>()->default_value(-1), "internalChunkSize")(
    "seed", bpo::value<ULong_t>()->default_value(0), "initial seed as ULong_t (default: 0 == random)")(
    "field", bpo::value<std::string>()->default_value("-5"), "L3 field rounded to kGauss, allowed values +-2,+-5 and 0; +-<intKGaus>U for uniform field; \"ccdb\" for taking it from CCDB ")("vertexMode", bpo::value<std::string>()->default_value("kDiamondParam"), "Where the beam-spot vertex should come from. Must be one of kNoVertex, kDiamondParam, kCCDB")(
    "nworkers,j", bpo::value<int>()->default_value(nsimworkersdefault), "number of parallel simulation workers (only for parallel mode)")(
    "noemptyevents", "only writes events with at least one hit")(
    "CCDBUrl", bpo::value<std::string>()->default_value("http://alice-ccdb.cern.ch"), "URL for CCDB to be used.")(
    "timestamp", bpo::value<uint64_t>(), "global timestamp value in ms (for anchoring) - default is now ... or beginning of run if ALICE run number was given")(
    "run", bpo::value<int>()->default_value(-1), "ALICE run number")(
    "asservice", bpo::value<bool>()->default_value(false), "run in service/server mode")(
    "noGeant", bpo::bool_switch(), "prohibits any Geant transport/physics (by using tight cuts)")(
    "forwardKine", bpo::bool_switch(), "forward kinematics on a FairMQ channel")(
    "noDiscOutput", bpo::bool_switch(), "switch off writing sim results to disc (useful in combination with forwardKine)");
  options.add_options()("fromCollContext", bpo::value<std::string>()->default_value(""), "Use a pregenerated collision context to infer number of events to simulate, how to embedd them, the vertex position etc. Takes precedence of other options such as \"--nEvents\". The format is COLLISIONCONTEXTFILE.root[:SIGNALNAME] where SIGNALNAME is the event part in the context which is relevant.");
}

void SimConfig::determineActiveModules(std::vector<std::string> const& inputargs, std::vector<std::string> const& skippedModules, std::vector<std::string>& activeModules, bool isUpgrade)
{
  using o2::detectors::DetID;

  // input args is a vector of module strings as obtained from the -m,--modules options
  // of SimConfig
  activeModules = inputargs;
#ifdef ENABLE_UPGRADES
  if (activeModules[0] != "all") {
    if (isUpgrade) {
      for (int i = 0; i < activeModules.size(); ++i) {
        if (activeModules[i] != "A3IP" &&
            activeModules[i] != "IT3" &&
            activeModules[i] != "TRK" &&
            activeModules[i] != "FT3" &&
            activeModules[i] != "FCT" &&
            activeModules[i] != "TF3" &&
            activeModules[i] != "RCH" &&
            activeModules[i] != "MI3" &&
            activeModules[i] != "ECL") {
          LOGP(fatal, "List of active modules contains {}, which is not a module from the upgrades.", activeModules[i]);
        }
      }
    }
    if (!isUpgrade) {
      for (int i = 0; i < activeModules.size(); ++i) {
        if (activeModules[i] == "A3IP" ||
            activeModules[i] == "TRK" ||
            activeModules[i] == "FT3" ||
            activeModules[i] == "FCT" ||
            activeModules[i] == "TF3" ||
            activeModules[i] == "RCH" ||
            activeModules[i] == "MI3" ||
            activeModules[i] == "ECL") {
          LOGP(fatal, "List of active modules contains {}, which is not a run 3 module", activeModules[i]);
        }
      }
    }
  }
#endif
  if (activeModules.size() == 1 && activeModules[0] == "all") {
    activeModules.clear();
#ifdef ENABLE_UPGRADES
    if (isUpgrade) {
      for (int d = DetID::First; d <= DetID::Last; ++d) {
        if (d == DetID::TRK ||
            d == DetID::FT3 ||
            d == DetID::FCT ||
            d == DetID::TF3 ||
            d == DetID::RCH ||
            d == DetID::ECL ||
            d == DetID::MI3) {
          activeModules.emplace_back(DetID::getName(d));
        }
      }
      activeModules.emplace_back("A3IP");
      activeModules.emplace_back("A3ABSO");
      activeModules.emplace_back("A3MAG");
    } else {
#endif
      // add passive components manually (make a PassiveDetID for them!)
      activeModules.emplace_back("HALL");
      activeModules.emplace_back("MAG");
      activeModules.emplace_back("DIPO");
      activeModules.emplace_back("COMP");
      activeModules.emplace_back("PIPE");
      activeModules.emplace_back("ABSO");
      activeModules.emplace_back("SHIL");
      for (int d = DetID::First; d <= DetID::Last; ++d) {
#ifdef ENABLE_UPGRADES
        if (d != DetID::IT3 && d != DetID::TRK && d != DetID::FT3 && d != DetID::FCT && d != DetID::TF3 && d != DetID::RCH && d != DetID::ECL && d != DetID::MI3) {
          activeModules.emplace_back(DetID::getName(d));
        }
      }
#else
      activeModules.emplace_back(DetID::getName(d));
#endif
    }
  }
  filterSkippedElements(activeModules, skippedModules);
}

bool SimConfig::determineActiveModulesList(const std::string& version, std::vector<std::string> const& inputargs, std::vector<std::string> const& skippedModules, std::vector<std::string>& activeModules)
{
  DetectorList_t modules;
  DetectorMap_t map;
  if (auto pos = version.find(':'); pos != std::string::npos) {
    auto pversion = version.substr(0, pos);
    auto ppath = version.substr(pos + 1);
    if (!parseDetectorMapfromJSON(ppath, map)) {
      LOGP(error, "Could not parse {}; check errors above!", ppath);
      return false;
    }
    if (map.find(pversion) == map.end()) {
      LOGP(error, "List {} is not defined in custom JSON file!", pversion);
      printDetMap(map);
      return false;
    }
    modules = map[pversion];
    LOGP(info, "Running with version {} from custom detector list '{}'", pversion, ppath);
  } else {
    // Otherwise check 'official' versions which provided in config
    auto o2env = std::getenv("O2_ROOT");
    if (!o2env) {
      LOGP(error, "O2_ROOT environment not defined");
      return false;
    }
    const std::string rootpath(fmt::format("{}/share/config/o2simdefaultdetectorlist.json", o2env));
    if (!parseDetectorMapfromJSON(rootpath, map)) {
      LOGP(error, "Could not parse {} -> check errors above!", rootpath);
      return false;
    }
    if (map.find(version) == map.end()) {
      LOGP(error, "List {} is not defined in 'official' JSON file!", version);
      printDetMap(map);
      return false;
    }
    modules = map[version];
    LOGP(info, "Running with official detector version '{}'", version);
  }
  // check if specified modules are in list
  if (inputargs.size() != 1 || inputargs[0] != "all") {
    std::vector<std::string> diff;
    for (const auto& in : inputargs) {
      if (std::find(modules.begin(), modules.end(), in) == std::end(modules)) {
        diff.emplace_back(in);
      }
    }
    if (!diff.empty()) {
      LOGP(error, "Modules specified that are not present in detector list {}", version);
      for (int j{0}; const auto& m : diff) {
        LOGP(info, " - {: <2}. {}", j++, m);
      }
      printDetMap(map, version);
      return false;
    }
  }
  // Insert into active modules if module is built buy -m or insert all if default for -m is used ("all")
  std::copy_if(modules.begin(), modules.end(), std::back_inserter(activeModules),
               [&inputargs](const auto& e) { return (inputargs.size() == 1 && inputargs[0] == "all") || (std::find(inputargs.begin(), inputargs.end(), e) != inputargs.end()); });
  return filterSkippedElements(activeModules, skippedModules);
}

void SimConfig::determineReadoutDetectors(std::vector<std::string> const& activeModules, std::vector<std::string> const& enableReadout, std::vector<std::string> const& disableReadout, std::vector<std::string>& readoutDetectors)
{
  using o2::detectors::DetID;

  readoutDetectors.clear();

  auto isDet = [](std::string const& s) {
    return DetID::nameToID(s.c_str()) >= DetID::First;
  };

  if (enableReadout.empty()) {
    // if no readout explicitly given, use all detectors from active modules
    for (auto& am : activeModules) {
      if (!isDet(am)) {
        // either we found a passive module or one with disabled readout ==> skip
        continue;
      }
      readoutDetectors.emplace_back(am);
    }
  } else {
    for (auto& er : enableReadout) {
      if (!isDet(er)) {
        // either we found a passive module or one with disabled readout ==> skip
        LOG(fatal) << "Enabled readout for " << er << " which is not a detector.";
      }
      if (std::find(activeModules.begin(), activeModules.end(), er) == activeModules.end()) {
        // add to active modules if not yet there
        LOG(fatal) << "Module " << er << " not constructed and cannot be used for readout (make sure it is contained in -m option).";
      }
      readoutDetectors.emplace_back(er);
    }
  }
  for (auto& dr : disableReadout) {
    if (!isDet(dr)) {
      // either we found a passive module or one with disabled readout ==> skip
      LOG(fatal) << "Disabled readout for " << dr << " which is not a detector.";
    }
    if (std::find(activeModules.begin(), activeModules.end(), dr) == activeModules.end()) {
      // add to active modules if not yet there
      LOG(fatal) << "Module " << dr << " not constructed, makes no sense to disable its readout (make sure it is contained in -m option).";
    }
    auto iter = std::find(readoutDetectors.begin(), readoutDetectors.end(), dr);
    if (iter != readoutDetectors.end()) {
      readoutDetectors.erase(iter);
    }
  }
}

std::pair<std::string, std::string> SimConfig::getCollContextFilenameAndEventPrefix() const
{
  // we decompose the argument to fetch
  // (a) collision contextfilename
  // (b) sim prefix to use from the context
  auto pos = mConfigData.mFromCollisionContext.find(':');
  std::string collcontextfile{mConfigData.mFromCollisionContext};
  std::string simprefix{mConfigData.mOutputPrefix};
  if (pos != std::string::npos) {
    collcontextfile = mConfigData.mFromCollisionContext.substr(0, pos);
    simprefix = mConfigData.mFromCollisionContext.substr(pos + 1);
  }
  return std::make_pair(collcontextfile, simprefix);
}

bool SimConfig::resetFromParsedMap(boost::program_options::variables_map const& vm)
{
  using o2::detectors::DetID;
  mConfigData.mMCEngine = vm["mcEngine"].as<std::string>();
  mConfigData.mNoGeant = vm["noGeant"].as<bool>();

  // Reset modules and detectors as they are anyway re-parsed
  mConfigData.mReadoutDetectors.clear();
  mConfigData.mActiveModules.clear();

  // Get final set of active Modules
  if (!determineActiveModulesList(vm["detectorList"].as<std::string>(), vm["modules"].as<std::vector<std::string>>(), vm["skipModules"].as<std::vector<std::string>>(), mConfigData.mActiveModules)) {
    return false;
  }

  if (mConfigData.mNoGeant) {
    // CAVE is all that's needed (and that will be built either way), so clear all modules and set the O2TrivialMCEngine
    mConfigData.mActiveModules.clear();
    // force usage of O2TrivialMCEngine, no overhead from actual transport engine initialisation
    mConfigData.mMCEngine = "O2TrivialMCEngine";
  } else if (mConfigData.mMCEngine.compare("O2TrivialMCEngine") == 0) {
    LOG(error) << "The O2TrivialMCEngine engine can only be used with --noGeant option";
    return false;
  }

  const auto& activeModules = mConfigData.mActiveModules;

  // get final set of detectors which are readout
  determineReadoutDetectors(activeModules, vm["readoutDetectors"].as<std::vector<std::string>>(), vm["skipReadoutDetectors"].as<std::vector<std::string>>(), mConfigData.mReadoutDetectors);

  mConfigData.mGenerator = vm["generator"].as<std::string>();
  mConfigData.mTrigger = vm["trigger"].as<std::string>();
  mConfigData.mNEvents = vm["nEvents"].as<unsigned int>();
  mConfigData.mExtKinFileName = vm["extKinFile"].as<std::string>();
  mConfigData.mEmbedIntoFileName = vm["embedIntoFile"].as<std::string>();
  mConfigData.mStartEvent = vm["startEvent"].as<unsigned int>();
  mConfigData.mBMax = vm["bMax"].as<float>();
  mConfigData.mIsMT = vm["isMT"].as<bool>();
  mConfigData.mOutputPrefix = vm["outPrefix"].as<std::string>();
  mConfigData.mLogSeverity = vm["logseverity"].as<std::string>();
  mConfigData.mLogVerbosity = vm["logverbosity"].as<std::string>();
  mConfigData.mKeyValueTokens = vm["configKeyValues"].as<std::string>();
  mConfigData.mConfigFile = vm["configFile"].as<std::string>();
  mConfigData.mPrimaryChunkSize = vm["chunkSize"].as<unsigned int>();
  mConfigData.mInternalChunkSize = vm["chunkSizeI"].as<int>();
  mConfigData.mStartSeed = vm["seed"].as<ULong_t>();
  mConfigData.mSimWorkers = vm["nworkers"].as<int>();
  if (vm.count("timestamp")) {
    mConfigData.mTimestamp = vm["timestamp"].as<uint64_t>();
    mConfigData.mTimestampMode = TimeStampMode::kManual;
  } else {
    mConfigData.mTimestamp = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    mConfigData.mTimestampMode = TimeStampMode::kNow;
  }
  mConfigData.mRunNumber = vm["run"].as<int>();
  mConfigData.mCCDBUrl = vm["CCDBUrl"].as<std::string>();
  mConfigData.mAsService = vm["asservice"].as<bool>();
  mConfigData.mForwardKine = vm["forwardKine"].as<bool>();
  mConfigData.mWriteToDisc = !vm["noDiscOutput"].as<bool>();
  if (vm.count("noemptyevents")) {
    mConfigData.mFilterNoHitEvents = true;
  }
  mConfigData.mFromCollisionContext = vm["fromCollContext"].as<std::string>();
  auto collcontext_simprefix = getCollContextFilenameAndEventPrefix();
  adjustFromCollContext(collcontext_simprefix.first, collcontext_simprefix.second);

  // analyse vertex options
  if (!parseVertexModeString(vm["vertexMode"].as<std::string>(), mConfigData.mVertexMode)) {
    return false;
  }

  // analyse field options
  // either: "ccdb" or +-2[U],+-5[U] and 0[U]; +-<intKGaus>U
  auto& fieldstring = vm["field"].as<std::string>();
  std::regex re("(ccdb)|([+-]?(0|[2-9]|[12][0-9]|20)U?)");
  if (!std::regex_match(fieldstring, re)) {
    LOG(error) << "Invalid field option " << fieldstring;
    return false;
  }
  if (fieldstring == "ccdb") {
    mConfigData.mFieldMode = SimFieldMode::kCCDB;
  } else if (fieldstring.find("U") != std::string::npos) {
    mConfigData.mFieldMode = SimFieldMode::kUniform;
  }
  if (fieldstring != "ccdb") {
    mConfigData.mField = std::stoi((vm["field"].as<std::string>()).substr(0, (vm["field"].as<std::string>()).rfind("U")));
  }
  if (!parseFieldString(fieldstring, mConfigData.mField, mConfigData.mFieldMode)) {
    return false;
  }

  return true;
}

bool SimConfig::parseVertexModeString(std::string const& vertexstring, VertexMode& mode)
{
  // vertexstring must be either kNoVertex, kDiamondParam, kCCDB
  if (vertexstring == "kNoVertex") {
    mode = VertexMode::kNoVertex;
    return true;
  } else if (vertexstring == "kDiamondParam") {
    mode = VertexMode::kDiamondParam;
    return true;
  } else if (vertexstring == "kCCDB") {
    mode = VertexMode::kCCDB;
    return true;
  }
  LOG(error) << "Vertex mode must be one of kNoVertex, kDiamondParam, kCCDB";
  return false;
}

bool SimConfig::parseFieldString(std::string const& fieldstring, int& fieldvalue, SimFieldMode& mode)
{
  // analyse field options
  // either: "ccdb" or +-2[U],+-5[U] and 0[U]; +-<intKGaus>U
  std::regex re("(ccdb)|([+-]?(0|[2-9]|[12][0-9]|20)U?)");
  if (!std::regex_match(fieldstring, re)) {
    LOG(error) << "Invalid field option " << fieldstring;
    return false;
  }
  if (fieldstring == "ccdb") {
    mode = SimFieldMode::kCCDB;
  } else if (fieldstring.find("U") != std::string::npos) {
    mode = SimFieldMode::kUniform;
  }
  if (fieldstring != "ccdb") {
    fieldvalue = std::stoi(fieldstring.substr(0, fieldstring.rfind("U")));
  }
  return true;
}

bool SimConfig::filterSkippedElements(std::vector<std::string>& elements, std::vector<std::string> const& skipped)
{
  for (auto& s : skipped) {
    if (s.empty()) { // nothing to skip here
      continue;
    }
    auto iter = std::find(elements.begin(), elements.end(), s);
    if (iter != elements.end()) {
      // take it out
      elements.erase(iter);
    } else {
      LOGP(error, "Skipped modules specified that are not present in built modules!");
      LOGP(error, "Built modules:");
      for (int j{0}; const auto& m : elements) {
        LOGP(error, " + {: <2}. {}", j++, m);
      }
      std::vector<std::string> diff;
      for (const auto& skip : skipped) {
        if (std::find(elements.begin(), elements.end(), skip) == std::end(elements)) {
          diff.emplace_back(skip);
        }
      }
      LOGP(error, "Specified skipped modules not present in built modules:");
      for (int j{0}; const auto& m : diff) {
        LOGP(error, " - {: <2}. {}", j++, m);
      }
      return false;
    }
  }
  return true;
}

void SimConfig::adjustFromCollContext(std::string const& collcontextfile, std::string const& prefix)
{
  // When we use pregenerated collision contexts, some options
  // need to be auto-adjusted. Do so and inform about this in the logs.
  if (collcontextfile == "") {
    return;
  }

  auto context = o2::steer::DigitizationContext::loadFromFile(collcontextfile);
  if (context) {
    //  find the events belonging to a source that corresponds to a sim prefix
    LOG(info) << "Looking up simprefixes " << prefix;
    int sourceid = context->findSimPrefix(prefix);
    if (sourceid == -1) {
      LOG(error) << "Could not find collisions with sim prefix " << prefix << " in the collision context. The collision context specifies the following prefixes:";
      for (auto& sp : context->getSimPrefixes()) {
        LOG(info) << sp;
      }
      LOG(fatal) << "Aborting due to prefix error";
    } else {
      auto collisionmap = context->getCollisionIndicesForSource(sourceid);
      LOG(info) << "Found " << collisionmap.size() << " events in the collisioncontext for prefix " << prefix;

      // check if collisionmap is dense (otherwise it will get screwed up with order/indexing in ROOT output)
      bool good = true;
      for (auto index = 0; index < collisionmap.size(); ++index) {
        if (collisionmap.find(index) == collisionmap.end()) {
          good = false;
        }
      }
      if (!good) {
        LOG(fatal) << "events in collisioncontext are non-compact ";
      }

      // do some adjustments based on the number of events to be simulated
      if (mConfigData.mNEvents == 0 || mConfigData.mNEvents == collisionmap.size()) {
        // we take what is specified in the context
        mConfigData.mNEvents = collisionmap.size();
      } else {
        LOG(warning) << "The number of events on the command line " << mConfigData.mNEvents << " and in the collision context differ. We take the one from collision context " << collisionmap.size();
        mConfigData.mNEvents = collisionmap.size();
      }
      LOG(info) << "Setting number of events to simulate to " << mConfigData.mNEvents;
    }
  } else {
    LOG(fatal) << "Could not open collision context file " << collcontextfile;
  }
}

bool SimConfig::resetFromArguments(int argc, char* argv[])
{
  // Arguments parsing
  bpo::variables_map vm;
  bpo::options_description desc("Allowed options");
  desc.add_options()("help,h", "Produce help message.");
  initOptions(desc, mConfigData.mIsUpgrade);

  try {
    bpo::store(parse_command_line(argc, argv, desc), vm);

    // help
    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return false;
    }

    bpo::notify(vm);
  } catch (const bpo::error& e) {
    std::cerr << e.what() << "\n\n";
    std::cerr << "Error parsing command line arguments; Available options:\n";

    std::cerr << desc << std::endl;
    return false;
  }

  return resetFromParsedMap(vm);
}

namespace o2::conf
{
// returns a reconfig struct given a configuration string (boost program options format)
bool parseSimReconfigFromString(std::string const& argumentstring, SimReconfigData& data)
{
  namespace bpo = boost::program_options;

  bpo::options_description options("Allowed options");

  options.add_options()(
    "nEvents,n", bpo::value<unsigned int>(&data.nEvents)->default_value(0), "number of events")(
    "generator,g", bpo::value<std::string>(&data.generator)->default_value("boxgen"), "Event generator to be used.")(
    "trigger,t", bpo::value<std::string>(&data.trigger)->default_value(""), "Event generator trigger to be used.")(
    "startEvent", bpo::value<unsigned int>(&data.startEvent)->default_value(0), "index of first event to be used (when applicable)")(
    "extKinFile", bpo::value<std::string>(&data.extKinfileName)->default_value("Kinematics.root"),
    "name of kinematics file for event generator from file (when applicable)")(
    "embedIntoFile", bpo::value<std::string>(&data.embedIntoFileName)->default_value(""),
    "filename containing the reference events to be used for the embedding")(
    "bMax,b", bpo::value<float>(&data.mBMax)->default_value(0.), "maximum value for impact parameter sampling (when applicable)")(
    "outPrefix,o", bpo::value<std::string>(&data.outputPrefix)->default_value("o2sim"), "prefix of output files")(
    "outDir,d", bpo::value<std::string>(&data.outputDir), "directory where to put simulation output (created when non-existant")(
    "configKeyValues", bpo::value<std::string>(&data.keyValueTokens)->default_value(""), "semicolon separated key=value strings (e.g.: 'TPC.gasDensity=1;...")(
    "configFile", bpo::value<std::string>(&data.configFile)->default_value(""), "Path to an INI or JSON configuration file")(
    "chunkSize", bpo::value<unsigned int>(&data.primaryChunkSize)->default_value(500), "max size of primary chunk (subevent) distributed by server")(
    "seed", bpo::value<ULong_t>(&data.startSeed)->default_value(0L), "initial seed as ULong_t (default: 0 == random)")(
    "stop", bpo::value<bool>(&data.stop)->default_value(false), "control command to shut down daemon");

  bpo::variables_map vm;
  try {
    bpo::store(bpo::command_line_parser(bpo::split_unix(argumentstring))
                 .options(options)
                 .run(),
               vm);
    bpo::notify(vm);
  } catch (const bpo::error& e) {
    std::cerr << e.what() << "\n\n";
    std::cerr << "Error parsing ReConfig data; Available options:\n";
    std::cerr << options << std::endl;
    return false;
  }
  return true;
}

} // namespace o2::conf

ClassImp(o2::conf::SimConfig);
