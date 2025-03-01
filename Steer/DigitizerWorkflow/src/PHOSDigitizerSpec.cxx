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

#include "PHOSDigitizerSpec.h"
#include "Framework/ConfigParamRegistry.h"
#include "Framework/ControlService.h"
#include "Framework/DataProcessorSpec.h"
#include "Framework/CCDBParamSpec.h"
#include "Framework/DataRefUtils.h"
#include "Framework/Lifetime.h"
#include "TStopwatch.h"
#include "Steer/HitProcessingManager.h" // for DigitizationContext
#include "TChain.h"
#include "CommonDataFormat/EvIndex.h"
#include "DataFormatsPHOS/TriggerRecord.h"
#include "PHOSSimulation/Digitizer.h"
#include "PHOSBase/PHOSSimParams.h"
#include "DataFormatsParameters/GRPObject.h"
#include "DataFormatsPHOS/MCLabel.h"
#include "DetectorsRaw/HBFUtils.h"
#include <SimulationDataFormat/MCTruthContainer.h>

using namespace o2::framework;
using SubSpecificationType = o2::framework::DataAllocator::SubSpecificationType;

namespace o2
{
namespace phos
{

void DigitizerSpec::initDigitizerTask(framework::InitContext& ic)
{

  auto simulatePileup = ic.options().get<int>("pileup");
  if (simulatePileup) {                                                // set readout time and dead time parameters
    mReadoutTime = o2::phos::PHOSSimParams::Instance().mReadoutTimePU; // PHOS readout time in ns
    mDeadTime = o2::phos::PHOSSimParams::Instance().mDeadTimePU;       // PHOS dead time (should include readout => mReadoutTime< mDeadTime)
  } else {
    mReadoutTime = o2::phos::PHOSSimParams::Instance().mReadoutTime; // PHOS readout time in ns
    mDeadTime = o2::phos::PHOSSimParams::Instance().mDeadTime;       // PHOS dead time (should include readout => mReadoutTime< mDeadTime)
  }

  // init digitizer
  mDigitizer.init();
  auto mctruth = ic.options().get<bool>("mctruth");
  if (!mctruth) {
    mDigitizer.processMC(false);
  }

  if (mHits) {
    delete mHits;
  }
  mHits = new std::vector<Hit>();
}

void DigitizerSpec::run(framework::ProcessingContext& pc)
{
  // read collision context from input
  auto context = pc.inputs().get<o2::steer::DigitizationContext*>("collisioncontext");
  context->initSimChains(o2::detectors::DetID::PHS, mSimChains);
  auto& timesview = context->getEventRecords();
  LOG(debug) << "GOT " << timesview.size() << " COLLISSION TIMES";

  if (mInitSimParams) { // trigger reading sim/rec parameters from CCDB, singleton initiated in Fetcher
    pc.inputs().get<o2::phos::PHOSSimParams*>("recoparams");
    mInitSimParams = false;
  }

  // if there is nothing to do ... return
  int n = timesview.size();
  if (n == 0) {
    return;
  }

  TStopwatch timer;
  timer.Start();

  if (mDigitizer.runStartTime() == 0) { // not set yet
    long runStartTime = timesview[0].getTimeNS();
    mDigitizer.setRunStartTime(runStartTime); // set timestamp to access CCDB if necessary
  }

  LOG(info) << " CALLING PHOS DIGITIZATION ";
  std::vector<TriggerRecord> triggers;

  int indexStart = mDigitsOut.size();
  auto& eventParts = context->getEventParts();
  // if this is last stream of hits and we can write directly to final vector of digits? Otherwize use temporary vectors
  mDigitsFinal.clear();
  mDigitsTmp.clear();
  bool isLastStream = true;
  double eventTime = timesview[0].getTimeNS() - o2::phos::PHOSSimParams::Instance().mDeadTime; // checked above that list not empty
  int eventId = 0;

  // the interaction record marking the timeframe start
  auto firstTF = InteractionTimeRecord(o2::raw::HBFUtils::Instance().getFirstSampledTFIR(), 0);

  // loop over all composite collisions given from context
  // (aka loop over all the interaction records)
  for (int collID = 0; collID < n; ++collID) {
    // Note: Very crude filter to neglect collisions coming before
    // the first interaction record of the timeframe. Remove this, once these collisions can be handled
    // within the digitization routine. Collisions before this timeframe might impact digits of this timeframe.
    // See https://its.cern.ch/jira/browse/O2-5395.
    if (timesview[collID] < firstTF) {
      LOG(info) << "Too early: Not digitizing collision " << collID;
      continue;
    }

    double dt = timesview[collID].getTimeNS() - eventTime; // start new PHOS readout, continue current or dead time?
    if (dt > mReadoutTime && dt < mDeadTime) {             // dead time, skip event
      continue;
    }

    if (dt >= o2::phos::PHOSSimParams::Instance().mDeadTime) { // start new event
      // new event
      eventTime = timesview[collID].getTimeNS();
      dt = 0.;
      eventId = collID;
    }

    // Check if next event has to be added to this read-out
    if (collID < n - 1) {
      isLastStream = (timesview[collID + 1].getTimeNS() - eventTime > mReadoutTime);
    } else {
      isLastStream = true;
    }

    // for each collision, loop over the constituents event and source IDs
    // (background signal merging is basically taking place here)
    // merge new hist to current digit list
    auto part = eventParts[collID].begin();
    while (part != eventParts[collID].end()) {
      // get the hits for this event and this source
      int source = part->sourceID;
      int entry = part->entryID;
      mHits->clear();
      context->retrieveHits(mSimChains, "PHSHit", source, entry, mHits);

      part++;
      if (part == eventParts[collID].end() && isLastStream) { // last stream, copy digits directly to output vector
        mDigitizer.processHits(mHits, mDigitsFinal, mDigitsOut, mLabels, entry, source, dt);
        mDigitsFinal.clear();
        // finalyze previous event and clean
        //  Add trigger record
        triggers.emplace_back(timesview[eventId], indexStart, mDigitsOut.size() - indexStart);
        indexStart = mDigitsOut.size();
      } else { // Fill intermediate digitvector
        mDigitsTmp.swap(mDigitsFinal);
        mDigitizer.processHits(mHits, mDigitsTmp, mDigitsFinal, mLabels, entry, source, dt);
        mDigitsTmp.clear();
      }
    }
  }
  LOG(debug) << "Have " << mLabels.getNElements() << " PHOS labels ";
  // here we have all digits and we can send them to consumer (aka snapshot it onto output)
  pc.outputs().snapshot(Output{"PHS", "DIGITS", 0}, mDigitsOut);
  pc.outputs().snapshot(Output{"PHS", "DIGITTRIGREC", 0}, triggers);
  if (pc.outputs().isAllowed({"PHS", "DIGITSMCTR", 0})) {
    pc.outputs().snapshot(Output{"PHS", "DIGITSMCTR", 0}, mLabels);
  }

  // PHOS is always a triggering detector
  const o2::parameters::GRPObject::ROMode roMode = o2::parameters::GRPObject::TRIGGERING;
  LOG(debug) << "PHOS: Sending ROMode= " << roMode << " to GRPUpdater";
  pc.outputs().snapshot(Output{"PHS", "ROMode", 0}, roMode);

  timer.Stop();
  LOG(info) << "Digitization took " << timer.CpuTime() << "s";

  // we should be only called once; tell DPL that this process is ready to exit
  pc.services().get<ControlService>().readyToQuit(QuitRequest::Me);
}

DataProcessorSpec getPHOSDigitizerSpec(int channel, bool mctruth)
{
  std::vector<OutputSpec> outputs;
  outputs.emplace_back("PHS", "DIGITS", 0, Lifetime::Timeframe);
  outputs.emplace_back("PHS", "DIGITTRIGREC", 0, Lifetime::Timeframe);
  if (mctruth) {
    outputs.emplace_back("PHS", "DIGITSMCTR", 0, Lifetime::Timeframe);
  }
  outputs.emplace_back("PHS", "ROMode", 0, Lifetime::Timeframe);

  std::vector<o2::framework::InputSpec> inputs;
  inputs.emplace_back("collisioncontext", "SIM", "COLLISIONCONTEXT", static_cast<SubSpecificationType>(channel), Lifetime::Timeframe);
  inputs.emplace_back("recoparams", o2::header::gDataOriginPHS, "PHS_RecoParams", 0, o2::framework::Lifetime::Condition, o2::framework::ccdbParamSpec("PHS/Config/RecoParams"));

  // create the full data processor spec using
  //  a name identifier
  //  input description
  //  algorithmic description (here a lambda getting called once to setup the actual processing function)
  //  options that can be used for this processor (here: input file names where to take the hits)
  return DataProcessorSpec{
    "PHOSDigitizer",
    inputs,
    outputs,
    AlgorithmSpec{o2::framework::adaptFromTask<DigitizerSpec>()},
    Options{{"pileup", VariantType::Int, 1, {"whether to run in continuous time mode"}},
            {"mctruth", VariantType::Bool, true, {"whether to process MC info"}}}};
}
} // namespace phos
} // namespace o2
