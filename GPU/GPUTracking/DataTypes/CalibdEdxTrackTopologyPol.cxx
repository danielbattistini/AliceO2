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

#include "Rtypes.h"
#include "CalibdEdxTrackTopologyPol.h"

#include <cstddef>
#include <string_view>
#if !defined(GPUCA_STANDALONE)
#include "TFile.h"
#endif

using namespace o2::tpc;

#if !defined(GPUCA_GPUCODE) && !defined(GPUCA_STANDALONE) // code invisible on GPU and in the standalone compilation
#include "NDPiecewisePolynomials.inc"
void CalibdEdxTrackTopologyPol::dumpToTree(const uint32_t nSamplingPoints[/* Dim */], const char* outName) const
{
  for (uint32_t i = 0; i < FFits; i++) {
    const auto treename = getPolyName(i, ChargeType::Max);
    mCalibPolsqMax[i].dumpToTree(nSamplingPoints, outName, treename.data(), false);
  }

  for (uint32_t i = 0; i < FFits; i++) {
    const auto treename = getPolyName(i, ChargeType::Tot);
    mCalibPolsqTot[i].dumpToTree(nSamplingPoints, outName, treename.data(), false);
  }
}
#endif

void CalibdEdxTrackTopologyPol::cloneFromObject(const CalibdEdxTrackTopologyPol& obj, char* newFlatBufferPtr)
{
  const char* oldFlatBufferPtr = obj.mFlatBufferPtr;
  FlatObject::cloneFromObject(obj, newFlatBufferPtr);

  for (uint32_t i = 0; i < FFits; i++) {
    char* buffer = FlatObject::relocatePointer(oldFlatBufferPtr, mFlatBufferPtr, obj.mCalibPolsqTot[i].getFlatBufferPtr());
    mCalibPolsqTot[i].cloneFromObject(obj.mCalibPolsqTot[i], buffer);
  }

  for (uint32_t i = 0; i < FFits; i++) {
    char* buffer = FlatObject::relocatePointer(oldFlatBufferPtr, mFlatBufferPtr, obj.mCalibPolsqMax[i].getFlatBufferPtr());
    mCalibPolsqMax[i].cloneFromObject(obj.mCalibPolsqMax[i], buffer);
  }

  for (int32_t i = 0; i < FFits; ++i) {
    mScalingFactorsqTot[i] = obj.mScalingFactorsqTot[i];
    mScalingFactorsqMax[i] = obj.mScalingFactorsqMax[i];
  }
}

void CalibdEdxTrackTopologyPol::moveBufferTo(char* newFlatBufferPtr)
{
  char* oldFlatBufferPtr = mFlatBufferPtr;
  FlatObject::moveBufferTo(newFlatBufferPtr);
  char* currFlatBufferPtr = mFlatBufferPtr;
  mFlatBufferPtr = oldFlatBufferPtr;
  setActualBufferAddress(currFlatBufferPtr);
}

void CalibdEdxTrackTopologyPol::destroy()
{
  for (uint32_t i = 0; i < FFits; i++) {
    mCalibPolsqTot[i].destroy();
    mCalibPolsqMax[i].destroy();
  }
  FlatObject::destroy();
}

void CalibdEdxTrackTopologyPol::setActualBufferAddress(char* actualFlatBufferPtr)
{
  FlatObject::setActualBufferAddress(actualFlatBufferPtr);
  size_t offset = 0;
  for (uint32_t i = 0; i < FFits; i++) {
    offset = alignSize(offset, mCalibPolsqTot[i].getBufferAlignmentBytes());
    mCalibPolsqTot[i].setActualBufferAddress(mFlatBufferPtr + offset);
    offset += mCalibPolsqTot[i].getFlatBufferSize();
  }
  for (uint32_t i = 0; i < FFits; i++) {
    offset = alignSize(offset, mCalibPolsqMax[i].getBufferAlignmentBytes());
    mCalibPolsqMax[i].setActualBufferAddress(mFlatBufferPtr + offset);
    offset += mCalibPolsqMax[i].getFlatBufferSize();
  }
}

void CalibdEdxTrackTopologyPol::setFutureBufferAddress(char* futureFlatBufferPtr)
{
  for (uint32_t i = 0; i < FFits; i++) {
    char* buffer = relocatePointer(mFlatBufferPtr, futureFlatBufferPtr, mCalibPolsqTot[i].getFlatBufferPtr());
    mCalibPolsqTot[i].setFutureBufferAddress(buffer);
  }
  for (uint32_t i = 0; i < FFits; i++) {
    char* buffer = relocatePointer(mFlatBufferPtr, futureFlatBufferPtr, mCalibPolsqMax[i].getFlatBufferPtr());
    mCalibPolsqMax[i].setFutureBufferAddress(buffer);
  }
  FlatObject::setFutureBufferAddress(futureFlatBufferPtr);
}

#if !defined(GPUCA_STANDALONE)

void CalibdEdxTrackTopologyPol::construct()
{
  FlatObject::startConstruction();

  size_t buffSize = 0;
  size_t offsets1[FFits];
  size_t offsets2[FFits];

  for (int32_t index = 0; index < FFits; ++index) {
    buffSize = alignSize(buffSize, mCalibPolsqTot[index].getBufferAlignmentBytes());
    offsets1[index] = buffSize;
    buffSize += mCalibPolsqTot[index].getFlatBufferSize();
  }
  for (int32_t index = 0; index < FFits; ++index) {
    buffSize = alignSize(buffSize, mCalibPolsqMax[index].getBufferAlignmentBytes());
    offsets2[index] = buffSize;
    buffSize += mCalibPolsqMax[index].getFlatBufferSize();
  }

  FlatObject::finishConstruction(buffSize);

  for (uint32_t i = 0; i < FFits; i++) {
    mCalibPolsqTot[i].moveBufferTo(mFlatBufferPtr + offsets1[i]);
  }
  for (uint32_t i = 0; i < FFits; i++) {
    mCalibPolsqMax[i].moveBufferTo(mFlatBufferPtr + offsets2[i]);
  }
}

void CalibdEdxTrackTopologyPol::setDefaultPolynomials()
{
  for (int32_t i = 0; i < FFits; ++i) {
    const uint32_t n[FDim]{6, 5, 5, 5, 5};

    //                        z tan(theta) sin(phi) |relPad| relTime
    const float minqMax[FDim]{0, 0, 0, 0, -0.5f};
    const float maxqMax[FDim]{250, 1.5f, 0.9f, 0.5f, 0.5f};
    mCalibPolsqMax[i].init(minqMax, maxqMax, n);
    mCalibPolsqMax[i].setDefault();

    //                        z tan(theta) sin(phi) threshold <qTot>
    const float minqTot[FDim]{0, 0, 0, 2, 30};
    const float maxqTot[FDim]{250, 1.5f, 0.9f, 5, 200};
    mCalibPolsqTot[i].init(minqTot, maxqTot, n);
    mCalibPolsqTot[i].setDefault();
  }
  construct();
}

void CalibdEdxTrackTopologyPol::writeToFile(TFile& outf, const char* name) const
{
  CalibdEdxTrackTopologyPolContainer cont;
  cont.mCalibPols.reserve(FFits);

  for (const auto& par : mCalibPolsqTot) {
    cont.mCalibPols.emplace_back(par.getContainer());
  }

  for (const auto& par : mCalibPolsqMax) {
    cont.mCalibPols.emplace_back(par.getContainer());
  }

  for (const auto par : mScalingFactorsqTot) {
    cont.mScalingFactorsqTot.emplace_back(par);
  }

  for (const auto par : mScalingFactorsqMax) {
    cont.mScalingFactorsqMax.emplace_back(par);
  }

  outf.WriteObject(&cont, name);
}

void CalibdEdxTrackTopologyPol::setFromContainer(const CalibdEdxTrackTopologyPolContainer& container)
{
  if (2 * FFits != container.mCalibPols.size()) {
    LOGP(warning, "wrong number of polynomials stored! this {} container {}", 2 * FFits, container.mCalibPols.size());
    return;
  }

  const auto nFacCont = container.mScalingFactorsqMax.size() + container.mScalingFactorsqTot.size();
  if (2 * FFits != nFacCont) {
    LOGP(warning, "wrong number of scaling factors stored! this {} container {}", 2 * FFits, nFacCont);
    return;
  }

  for (int32_t i = 0; i < FFits; ++i) {
    mCalibPolsqTot[i].setFromContainer(container.mCalibPols[i]);
  }

  for (int32_t i = 0; i < FFits; ++i) {
    mCalibPolsqMax[i].setFromContainer(container.mCalibPols[FFits + i]);
  }

  for (int32_t i = 0; i < FFits; ++i) {
    mScalingFactorsqTot[i] = container.mScalingFactorsqTot[i];
    mScalingFactorsqMax[i] = container.mScalingFactorsqMax[i];
  }
  construct();
}

void CalibdEdxTrackTopologyPol::loadFromFile(const char* fileName, const char* name)
{
  TFile inpf(fileName, "READ");
  CalibdEdxTrackTopologyPolContainer* polTmp = nullptr;
  inpf.GetObject(name, polTmp);
  if (polTmp) {
    setFromContainer(*polTmp);
    delete polTmp;
  } else {
    LOGP(info, "couldnt load object {} from input file", name);
  }
}

void CalibdEdxTrackTopologyPol::setPolynomialsFromFile(TFile& inpf)
{
  for (int32_t ireg = 0; ireg < FFits; ++ireg) {
    const auto polnameqTot = getPolyName(ireg, ChargeType::Tot);
    mCalibPolsqTot[ireg].loadFromFile(inpf, polnameqTot.data());
    const auto polnameqMax = getPolyName(ireg, ChargeType::Max);
    mCalibPolsqMax[ireg].loadFromFile(inpf, polnameqMax.data());
  }
  construct();
}

std::string CalibdEdxTrackTopologyPol::getPolyName(const int32_t region, const ChargeType charge)
{
  const std::string typeName[2] = {"qMax", "qTot"};
  const std::string polname = fmt::format("polynomial_{}_region{}", typeName[charge], region).data();
  return polname;
}

#endif
